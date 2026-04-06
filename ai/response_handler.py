"""
response_handler.py — VernisOS Remediation Response Handler (Phase 10)

Sends structured CMD|REMEDIATE frames to the kernel to trigger
automated responses when anomalies are detected.

Supported actions:
  - log       : log-only, no kernel action
  - throttle  : reduce scheduler quantum for a PID
  - kill      : terminate a process
  - revoke    : revoke a capability from a process
  - suspend   : pause scheduling for a PID
"""

from __future__ import annotations

import time
from dataclasses import dataclass, field
from enum import Enum
from typing import TYPE_CHECKING, Optional

if TYPE_CHECKING:
    from ai.corelib import EngineContext
    from ai.anomaly_detector import Anomaly


class RemediationAction(Enum):
    LOG      = "log"
    THROTTLE = "throttle"
    KILL     = "kill"
    REVOKE   = "revoke"
    SUSPEND  = "suspend"


@dataclass
class RemediationRule:
    """Maps an anomaly severity + optional detector to a remediation action."""
    min_severity:  str                    # "LOW", "MEDIUM", "HIGH", "CRITICAL"
    action:        RemediationAction
    target:        str = "source"         # "source" = anomaly source PID, or explicit PID
    param:         str = ""               # e.g., quantum ms for throttle, cap name for revoke
    repeat_limit:  int = 3                # max times to apply same action per PID within cooldown
    cooldown_sec:  float = 30.0


@dataclass
class RemediationRecord:
    """Tracks one sent remediation for cooldown/dedup."""
    pid:       int
    action:    RemediationAction
    timestamp: float
    anomaly:   str


SEVERITY_ORDER = {"LOW": 0, "MEDIUM": 1, "HIGH": 2, "CRITICAL": 3}

# Default rules — conservative escalation
DEFAULT_RULES: list[RemediationRule] = [
    RemediationRule(min_severity="CRITICAL", action=RemediationAction.KILL,
                    param="", repeat_limit=1, cooldown_sec=60.0),
    RemediationRule(min_severity="HIGH", action=RemediationAction.THROTTLE,
                    param="25", repeat_limit=3, cooldown_sec=30.0),
    RemediationRule(min_severity="MEDIUM", action=RemediationAction.LOG,
                    param="", repeat_limit=10, cooldown_sec=10.0),
    RemediationRule(min_severity="LOW", action=RemediationAction.LOG,
                    param="", repeat_limit=20, cooldown_sec=5.0),
]


class ResponseHandler:
    """
    Determines and sends remediation actions to the kernel
    based on anomaly severity.

    Protocol format sent to kernel:
        CMD|0|REMEDIATE|<action>|<target_pid>|<param>

    Examples:
        CMD|0|REMEDIATE|log|all|0
        CMD|0|REMEDIATE|throttle|15|25
        CMD|0|REMEDIATE|kill|15|0
        CMD|0|REMEDIATE|revoke|15|CAP_IPC_SEND
    """

    MAX_HISTORY = 200

    def __init__(self, ctx: "EngineContext",
                 rules: Optional[list[RemediationRule]] = None,
                 enabled: bool = True) -> None:
        self.ctx      = ctx
        self._rules   = rules or list(DEFAULT_RULES)
        self._enabled = enabled
        self._history: list[RemediationRecord] = []

    @property
    def enabled(self) -> bool:
        return self._enabled

    @enabled.setter
    def enabled(self, val: bool) -> None:
        self._enabled = val
        self.ctx.logger.info("response", f"Remediation {'enabled' if val else 'disabled'}")

    def handle_anomaly(self, anomaly: "Anomaly", source_pid: int = 0) -> Optional[str]:
        """
        Evaluate anomaly against rules and send remediation if appropriate.
        Returns the action taken as a string, or None if no action.
        """
        if not self._enabled:
            return None

        sev_str = anomaly.severity.value
        sev_ord = SEVERITY_ORDER.get(sev_str, 0)

        for rule in self._rules:
            rule_ord = SEVERITY_ORDER.get(rule.min_severity, 0)
            if sev_ord < rule_ord:
                continue

            target_pid = source_pid if rule.target == "source" else int(rule.target)

            if self._is_cooldown(target_pid, rule.action, rule.cooldown_sec, rule.repeat_limit):
                continue

            self._send_remediation(rule.action, target_pid, rule.param, anomaly)
            return f"{rule.action.value}|{target_pid}|{rule.param}"

        return None

    def _is_cooldown(self, pid: int, action: RemediationAction,
                     cooldown_sec: float, repeat_limit: int) -> bool:
        """Check if we've already sent this action for this PID too recently."""
        now    = time.monotonic()
        cutoff = now - cooldown_sec
        recent = [
            r for r in self._history
            if r.pid == pid and r.action == action and r.timestamp >= cutoff
        ]
        return len(recent) >= repeat_limit

    def _send_remediation(self, action: RemediationAction, pid: int,
                          param: str, anomaly: "Anomaly") -> bool:
        """Send CMD|REMEDIATE frame to kernel."""
        from ai.corelib import MessageFrame

        target_str = str(pid) if pid > 0 else "all"
        payload = f"REMEDIATE|{action.value}|{target_str}|{param or '0'}"

        frame = MessageFrame(msg_type="CMD", seq=0, payload=payload)
        success = self.ctx.conn.send_frame(frame)

        self.ctx.logger.info(
            "response",
            f"REMEDIATE {action.value} → PID {target_str} "
            f"(param={param or 'none'}, anomaly={anomaly.title})"
        )

        self._history.append(RemediationRecord(
            pid=pid, action=action,
            timestamp=time.monotonic(), anomaly=anomaly.title,
        ))
        if len(self._history) > self.MAX_HISTORY:
            self._history.pop(0)

        return success

    def recent_actions(self, last_n: int = 10) -> list[RemediationRecord]:
        return self._history[-last_n:]

    def status_report(self) -> str:
        total = len(self._history)
        if total == 0:
            return "Response handler: no remediations sent."
        by_action = {}
        for r in self._history:
            by_action[r.action.value] = by_action.get(r.action.value, 0) + 1
        parts = ", ".join(f"{k}:{v}" for k, v in by_action.items())
        return f"Response handler: {total} remediations sent — {parts}"
