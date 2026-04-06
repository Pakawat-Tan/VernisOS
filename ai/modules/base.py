"""
modules/base.py — AIModule base class (Phase 9)
"""

from __future__ import annotations
from abc import ABC, abstractmethod
from typing import TYPE_CHECKING

if TYPE_CHECKING:
    from ai.corelib import EngineContext


class AIModule(ABC):
    """
    Base class for all AI analysis modules.

    Each module handles a specific topic domain (e.g. system monitoring,
    memory analysis, security auditing). The dispatcher tries modules in
    priority order — highest priority first.
    """

    #: Lower number = higher priority (checked first)
    priority: int = 100

    def __init__(self, ctx: "EngineContext") -> None:
        self.ctx = ctx

    @abstractmethod
    def can_handle(self, query: str) -> bool:
        """Return True if this module can respond to the query."""
        ...

    @abstractmethod
    def process(self, query: str) -> str:
        """Process the query and return a response string."""
        ...

    def on_event(self, event_type: str, data: str) -> None:
        """Handle a kernel event notification (optional override)."""
        pass

    @property
    def name(self) -> str:
        return self.__class__.__name__
