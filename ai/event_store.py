"""
event_store.py — VernisOS Event History Store (Phase 10)

Thread-safe event log with timestamp-based, type-based,
and per-PID query capabilities.
"""

from __future__ import annotations

import threading
import time
from collections import deque
from dataclasses import dataclass, field
from typing import Optional


@dataclass
class Event:
    """A single recorded kernel event."""
    timestamp:  float
    event_type: str
    data:       str
    source_pid: int = 0

    def __str__(self) -> str:
        age = time.monotonic() - self.timestamp
        pid_str = f" PID={self.source_pid}" if self.source_pid else ""
        return f"[{age:.1f}s ago] {self.event_type}{pid_str}: {self.data}"


class EventStore:
    """
    Ring-buffer event store with query API.

    Records kernel events and provides filtered queries by:
      - event type
      - source PID
      - time range
      - combined filters
    """

    DEFAULT_MAX = 2000

    def __init__(self, maxlen: int = DEFAULT_MAX) -> None:
        self._events: deque[Event] = deque(maxlen=maxlen)
        self._lock = threading.Lock()
        self._total_recorded: int = 0

    def record(self, event_type: str, data: str, source_pid: int = 0) -> Event:
        """Record a new event. Thread-safe."""
        ev = Event(
            timestamp=time.monotonic(),
            event_type=event_type,
            data=data,
            source_pid=source_pid,
        )
        with self._lock:
            self._events.append(ev)
            self._total_recorded += 1
        return ev

    @property
    def total_recorded(self) -> int:
        return self._total_recorded

    @property
    def size(self) -> int:
        with self._lock:
            return len(self._events)

    # ---- Query methods ----

    def query_recent(self, last_n: int = 20) -> list[Event]:
        """Return the last N events."""
        with self._lock:
            return list(self._events)[-last_n:]

    def query_by_type(self, event_type: str, minutes: float = 5.0) -> list[Event]:
        """Return all events of a given type within the last N minutes."""
        cutoff = time.monotonic() - (minutes * 60)
        with self._lock:
            return [
                e for e in self._events
                if e.event_type == event_type and e.timestamp >= cutoff
            ]

    def query_by_pid(self, pid: int, minutes: float = 5.0) -> list[Event]:
        """Return all events from a specific PID within the last N minutes."""
        cutoff = time.monotonic() - (minutes * 60)
        with self._lock:
            return [
                e for e in self._events
                if e.source_pid == pid and e.timestamp >= cutoff
            ]

    def query_timerange(self, start_ts: float, end_ts: float) -> list[Event]:
        """Return events between two timestamps (monotonic)."""
        with self._lock:
            return [
                e for e in self._events
                if start_ts <= e.timestamp <= end_ts
            ]

    def query(self, event_type: Optional[str] = None,
              pid: Optional[int] = None,
              minutes: float = 5.0,
              limit: int = 100) -> list[Event]:
        """Combined query with optional type + PID + time filters."""
        cutoff = time.monotonic() - (minutes * 60)
        with self._lock:
            results = []
            for e in reversed(self._events):
                if e.timestamp < cutoff:
                    break
                if event_type and e.event_type != event_type:
                    continue
                if pid is not None and e.source_pid != pid:
                    continue
                results.append(e)
                if len(results) >= limit:
                    break
            results.reverse()
            return results

    def count_by_type(self, minutes: float = 5.0) -> dict[str, int]:
        """Return event counts grouped by type within the last N minutes."""
        cutoff = time.monotonic() - (minutes * 60)
        counts: dict[str, int] = {}
        with self._lock:
            for e in self._events:
                if e.timestamp >= cutoff:
                    counts[e.event_type] = counts.get(e.event_type, 0) + 1
        return counts

    def summary(self, minutes: float = 5.0) -> str:
        """Human-readable summary of recent events."""
        counts = self.count_by_type(minutes)
        total  = sum(counts.values())
        if total == 0:
            return f"Event store: no events in last {minutes:.0f}min (total recorded: {self._total_recorded})"
        parts = ", ".join(f"{k}:{v}" for k, v in sorted(counts.items()))
        return (
            f"Event store: {total} events in last {minutes:.0f}min — {parts} "
            f"(total recorded: {self._total_recorded})"
        )

    # ---- PID extraction helper ----

    @staticmethod
    def extract_pid(event_type: str, data: str) -> int:
        """
        Try to extract a PID from event data based on event type conventions.

        PROC: 'pid|action|name' → pid
        EXCP: 'code|addr|pid'   → pid
        DENY: 'pid|reason'      → pid
        FAIL: 'pid|reason'      → pid
        SYSCALL: 'pid|num'      → pid
        """
        parts = data.split("|")
        try:
            if event_type in ("PROC", "DENY", "FAIL", "SYSCALL"):
                return int(parts[0]) if parts else 0
            if event_type == "EXCP" and len(parts) >= 3:
                return int(parts[2])
        except (ValueError, IndexError):
            pass
        return 0
