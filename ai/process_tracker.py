"""
process_tracker.py — VernisOS Per-Process Behavior Tracking (Phase 10)

Tracks per-PID state: lifecycle events, syscall counts, failure counts,
anomaly flags, and trust scoring with severity escalation.
"""

from __future__ import annotations

import time
from dataclasses import dataclass, field
from enum import Enum
from typing import Optional


class ProcessAction(Enum):
    FORK   = "fork"
    EXEC   = "exec"
    EXIT   = "exit"
    KILL   = "kill"


class TrustLevel(Enum):
    TRUSTED     = "trusted"
    NORMAL      = "normal"
    SUSPICIOUS  = "suspicious"
    UNTRUSTED   = "untrusted"


# Thresholds for trust level escalation
TRUST_THRESHOLDS = {
    "failures_to_suspicious": 3,
    "failures_to_untrusted":  6,
    "denials_to_suspicious":  2,
    "denials_to_untrusted":   5,
    "anomalies_to_suspicious": 2,
    "anomalies_to_untrusted":  4,
}


@dataclass
class ProcessProfile:
    """Behavioral profile for a single process."""

    pid:            int
    name:           str          = ""
    parent_pid:     int          = 0
    created_at:     float        = field(default_factory=time.monotonic)
    exited_at:      Optional[float] = None

    # Counters
    syscall_count:  int          = 0
    failure_count:  int          = 0
    denial_count:   int          = 0
    exception_count: int         = 0

    # Anomaly tracking
    anomaly_flags:  set          = field(default_factory=set)
    anomaly_count:  int          = 0

    # Trust
    trust:          TrustLevel   = TrustLevel.NORMAL

    # Recent events (ring buffer of last 32)
    _events:        list         = field(default_factory=list)

    @property
    def alive(self) -> bool:
        return self.exited_at is None

    @property
    def uptime(self) -> float:
        end = self.exited_at or time.monotonic()
        return end - self.created_at

    def record_event(self, event_type: str, detail: str = "") -> None:
        self._events.append((time.monotonic(), event_type, detail))
        if len(self._events) > 32:
            self._events.pop(0)

    def recent_events(self, last_n: int = 10) -> list[tuple]:
        return self._events[-last_n:]

    def recompute_trust(self) -> TrustLevel:
        """Recompute trust level based on current counters."""
        t = TRUST_THRESHOLDS

        if (self.failure_count >= t["failures_to_untrusted"] or
                self.denial_count >= t["denials_to_untrusted"] or
                self.anomaly_count >= t["anomalies_to_untrusted"]):
            self.trust = TrustLevel.UNTRUSTED
        elif (self.failure_count >= t["failures_to_suspicious"] or
              self.denial_count >= t["denials_to_suspicious"] or
              self.anomaly_count >= t["anomalies_to_suspicious"]):
            self.trust = TrustLevel.SUSPICIOUS
        return self.trust

    def summary(self) -> str:
        status = "alive" if self.alive else "exited"
        flags  = ", ".join(self.anomaly_flags) if self.anomaly_flags else "none"
        return (
            f"PID {self.pid} ({self.name or 'unknown'}) [{status}] "
            f"trust={self.trust.value} "
            f"syscalls={self.syscall_count} fails={self.failure_count} "
            f"denials={self.denial_count} excps={self.exception_count} "
            f"anomalies={self.anomaly_count} flags=[{flags}]"
        )


