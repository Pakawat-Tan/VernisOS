"""
modules/system_monitor.py — System Monitor Module (Phase 9)

Handles queries about: status, process, memory, scheduler, uptime.
Records kernel events and can summarize system health.
"""

from __future__ import annotations

import re
from typing import TYPE_CHECKING
from .base import AIModule

if TYPE_CHECKING:
    from ai.corelib import EngineContext


class SystemMonitorModule(AIModule):
    priority = 10

    TOPICS = re.compile(
        r"status|process|cpu|scheduler|memory|mem|uptime|health|ping|hello|version",
        re.IGNORECASE,
    )

    def __init__(self, ctx: "EngineContext") -> None:
        super().__init__(ctx)
        self._boot_event:    str | None = None
        self._last_exc:      str | None = None
        self._module_events: list[str]  = []
        self._proc_events:   list[str]  = []

    # ---- Module interface ----

    def can_handle(self, query: str) -> bool:
        return bool(self.TOPICS.search(query))

    def process(self, query: str) -> str:
        q = query.lower()

        if any(w in q for w in ("ping", "hello", "hi")):
            return (
                f"VernisOS AI online. "
                f"Uptime: {self.ctx.uptime_seconds()}s. "
                f"Queries processed: {self.ctx.query_count}."
            )

        if "version" in q:
            return "VernisOS AI Engine v0.9 (Phase 9) — corelib + policy + modules active."

        if "uptime" in q:
            u = self.ctx.uptime_seconds()
            return f"AI engine uptime: {u}s ({u // 60}m {u % 60}s)."

        if any(w in q for w in ("status", "health")):
            return self._system_status()

        if any(w in q for w in ("process", "cpu", "scheduler")):
            return self._process_status()

        if any(w in q for w in ("memory", "mem")):
            return self._memory_status()

        return self._system_status()

    def on_event(self, event_type: str, data: str) -> None:
        if event_type == "BOOT":
            self._boot_event = data
            self.ctx.logger.info("monitor", f"Boot event: {data}")

        elif event_type == "EXCP":
            self._last_exc = data
            self.ctx.logger.warning("monitor", f"Kernel exception: {data}")

        elif event_type == "MOD":
            self._module_events.append(data)
            self.ctx.logger.info("monitor", f"Module event: {data}")

        elif event_type == "PROC":
            self._proc_events.append(data)
            self.ctx.logger.info("monitor", f"Process event: {data}")

    # ---- Internal reports ----

    def _system_status(self) -> str:
        parts = [
            f"System: {'running — boot OK' if self._boot_event else 'no boot event received'}",
            f"AI uptime: {self.ctx.uptime_seconds()}s",
            f"Queries: {self.ctx.query_count}  Events: {self.ctx.event_count}",
        ]
        if self._last_exc:
            parts.append(f"Last exception: {self._last_exc}")
        if self._module_events:
            parts.append(f"Module events: {len(self._module_events)}")
        return " | ".join(parts)

    def _process_status(self) -> str:
        if self._proc_events:
            recent = self._proc_events[-3:]
            return f"Process events (last {len(recent)}): " + "; ".join(recent)
        return "Scheduler nominal. No process anomalies reported to AI engine."

    def _memory_status(self) -> str:
        mem = self.ctx.get_stat("memory_usage")
        if mem:
            return f"Memory usage: {mem}"
        return "Memory subsystem OK. No allocation anomalies reported."


class HelpModule(AIModule):
    """Handles 'help' queries — always last resort."""
    priority = 200

    def can_handle(self, query: str) -> bool:
        return "help" in query.lower()

    def process(self, query: str) -> str:
        return (
            "Available query topics: "
            "status, ping, uptime, version, "
            "process, scheduler, memory, "
            "module, security, help"
        )
