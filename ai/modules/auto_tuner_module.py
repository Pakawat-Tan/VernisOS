"""
modules/auto_tuner_module.py — AI Auto-Tuning Module (Phase 11)

Wraps AutoTuner inside the AIModule interface:
  - Feeds every kernel event/stat into the AutoTuner
  - Sends CMD|TUNE frames to the kernel when a decision is made
  - Responds to CLI queries about current load and tuning history
"""

from __future__ import annotations

import re
from typing import TYPE_CHECKING

from .base import AIModule
from ai.auto_tuner import AutoTuner, TuningDecision, LoadLevel, _LOAD_LABELS

if TYPE_CHECKING:
    from ai.corelib import EngineContext


class AutoTunerModule(AIModule):
    """
    Integrates the Auto-Tuning Engine with the AI module pipeline.

    Priority 8 — sits between BehaviorMonitor (5) and SystemMonitor (10).
    """

    priority = 8

    TOPICS = re.compile(
        r"tune|tuning|auto.?tun|load|schedul|quantum|throttl|"
        r"memory.?pressure|mem.?use|cpu.?load|performance|optim",
        re.IGNORECASE,
    )

    def __init__(self, ctx: "EngineContext") -> None:
        super().__init__(ctx)
        self._tuner = AutoTuner(
            on_decision=self._on_decision,
            decision_interval_sec=5.0,   # re-evaluate every 5 s
        )

    # ---- Decision callback (called by AutoTuner's background timer) ----

    def _on_decision(self, decision: TuningDecision) -> None:
        self.ctx.logger.info(
            "auto-tuner",
            f"Decision: {decision}",
        )
        # Send CMD|TUNE frame to kernel so it can act on the suggestion
        from ai.corelib import MessageFrame
        self.ctx.conn.send_frame(
            MessageFrame(msg_type="CMD", seq=0, payload=decision.to_cmd_payload())
        )
        self.ctx.record_stat("last_tune_decision", str(decision))

    # ---- AIModule interface ----

    def can_handle(self, query: str) -> bool:
        return bool(self.TOPICS.search(query))

    def process(self, query: str) -> str:
        q = query.lower()

        if any(w in q for w in ("summary", "status", "load")):
            return self._status()

        if any(w in q for w in ("history", "recent", "last", "decision")):
            return self._history()

        if any(w in q for w in ("assess", "level")):
            load = self._tuner.assess_load()
            return f"Current load level: {_LOAD_LABELS[load]}"

        if any(w in q for w in ("force", "now", "apply")):
            d = self._tuner.compute_decision()
            if d:
                return f"Decision applied: {d}"
            return "No tuning action needed at this time."

        return self._status()

    def on_event(self, event_type: str, data: str) -> None:
        """Feed every kernel event and STAT metric into the tuner."""
        self._tuner.feed_event(event_type, data)

        if event_type == "STAT":
            parts = data.split("|", 1)
            if len(parts) == 2:
                try:
                    self._tuner.update_metric(parts[0], float(parts[1]))
                except ValueError:
                    pass

    # ---- Internal reports ----

    def _status(self) -> str:
        summary = self._tuner.summary()
        last    = self.ctx.get_stat("last_tune_decision")
        lines   = [f"Auto-Tuner: {summary}"]
        if last:
            lines.append(f"Last decision: {last}")
        return "\n".join(lines)

    def _history(self) -> str:
        decisions = self._tuner.recent_decisions(5)
        if not decisions:
            return "Auto-Tuner: no tuning decisions recorded yet."
        lines = [f"Recent tuning decisions ({len(decisions)}):"]
        for d in decisions:
            lines.append(f"  {d}")
        return "\n".join(lines)