class ProcessTracker:
    """
    Tracks all known processes and their behavioral profiles.

    Feed kernel events (PROC, EXCP, DENY, FAIL) and it updates
    per-PID counters and trust levels.
    """

    MAX_EXITED = 64  # keep at most this many exited profiles

    def __init__(self) -> None:
        self._procs: dict[int, ProcessProfile] = {}
        self._exited_pids: list[int] = []

    def _get_or_create(self, pid: int) -> ProcessProfile:
        if pid not in self._procs:
            self._procs[pid] = ProcessProfile(pid=pid)
        return self._procs[pid]

    def get(self, pid: int) -> Optional[ProcessProfile]:
        return self._procs.get(pid)

    @property
    def active_count(self) -> int:
        return sum(1 for p in self._procs.values() if p.alive)

    @property
    def all_profiles(self) -> list[ProcessProfile]:
        return list(self._procs.values())

    def suspicious_processes(self) -> list[ProcessProfile]:
        return [
            p for p in self._procs.values()
            if p.trust in (TrustLevel.SUSPICIOUS, TrustLevel.UNTRUSTED) and p.alive
        ]

    # ---- Event handlers ----

    def on_proc_event(self, data: str) -> Optional[ProcessProfile]:
        """
        Handle PROC event. Expected format: 'pid|action[|name]'
        e.g. '15|fork|init', '15|exit|0'
        """
        parts = data.split("|")
        if len(parts) < 2:
            return None

        try:
            pid = int(parts[0])
        except ValueError:
            return None

        action = parts[1].lower()
        extra  = parts[2] if len(parts) > 2 else ""

        prof = self._get_or_create(pid)
        prof.record_event("PROC", f"{action}|{extra}")

        if action == "fork":
            if extra:
                prof.name = extra
        elif action == "exec":
            if extra:
                prof.name = extra
        elif action in ("exit", "kill"):
            prof.exited_at = time.monotonic()
            self._exited_pids.append(pid)
            self._gc_exited()

        return prof

    def on_exception(self, data: str) -> Optional[ProcessProfile]:
        """Handle EXCP event. Format: 'code|addr[|pid]'"""
        parts = data.split("|")
        pid = self._extract_pid(parts, idx=2)
        if pid is None:
            return None

        prof = self._get_or_create(pid)
        prof.exception_count += 1
        prof.failure_count += 1
        prof.record_event("EXCP", data)
        old_trust = prof.trust
        prof.recompute_trust()
        if prof.trust != old_trust:
            prof.anomaly_flags.add("trust_escalated")
        return prof

    def on_denial(self, data: str) -> Optional[ProcessProfile]:
        """Handle DENY event. Format: 'pid|reason'"""
        parts = data.split("|")
        pid = self._extract_pid(parts, idx=0)
        if pid is None:
            return None

        prof = self._get_or_create(pid)
        prof.denial_count += 1
        prof.record_event("DENY", data)
        prof.recompute_trust()
        return prof

    def on_syscall(self, data: str) -> Optional[ProcessProfile]:
        """Handle SYSCALL event. Format: 'pid|syscall_num'"""
        parts = data.split("|")
        pid = self._extract_pid(parts, idx=0)
        if pid is None:
            return None

        prof = self._get_or_create(pid)
        prof.syscall_count += 1
        prof.record_event("SYSCALL", data)
        return prof

    def on_failure(self, data: str) -> Optional[ProcessProfile]:
        """Handle FAIL event. Format: 'pid|reason'"""
        parts = data.split("|")
        pid = self._extract_pid(parts, idx=0)
        if pid is None:
            return None

        prof = self._get_or_create(pid)
        prof.failure_count += 1
        prof.record_event("FAIL", data)
        prof.recompute_trust()
        return prof

    def flag_anomaly(self, pid: int, flag: str) -> None:
        """Mark a process with an anomaly flag (e.g., 'fork_bomb', 'exception_spam')."""
        prof = self.get(pid)
        if prof:
            prof.anomaly_flags.add(flag)
            prof.anomaly_count += 1
            prof.recompute_trust()

    # ---- Queries ----

    def status_report(self) -> str:
        alive = [p for p in self._procs.values() if p.alive]
        suspicious = self.suspicious_processes()
        lines = [
            f"Process tracker: {len(alive)} active, "
            f"{len(self._procs)} total tracked, "
            f"{len(suspicious)} suspicious"
        ]
        if suspicious:
            lines.append("Suspicious processes:")
            for p in suspicious:
                lines.append(f"  {p.summary()}")
        return "\n".join(lines)

    def process_detail(self, pid: int) -> str:
        prof = self.get(pid)
        if not prof:
            return f"PID {pid}: not tracked."
        lines = [prof.summary()]
        events = prof.recent_events(5)
        if events:
            lines.append("  Recent events:")
            for ts, etype, detail in events:
                age = time.monotonic() - ts
                lines.append(f"    [{age:.1f}s ago] {etype}: {detail}")
        return "\n".join(lines)

    # ---- Internal ----

    @staticmethod
    def _extract_pid(parts: list[str], idx: int) -> Optional[int]:
        if len(parts) <= idx:
            return None
        try:
            return int(parts[idx])
        except ValueError:
            return None

    def _gc_exited(self) -> None:
        """Garbage-collect oldest exited profiles."""
        while len(self._exited_pids) > self.MAX_EXITED:
            old_pid = self._exited_pids.pop(0)
            self._procs.pop(old_pid, None)
