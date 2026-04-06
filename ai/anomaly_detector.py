"""
anomaly_detector.py — VernisOS Anomaly Detection Engine (Phase 10)

Detects behavioral anomalies from kernel event streams using:
  - RateDetector   : too many events per time window
  - PatternDetector: suspicious event sequences
  - ThresholdDetector: single-value threshold crossings
"""

from __future__ import annotations

import time
from collections import deque
from dataclasses import dataclass, field
from enum import Enum
from typing import Callable, Optional


# =============================================================================
# Severity
# =============================================================================

class Severity(Enum):
    LOW      = "LOW"
    MEDIUM   = "MEDIUM"
    HIGH     = "HIGH"
    CRITICAL = "CRITICAL"


# =============================================================================
# Anomaly Event
# =============================================================================

@dataclass
class Anomaly:
    detector:   str
    severity:   Severity
    title:      str
    detail:     str
    timestamp:  float = field(default_factory=time.monotonic)
    source:     str   = ""

    def __str__(self) -> str:
        age = time.monotonic() - self.timestamp
        return (
            f"[{self.severity.value}] {self.title} "
            f"(via {self.detector}, {age:.1f}s ago)"
            f"\n  {self.detail}"
        )


# =============================================================================
# Base Detector
# =============================================================================

class BaseDetector:
    name: str = "base"

    def __init__(self, on_anomaly: Callable[[Anomaly], None]) -> None:
        self._cb = on_anomaly

    def _emit(self, severity: Severity, title: str, detail: str,
              source: str = "") -> None:
        self._cb(Anomaly(self.name, severity, title, detail, source=source))

    def feed(self, event_type: str, data: str) -> None:
        raise NotImplementedError


# =============================================================================
# Rate Detector — too many events per window
# =============================================================================

@dataclass
class RateRule:
    event_type: str
    max_count:  int
    window_sec: float
    severity:   Severity = Severity.MEDIUM
    title:      str      = ""

    def make_title(self) -> str:
        return self.title or f"Rate limit exceeded: {self.event_type}"


class RateDetector(BaseDetector):
    """Alert when more than max_count events of a type occur within window_sec."""

    name = "rate"

    def __init__(self, rules: list[RateRule],
                 on_anomaly: Callable[[Anomaly], None]) -> None:
        super().__init__(on_anomaly)
        self._rules = rules
        # deque of (timestamp, data) per event_type
        self._windows: dict[str, deque] = {r.event_type: deque() for r in rules}

    def feed(self, event_type: str, data: str) -> None:
        if event_type not in self._windows:
            return
        now = time.monotonic()
        q   = self._windows[event_type]
        q.append((now, data))

        for rule in self._rules:
            if rule.event_type != event_type:
                continue
            cutoff = now - rule.window_sec
            while q and q[0][0] < cutoff:
                q.popleft()
            if len(q) > rule.max_count:
                self._emit(
                    rule.severity,
                    rule.make_title(),
                    (
                        f"{len(q)} '{event_type}' events in {rule.window_sec}s "
                        f"(max {rule.max_count}). Last: {data}"
                    ),
                )


# =============================================================================
# Pattern Detector — suspicious event sequences
# =============================================================================

@dataclass
class PatternRule:
    name:     str
    sequence: list[str]   # ordered list of event_types to match
    window_sec: float
    severity: Severity = Severity.HIGH

    @property
    def title(self) -> str:
        return f"Suspicious sequence: {self.name}"


class PatternDetector(BaseDetector):
    """Alert when a specific sequence of events occurs within a time window."""

    name = "pattern"

    def __init__(self, rules: list[PatternRule],
                 on_anomaly: Callable[[Anomaly], None]) -> None:
        super().__init__(on_anomaly)
        self._rules = rules
        # ring buffer of (timestamp, event_type, data)
        self._history: deque = deque(maxlen=64)

    def feed(self, event_type: str, data: str) -> None:
        now = time.monotonic()
        self._history.append((now, event_type, data))

        for rule in self._rules:
            self._check_pattern(rule, now)

    def _check_pattern(self, rule: PatternRule, now: float) -> None:
        seq     = rule.sequence
        cutoff  = now - rule.window_sec
        # Scan history backwards to find the last element of the sequence
        recent  = [(t, e, d) for t, e, d in self._history if t >= cutoff]
        matched: list[str] = []
        needed  = list(seq)

        for _, ev, data in recent:
            if needed and ev == needed[0]:
                matched.append(f"{ev}:{data[:20]}")
                needed.pop(0)
                if not needed:
                    self._emit(
                        rule.severity,
                        rule.title,
                        f"Pattern matched within {rule.window_sec}s: "
                        + " → ".join(matched),
                    )
                    break


# =============================================================================
# Threshold Detector — single numeric values
# =============================================================================

@dataclass
class ThresholdRule:
    metric:   str
    max_val:  float
    severity: Severity = Severity.MEDIUM
    title:    str      = ""


