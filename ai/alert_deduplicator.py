"""
alert_deduplicator.py — VernisOS Alert Deduplication (Phase 10)

Prevents alert spam by rate-limiting identical or similar alerts
within a configurable suppression window.
"""

from __future__ import annotations

import time
from typing import TYPE_CHECKING

if TYPE_CHECKING:
    from ai.anomaly_detector import Anomaly


class AlertDeduplicator:
    """
    Suppress duplicate alerts within a time window.

    Key = detector:title (e.g., "rate:Exception storm detected")
    If the same key fires again within suppress_window_sec,
    it is silently dropped and counted.
    """

    def __init__(self, suppress_window_sec: float = 30.0) -> None:
        self._window = suppress_window_sec
        self._last_sent: dict[str, float]  = {}
        self._suppressed: dict[str, int]   = {}
        self._total_suppressed: int        = 0

    def should_alert(self, anomaly: "Anomaly") -> bool:
        """Return True if this anomaly should be forwarded as an alert."""
        key = f"{anomaly.detector}:{anomaly.title}"
        now = time.monotonic()

        if key in self._last_sent:
            elapsed = now - self._last_sent[key]
            if elapsed < self._window:
                self._suppressed[key] = self._suppressed.get(key, 0) + 1
                self._total_suppressed += 1
                return False

        self._last_sent[key] = now
        return True

    def suppressed_count(self, key: str = "") -> int:
        """Return number of suppressed alerts (for a key, or total)."""
        if key:
            return self._suppressed.get(key, 0)
        return self._total_suppressed

    def reset(self) -> None:
        """Clear all suppression state."""
        self._last_sent.clear()
        self._suppressed.clear()
        self._total_suppressed = 0

    def summary(self) -> str:
        if self._total_suppressed == 0:
            return "Alert dedup: no duplicates suppressed."
        top_keys = sorted(self._suppressed.items(), key=lambda x: x[1], reverse=True)[:5]
        lines = [f"Alert dedup: {self._total_suppressed} total suppressed"]
        for k, cnt in top_keys:
            lines.append(f"  {k}: {cnt}x suppressed")
        return "\n".join(lines)
