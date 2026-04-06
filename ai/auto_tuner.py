"""
auto_tuner.py — VernisOS AI Auto-Tuning Engine (Phase 11)

Monitors kernel metrics in real-time and generates tuning decisions:
  - SCHED_QUANTUM  : widen/narrow the scheduler time slice
  - SCHED_PRIO     : adjust process priority
  - MEM_PRESSURE   : warn about memory and suggest cleanup
  - THROTTLE       : flag a process as needing throttling
  - IDLE           : system is under-utilised — loosen constraints

Decision lifecycle:
  feed_event() / update_metric()
    └─ assess_load()
         └─ compute_decision()
              └─ on_decision callback  →  caller sends CMD to kernel
"""

from __future__ import annotations

import threading
import time
from collections import deque
from dataclasses import dataclass, field
from datetime import datetime
from enum import IntEnum
from typing import Callable, Deque, Dict, List, Optional


# =============================================================================
# Load level
# =============================================================================

class LoadLevel(IntEnum):
    LOW      = 0   # < 20 % activity — system idle
    NORMAL   = 1   # 20–60 %         — healthy
    HIGH     = 2   # 60–85 %         — busy, worth tuning
    CRITICAL = 3   # > 85 %          — act now


_LOAD_LABELS = {
    LoadLevel.LOW:      "LOW",
    LoadLevel.NORMAL:   "NORMAL",
    LoadLevel.HIGH:     "HIGH",
    LoadLevel.CRITICAL: "CRITICAL",
}


# =============================================================================
# Tuning decision
# =============================================================================

@dataclass
class TuningDecision:
    action:    str        # SCHED_QUANTUM | SCHED_PRIO | MEM_PRESSURE | THROTTLE | IDLE
    target:    str        # "scheduler" | "memory" | process-name
    value:     float      # numeric suggestion (0 if N/A)
    reason:    str        # human-readable rationale
    load:      LoadLevel
    timestamp: datetime = field(default_factory=datetime.now)

    def __str__(self) -> str:
        ts  = self.timestamp.strftime("%H:%M:%S")
        lvl = _LOAD_LABELS[self.load]
        return (
            f"[{ts}] {self.action} target={self.target} "
            f"value={self.value:.1f} load={lvl} - {self.reason}"
        )

    def to_cmd_payload(self) -> str:
        """Wire format sent inside CMD frame to kernel."""
        return f"TUNE|{self.action}|{self.target}|{self.value:.2f}|{self.reason}"


# =============================================================================
# Sliding-window rate tracker (events per second over a window)
# =============================================================================

class _RateWindow:
    def __init__(self, window_sec: float = 10.0) -> None:
        self._times: Deque[float] = deque()
        self._window = window_sec

    def record(self) -> None:
        now = time.monotonic()
        self._times.append(now)
        cutoff = now - self._window
        while self._times and self._times[0] < cutoff:
            self._times.popleft()

    @property
    def rate(self) -> float:
        """Events per second over the sliding window."""
        now = time.monotonic()
        cutoff = now - self._window
        while self._times and self._times[0] < cutoff:
            self._times.popleft()
        return len(self._times) / self._window


# =============================================================================
# Metrics snapshot
# =============================================================================

@dataclass
class MetricsSnapshot:
    process_count:  int   = 0
    event_rate:     float = 0.0   # events/sec
    exception_count:int   = 0
    anomaly_count:  int   = 0
    cpu_load_pct:   float = 0.0   # 0–100, estimated from tick pressure
    mem_pressure:   float = 0.0   # 0–100, from STAT|mem_used|<bytes>
    timestamp: datetime = field(default_factory=datetime.now)


# =============================================================================
# Auto-Tuner engine
# =============================================================================