class ThresholdDetector(BaseDetector):
    """Alert when a reported numeric metric exceeds a threshold."""

    name = "threshold"

    def __init__(self, rules: list[ThresholdRule],
                 on_anomaly: Callable[[Anomaly], None]) -> None:
        super().__init__(on_anomaly)
        self._rules = {r.metric: r for r in rules}

    def update(self, metric: str, value: float, source: str = "") -> None:
        """Call this when the kernel reports a numeric metric."""
        if metric not in self._rules:
            return
        rule = self._rules[metric]
        if value > rule.max_val:
            title = rule.title or f"Metric '{metric}' exceeded threshold"
            self._emit(
                rule.severity,
                title,
                f"Reported {value:.2f} > threshold {rule.max_val:.2f}",
                source=source,
            )

    def feed(self, event_type: str, data: str) -> None:
        # Parse events like "METRIC|<name>|<value>"
        if event_type != "METRIC":
            return
        parts = data.split("|", 1)
        if len(parts) == 2:
            try:
                self.update(parts[0], float(parts[1]))
            except ValueError:
                pass


# =============================================================================
# Composite Detector — wraps all detectors
# =============================================================================

class AnomalyDetector:
    """
    Aggregates multiple detectors and maintains an anomaly history.

    Default rules (Phase 10):
      - Rate: >5 EXCP in 10s → HIGH
      - Rate: >20 PROC in 5s  → MEDIUM (fork bomb protection)
      - Rate: >10 MOD in 30s  → MEDIUM (module storm)
      - Pattern: EXCP → EXCP → EXCP within 3s → CRITICAL (exception storm)
      - Pattern: MOD → EXCP within 2s → HIGH (bad module)
    """

    MAX_HISTORY = 200

    def __init__(self, on_anomaly: Optional[Callable[[Anomaly], None]] = None) -> None:
        self._external_cb = on_anomaly
        self._history: deque[Anomaly] = deque(maxlen=self.MAX_HISTORY)

        cb = self._handle_anomaly

        self._rate = RateDetector(
            rules=[
                RateRule("EXCP",  max_count=5,  window_sec=10.0,
                         severity=Severity.HIGH,
                         title="Exception storm detected"),
                RateRule("PROC",  max_count=20, window_sec=5.0,
                         severity=Severity.MEDIUM,
                         title="Rapid process creation (possible fork bomb)"),
                RateRule("MOD",   max_count=10, window_sec=30.0,
                         severity=Severity.MEDIUM,
                         title="Module load storm"),
                RateRule("FAIL",  max_count=3,  window_sec=10.0,
                         severity=Severity.HIGH,
                         title="Repeated failures detected"),
            ],
            on_anomaly=cb,
        )

        self._pattern = PatternDetector(
            rules=[
                PatternRule(
                    name="exception-storm",
                    sequence=["EXCP", "EXCP", "EXCP"],
                    window_sec=3.0,
                    severity=Severity.CRITICAL,
                ),
                PatternRule(
                    name="module-crash",
                    sequence=["MOD", "EXCP"],
                    window_sec=2.0,
                    severity=Severity.HIGH,
                ),
                PatternRule(
                    name="repeated-privilege-fail",
                    sequence=["DENY", "DENY", "DENY"],
                    window_sec=5.0,
                    severity=Severity.HIGH,
                ),
            ],
            on_anomaly=cb,
        )

        self._threshold = ThresholdDetector(
            rules=[
                ThresholdRule("proc_count",    max_val=32,   severity=Severity.MEDIUM),
                ThresholdRule("module_count",  max_val=8,    severity=Severity.LOW),
                ThresholdRule("ipc_queue_len", max_val=100,  severity=Severity.MEDIUM),
            ],
            on_anomaly=cb,
        )

    def _handle_anomaly(self, anomaly: Anomaly) -> None:
        self._history.append(anomaly)
        if self._external_cb:
            self._external_cb(anomaly)

    def feed_event(self, event_type: str, data: str) -> None:
        """Feed a kernel event to all detectors."""
        self._rate.feed(event_type, data)
        self._pattern.feed(event_type, data)
        self._threshold.feed(event_type, data)

    def update_metric(self, metric: str, value: float, source: str = "") -> None:
        """Update a numeric metric for threshold detection."""
        self._threshold.update(metric, value, source)

    def recent_anomalies(self, last_n: int = 10) -> list[Anomaly]:
        return list(self._history)[-last_n:]

    def anomaly_count(self, severity: Optional[Severity] = None) -> int:
        if severity is None:
            return len(self._history)
        return sum(1 for a in self._history if a.severity == severity)

    def summary(self) -> str:
        total    = len(self._history)
        critical = self.anomaly_count(Severity.CRITICAL)
        high     = self.anomaly_count(Severity.HIGH)
        medium   = self.anomaly_count(Severity.MEDIUM)
        low      = self.anomaly_count(Severity.LOW)
        return (
            f"Anomalies: {total} total — "
            f"CRITICAL:{critical} HIGH:{high} MEDIUM:{medium} LOW:{low}"
        )