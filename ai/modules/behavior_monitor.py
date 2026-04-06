"""
modules/behavior_monitor.py — AI Behavior Monitor Module (Phase 10)

Connects the AnomalyDetector, ProcessTracker, EventStore,
ResponseHandler, and AlertDeduplicator to the AI module system.

Handles queries about anomalies, alerts, processes, and system behavior.
"""

from __future__ import annotations

import re
from typing import TYPE_CHECKING, Optional
from .base import AIModule
from ai.anomaly_detector import AnomalyDetector, Anomaly, Severity
from ai.process_tracker import ProcessTracker
from ai.event_store import EventStore
from ai.alert_deduplicator import AlertDeduplicator
from ai.response_handler import ResponseHandler

if TYPE_CHECKING:
    from ai.corelib import EngineContext


class BehaviorMonitorModule(AIModule):
    """
    Receives all kernel events, feeds them to AnomalyDetector
    and ProcessTracker, records in EventStore, sends deduplicated
    alerts back to kernel, and triggers remediation when needed.
    """

    priority = 5   # high priority — checked before SystemMonitor

    TOPICS = re.compile(
        r"anomal|alert|monitor|behavior|behav|detect|threat|security|"
        r"crash|exception|process.*status|module.*status|health.*detail|"
        r"pid|trust|suspicious|remediat|event.*log|event.*hist",
        re.IGNORECASE,
    )

    def __init__(self, ctx: "EngineContext",
                 config: Optional[dict] = None) -> None:
        super().__init__(ctx)
        self._detector    = AnomalyDetector(on_anomaly=self._on_anomaly)
        self._tracker     = ProcessTracker()
        self._events      = EventStore()
        self._dedup       = AlertDeduplicator(suppress_window_sec=30.0)
        self._responder   = ResponseHandler(ctx, enabled=True)
        self._alert_count = 0

        # Load config if provided
        if config:
            self._apply_config(config)

    def _apply_config(self, config: dict) -> None:
        """Apply external config dict to tune detector rules."""
        from ai.config_loader import load_config, apply_config
        cfg = load_config(config.get("rules_path"))
        apply_config(self._detector, cfg)

    # ---- Properties for external access ----

    @property
    def tracker(self) -> ProcessTracker:
        return self._tracker

    @property
    def event_store(self) -> EventStore:
        return self._events

    @property
    def responder(self) -> ResponseHandler:
        return self._responder

    # ---- Internal callback (called by AnomalyDetector) ----

    def _on_anomaly(self, anomaly: Anomaly) -> None:
        self._alert_count += 1
        self.ctx.logger.warning(
            "monitor",
            f"ANOMALY [{anomaly.severity.value}] {anomaly.title}: {anomaly.detail}",
        )

        # Deduplicate before sending alert to kernel
        if not self._dedup.should_alert(anomaly):
            return

        # Send alert to kernel
        from ai.corelib import MessageFrame
        alert_msg = f"ALERT|{anomaly.severity.value}|{anomaly.title}|{anomaly.detail}"
        self.ctx.conn.send_frame(MessageFrame(msg_type="CMD", seq=0, payload=alert_msg))
        self.ctx.record_stat("last_anomaly", str(anomaly))

        # Trigger remediation if warranted
        source_pid = self._extract_pid_from_anomaly(anomaly)
        self._responder.handle_anomaly(anomaly, source_pid=source_pid)

    # ---- AIModule interface ----

    def can_handle(self, query: str) -> bool:
        return bool(self.TOPICS.search(query))

    def process(self, query: str) -> str:
        q = query.lower()

        if any(w in q for w in ("summary", "count", "how many", "total")):
            return self._detector.summary()

        if any(w in q for w in ("recent", "last", "latest", "list")):
            return self._recent_report()

        if any(w in q for w in ("critical", "crit")):
            return self._by_severity(Severity.CRITICAL)

        if any(w in q for w in ("high",)):
            return self._by_severity(Severity.HIGH)

        if any(w in q for w in ("clear", "reset")):
            return "[DENIED] Anomaly history cannot be cleared through AI query."

        # Process tracker queries
        if "pid" in q:
            return self._pid_query(q)

        if any(w in q for w in ("suspicious", "untrust", "trust")):
            return self._tracker.status_report()

        if any(w in q for w in ("process", "proc")):
            return self._tracker.status_report()

        # Event store queries
        if any(w in q for w in ("event log", "event hist", "events")):
            return self._events.summary()

        if any(w in q for w in ("remediat", "response", "action")):
            return self._responder.status_report()

        # Default: full status
        return self._full_status()

    def on_event(self, event_type: str, data: str) -> None:
        """Feed every kernel event into detector, tracker, and event store."""
        # Record in event store
        source_pid = EventStore.extract_pid(event_type, data)
        self._events.record(event_type, data, source_pid=source_pid)

        # Feed anomaly detector
        self._detector.feed_event(event_type, data)

        # Feed process tracker
        if event_type == "PROC":
            prof = self._tracker.on_proc_event(data)
            if prof and prof.trust.value in ("suspicious", "untrusted"):
                self._tracker.flag_anomaly(prof.pid, f"trust_{prof.trust.value}")
        elif event_type == "EXCP":
            self._tracker.on_exception(data)
        elif event_type == "DENY":
            self._tracker.on_denial(data)
        elif event_type == "FAIL":
            self._tracker.on_failure(data)
        elif event_type == "SYSCALL":
            self._tracker.on_syscall(data)

        # Update numeric metrics from STAT events
        if event_type == "STAT":
            parts = data.split("|", 1)
            if len(parts) == 2:
                try:
                    self._detector.update_metric(parts[0], float(parts[1]))
                except ValueError:
                    pass

    # ---- Internal reports ----

    def _full_status(self) -> str:
        lines = []
        # Anomaly summary
        lines.append(self._detector.summary())
        # Process summary
        suspicious = self._tracker.suspicious_processes()
        lines.append(
            f"Processes: {self._tracker.active_count} active, "
            f"{len(suspicious)} suspicious"
        )
        # Event summary
        lines.append(self._events.summary(minutes=5))
        # Dedup summary
        suppressed = self._dedup.suppressed_count()
        if suppressed:
            lines.append(f"Alerts suppressed (dedup): {suppressed}")
        # Remediation summary
        actions = self._responder.recent_actions(3)
        if actions:
            lines.append(f"Recent remediations: {len(actions)}")

        recent = self._detector.recent_anomalies(3)
        if recent:
            lines.append("Recent anomalies:")
            for a in recent:
                lines.append(f"  [{a.severity.value}] {a.title} — {a.detail[:60]}")
        return "\n".join(lines)

    def _recent_report(self) -> str:
        anomalies = self._detector.recent_anomalies(5)
        if not anomalies:
            return "No anomalies in recent history."
        lines = [f"Last {len(anomalies)} anomalies:"]
        for a in anomalies:
            lines.append(f"  [{a.severity.value}] {a.title}")
            lines.append(f"    {a.detail[:80]}")
        return "\n".join(lines)

    def _by_severity(self, severity: Severity) -> str:
        all_a = self._detector.recent_anomalies(50)
        filtered = [a for a in all_a if a.severity == severity]
        if not filtered:
            return f"No {severity.value} anomalies detected."
        lines = [f"{severity.value} anomalies ({len(filtered)}):"]
        for a in filtered[-5:]:
            lines.append(f"  {a.title}: {a.detail[:60]}")
        return "\n".join(lines)

    def _pid_query(self, query: str) -> str:
        """Extract PID from query and return process detail."""
        import re as _re
        match = _re.search(r'\bpid\s*(\d+)\b', query, _re.IGNORECASE)
        if not match:
            match = _re.search(r'\b(\d+)\b', query)
        if match:
            pid = int(match.group(1))
            detail = self._tracker.process_detail(pid)
            events = self._events.query_by_pid(pid, minutes=10)
            if events:
                detail += f"\n  Event store: {len(events)} events in last 10min"
            return detail
        return "Specify a PID number, e.g., 'PID 15 status'"

    @staticmethod
    def _extract_pid_from_anomaly(anomaly: Anomaly) -> int:
        """Try to extract a PID from anomaly detail text."""
        import re as _re
        match = _re.search(r'\bPID[= ]?(\d+)\b', anomaly.detail, _re.IGNORECASE)
        if match:
            return int(match.group(1))
        match = _re.search(r"'(\d+)'", anomaly.detail)
        if match:
            return int(match.group(1))
        return 0