class AutoTuner:
    """
    Core auto-tuning engine.

    Call feed_event() for every kernel event and update_metric() for
    STAT-type numeric updates. Call compute_decision() periodically
    (e.g. every 5 s) to obtain a TuningDecision (or None if no change
    is warranted). An on_decision callback fires automatically.
    """

    # Thresholds for load assessment
    _HIGH_RATE     = 20.0   # events/sec → HIGH
    _CRITICAL_RATE = 50.0   # events/sec → CRITICAL
    _HIGH_MEM      = 70.0   # % heap used → HIGH
    _CRIT_MEM      = 90.0   # % heap used → CRITICAL
    _HIGH_PROC     = 8      # processes   → HIGH
    _CRIT_PROC     = 16     # processes   → CRITICAL

    # Cooldown between decisions of the same action type
    _COOLDOWN_SEC  = 15.0

    def __init__(
        self,
        on_decision: Optional[Callable[[TuningDecision], None]] = None,
        decision_interval_sec: float = 5.0,
    ) -> None:
        self._on_decision       = on_decision
        self._interval          = decision_interval_sec
        self._lock              = threading.Lock()

        # Live metrics
        self._event_rate        = _RateWindow(window_sec=10.0)
        self._process_count     = 0
        self._exception_count   = 0
        self._anomaly_count     = 0
        self._mem_used_bytes    = 0
        self._heap_total_bytes  = 2 * 1024 * 1024  # default 2 MB

        # Numeric metrics pushed via update_metric()
        self._metrics: Dict[str, float] = {}

        # History
        self._decisions: List[TuningDecision] = []
        self._last_action_time: Dict[str, float] = {}

        # Background timer
        self._timer: Optional[threading.Timer] = None
        if decision_interval_sec > 0:
            self._schedule_next()

    # ---- Feed API ----

    def feed_event(self, event_type: str, data: str) -> None:
        """Record a kernel event (EVT frame payload)."""
        with self._lock:
            self._event_rate.record()
            if event_type in ("EXCP", "EXCEPTION", "FAULT"):
                self._exception_count += 1
            elif event_type == "ANOMALY":
                self._anomaly_count += 1
            elif event_type == "PROC_NEW":
                self._process_count += 1
            elif event_type == "PROC_EXIT":
                self._process_count = max(0, self._process_count - 1)

    def update_metric(self, key: str, value: float) -> None:
        """Update a named numeric metric (from STAT kernel events)."""
        with self._lock:
            self._metrics[key] = value
            if key == "process_count":
                self._process_count = int(value)
            elif key == "mem_used":
                self._mem_used_bytes = int(value)
            elif key == "heap_total":
                self._heap_total_bytes = max(1, int(value))

    # ---- Load assessment ----

    def assess_load(self) -> LoadLevel:
        """Determine the current system load level."""
        with self._lock:
            rate    = self._event_rate.rate
            procs   = self._process_count
            mem_pct = (self._mem_used_bytes / self._heap_total_bytes * 100.0
                       if self._heap_total_bytes else 0.0)

        # Critical if ANY metric is critical
        if rate >= self._CRITICAL_RATE or procs >= self._CRIT_PROC or mem_pct >= self._CRIT_MEM:
            return LoadLevel.CRITICAL

        # High if ANY metric is high
        if rate >= self._HIGH_RATE or procs >= self._HIGH_PROC or mem_pct >= self._HIGH_MEM:
            return LoadLevel.HIGH

        # Low if everything is very quiet
        if rate < 1.0 and procs <= 1 and mem_pct < 10.0:
            return LoadLevel.LOW

        return LoadLevel.NORMAL

    # ---- Decision engine ----

    def compute_decision(self) -> Optional[TuningDecision]:
        """
        Analyse current load and return a TuningDecision if action is
        needed, respecting per-action cooldowns.
        """
        load = self.assess_load()
        decision: Optional[TuningDecision] = None

        with self._lock:
            rate    = self._event_rate.rate
            procs   = self._process_count
            mem_pct = (self._mem_used_bytes / self._heap_total_bytes * 100.0
                       if self._heap_total_bytes else 0.0)
            exceptions = self._exception_count

        now = time.monotonic()

        def _cooldown_ok(action: str) -> bool:
            return now - self._last_action_time.get(action, 0) >= self._COOLDOWN_SEC

        if load == LoadLevel.CRITICAL:
            if _cooldown_ok("THROTTLE") and procs >= self._CRIT_PROC:
                decision = TuningDecision(
                    action="THROTTLE", target="scheduler",
                    value=float(procs),
                    reason=f"Critical load: {procs} processes active",
                    load=load,
                )
            elif _cooldown_ok("SCHED_QUANTUM"):
                # Widen quantum to reduce context-switch overhead
                decision = TuningDecision(
                    action="SCHED_QUANTUM", target="scheduler",
                    value=20.0,
                    reason=f"Critical event rate ({rate:.1f}/s) - widening quantum",
                    load=load,
                )

        elif load == LoadLevel.HIGH:
            if _cooldown_ok("SCHED_QUANTUM"):
                decision = TuningDecision(
                    action="SCHED_QUANTUM", target="scheduler",
                    value=15.0,
                    reason=f"High load ({rate:.1f} evt/s, {procs} procs) - tuning quantum",
                    load=load,
                )
            elif _cooldown_ok("MEM_PRESSURE") and mem_pct >= self._HIGH_MEM:
                decision = TuningDecision(
                    action="MEM_PRESSURE", target="memory",
                    value=mem_pct,
                    reason=f"Memory at {mem_pct:.1f}% - suggest process cleanup",
                    load=load,
                )

        elif load == LoadLevel.LOW:
            if _cooldown_ok("SCHED_QUANTUM"):
                # Narrow quantum for better interactivity when idle
                decision = TuningDecision(
                    action="SCHED_QUANTUM", target="scheduler",
                    value=5.0,
                    reason="Low load - narrowing quantum for responsiveness",
                    load=load,
                )

        elif load == LoadLevel.NORMAL:
            if _cooldown_ok("SCHED_QUANTUM") and exceptions > 0:
                decision = TuningDecision(
                    action="SCHED_PRIO", target="scheduler",
                    value=10.0,
                    reason=f"Normal load but {exceptions} exceptions - restore default priority",
                    load=load,
                )

        if decision:
            self._last_action_time[decision.action] = now
            self._decisions.append(decision)
            if self._on_decision:
                self._on_decision(decision)

        return decision

    # ---- History & reporting ----

    def recent_decisions(self, n: int = 10) -> List[TuningDecision]:
        return list(self._decisions[-n:])

    def summary(self) -> str:
        load = self.assess_load()
        with self._lock:
            rate  = self._event_rate.rate
            procs = self._process_count
            mem_pct = (self._mem_used_bytes / self._heap_total_bytes * 100.0
                       if self._heap_total_bytes else 0.0)
        return (
            f"Load={_LOAD_LABELS[load]} | "
            f"Events={rate:.1f}/s | "
            f"Procs={procs} | "
            f"Mem={mem_pct:.1f}% | "
            f"Decisions={len(self._decisions)}"
        )

    # ---- Background timer ----

    def _schedule_next(self) -> None:
        self._timer = threading.Timer(self._interval, self._tick)
        self._timer.daemon = True
        self._timer.start()

    def _tick(self) -> None:
        try:
            self.compute_decision()
        finally:
            self._schedule_next()

    def stop(self) -> None:
        if self._timer:
            self._timer.cancel()
            self._timer = None
