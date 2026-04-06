"""
policy_manager.py — VernisOS AI Policy Manager (Phase 9)

Controls what the AI engine is allowed to do based on:
  - Who is asking (privilege level from kernel session)
  - What action is requested (query topic / command type)
  - Current system state

Phase 9: dictionary-based policies (YAML config loader added in Phase 12).
"""

from __future__ import annotations

from dataclasses import dataclass, field
from enum import IntEnum
from typing import Optional
import re


# =============================================================================
# Privilege Levels (mirrors cli.h CliPrivilegeLevel)
# =============================================================================

class PrivilegeLevel(IntEnum):
    ROOT  = 0    # full system access
    ADMIN = 50   # administrative tasks
    USER  = 100  # limited access


# =============================================================================
# Policy Rule
# =============================================================================

@dataclass
class PolicyRule:
    """A single allow/deny rule."""
    action:        str            # topic or action pattern (regex allowed)
    min_privilege: PrivilegeLevel # minimum privilege required
    allowed:       bool = True    # True = allow, False = deny
    description:   str = ""

    def matches(self, action: str) -> bool:
        try:
            return bool(re.fullmatch(self.action, action, re.IGNORECASE))
        except re.error:
            return self.action.lower() == action.lower()


# =============================================================================
# Policy Set
# =============================================================================

@dataclass
class PolicySet:
    """A named collection of rules."""
    name:  str
    rules: list[PolicyRule] = field(default_factory=list)

    def add(self, action: str, min_priv: PrivilegeLevel,
            allowed: bool = True, description: str = "") -> None:
        self.rules.append(PolicyRule(action, min_priv, allowed, description))


# =============================================================================
# Policy Decision
# =============================================================================

@dataclass
class PolicyDecision:
    allowed:    bool
    reason:     str
    rule:       Optional[PolicyRule] = None


# =============================================================================
# Policy Manager
# =============================================================================

class PolicyManager:
    """
    Evaluates AI actions against loaded policy rules.

    Default policy (Phase 9):
      - ROOT   → full access to all topics
      - ADMIN  → read/query access, no destructive commands
      - USER   → status queries only, no system commands
    """

    def __init__(self) -> None:
        self._policies: list[PolicySet] = []
        self._active_policy: Optional[PolicySet] = None
        self._load_default_policy()

    # ---- Default policy ----

    def _load_default_policy(self) -> None:
        p = PolicySet("default")

        # Deny list (checked first — highest priority)
        p.add(r"rm.*|delete.*|format.*|wipe.*",
              PrivilegeLevel.ROOT, allowed=False,
              description="Destructive operations require explicit root override")

        # Root-only
        p.add(r"shutdown|restart|halt|reboot|kernel.*",
              PrivilegeLevel.ROOT, allowed=True,
              description="Power and kernel management — root only")

        p.add(r"module.*|sandbox.*|ipc.*",
              PrivilegeLevel.ROOT, allowed=True,
              description="Subsystem control — root only")

        # Admin+
        p.add(r"process.*|scheduler.*|memory.*|mem.*",
              PrivilegeLevel.ADMIN, allowed=True,
              description="System monitoring — admin+")

        p.add(r"log.*|audit.*|event.*",
              PrivilegeLevel.ADMIN, allowed=True,
              description="Log access — admin+")

        # User+
        p.add(r"status|help|hello|ping|uptime|whoami|version",
              PrivilegeLevel.USER, allowed=True,
              description="Basic status queries — all users")

        p.add(r".*",    # catch-all: allow generic queries for root/admin
              PrivilegeLevel.ADMIN, allowed=True,
              description="Generic queries — admin+")

        self._policies.append(p)
        self._active_policy = p

    # ---- Public API ----

    def load_policy(self, policy: PolicySet) -> None:
        """Replace the active policy set."""
        self._policies.append(policy)
        self._active_policy = policy

    def evaluate(self, action: str, privilege: PrivilegeLevel) -> PolicyDecision:
        """
        Check if `action` is allowed for the given `privilege` level.
        Rules are checked in order; first match wins.
        """
        if self._active_policy is None:
            return PolicyDecision(allowed=False, reason="No policy loaded")

        for rule in self._active_policy.rules:
            if rule.matches(action):
                if not rule.allowed:
                    return PolicyDecision(
                        allowed=False,
                        reason=f"Action '{action}' is explicitly denied: {rule.description}",
                        rule=rule,
                    )
                if privilege > rule.min_privilege:
                    return PolicyDecision(
                        allowed=False,
                        reason=(
                            f"Insufficient privilege for '{action}'. "
                            f"Requires {rule.min_privilege.name}, "
                            f"caller has {privilege.name}"
                        ),
                        rule=rule,
                    )
                return PolicyDecision(
                    allowed=True,
                    reason=f"Allowed by rule: {rule.description}",
                    rule=rule,
                )

        return PolicyDecision(
            allowed=False,
            reason=f"No policy rule matched action '{action}'",
        )

    def is_allowed(self, action: str, privilege: PrivilegeLevel) -> bool:
        return self.evaluate(action, privilege).allowed

    def describe(self) -> list[str]:
        """Return human-readable summary of active policy rules."""
        if not self._active_policy:
            return ["No active policy"]
        lines = [f"Policy: {self._active_policy.name}"]
        for r in self._active_policy.rules:
            tag = "ALLOW" if r.allowed else "DENY "
            lines.append(
                f"  [{tag}] pattern='{r.action}' "
                f"min_priv={r.min_privilege.name:5s}  {r.description}"
            )
        return lines

    # ---- Privilege helpers ----

    @staticmethod
    def privilege_from_kernel(kernel_priv: int) -> PrivilegeLevel:
        """Convert kernel CliPrivilegeLevel integer to PrivilegeLevel."""
        if kernel_priv == 0:
            return PrivilegeLevel.ROOT
        if kernel_priv <= 50:
            return PrivilegeLevel.ADMIN
        return PrivilegeLevel.USER
