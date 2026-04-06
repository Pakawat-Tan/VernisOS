"""
config_loader.py — VernisOS Anomaly Rule Config Loader (Phase 10)

Loads anomaly detection rules from YAML config files
and can update an AnomalyDetector at runtime.
"""

from __future__ import annotations

import os
from dataclasses import dataclass
from pathlib import Path
from typing import Any, Optional

from ai.anomaly_detector import (
    AnomalyDetector,
    RateRule,
    PatternRule,
    ThresholdRule,
    RateDetector,
    PatternDetector,
    ThresholdDetector,
    Severity,
)

# Default config path relative to project root
DEFAULT_CONFIG = os.path.join(os.path.dirname(__file__), "config", "anomaly_rules.yaml")

SEVERITY_MAP = {
    "LOW":      Severity.LOW,
    "MEDIUM":   Severity.MEDIUM,
    "HIGH":     Severity.HIGH,
    "CRITICAL": Severity.CRITICAL,
}


@dataclass
class AnomalyConfig:
    """Parsed anomaly configuration."""
    rate_rules:      list[RateRule]
    pattern_rules:   list[PatternRule]
    threshold_rules: list[ThresholdRule]


def _parse_severity(val: str) -> Severity:
    s = val.upper().strip()
    if s not in SEVERITY_MAP:
        raise ValueError(f"Unknown severity '{val}', expected: {list(SEVERITY_MAP)}")
    return SEVERITY_MAP[s]


def _parse_rate_rules(raw: list[dict]) -> list[RateRule]:
    rules = []
    for item in raw:
        rules.append(RateRule(
            event_type=str(item["event_type"]),
            max_count=int(item["max_count"]),
            window_sec=float(item["window_sec"]),
            severity=_parse_severity(item.get("severity", "MEDIUM")),
            title=str(item.get("title", "")),
        ))
    return rules


def _parse_pattern_rules(raw: list[dict]) -> list[PatternRule]:
    rules = []
    for item in raw:
        rules.append(PatternRule(
            name=str(item["name"]),
            sequence=list(item["sequence"]),
            window_sec=float(item["window_sec"]),
            severity=_parse_severity(item.get("severity", "HIGH")),
        ))
    return rules


def _parse_threshold_rules(raw: list[dict]) -> list[ThresholdRule]:
    rules = []
    for item in raw:
        rules.append(ThresholdRule(
            metric=str(item["metric"]),
            max_val=float(item["max_val"]),
            severity=_parse_severity(item.get("severity", "MEDIUM")),
            title=str(item.get("title", "")),
        ))
    return rules


def load_config(path: Optional[str] = None) -> AnomalyConfig:
    """
    Load anomaly rules from a YAML file.

    Falls back to built-in defaults if:
      - path is None and default config doesn't exist
      - YAML parsing fails (logs warning, uses defaults)

    Requires PyYAML (pyyaml). Uses safe_load for security.
    """
    config_path = path or DEFAULT_CONFIG

    if not os.path.exists(config_path):
        return _builtin_defaults()

    try:
        import yaml
    except ImportError:
        # No PyYAML available — use built-in defaults
        return _builtin_defaults()

    try:
        with open(config_path, "r") as f:
            data = yaml.safe_load(f)
    except Exception:
        return _builtin_defaults()

    if not isinstance(data, dict):
        return _builtin_defaults()

    return AnomalyConfig(
        rate_rules=_parse_rate_rules(data.get("rate_rules", [])),
        pattern_rules=_parse_pattern_rules(data.get("pattern_rules", [])),
        threshold_rules=_parse_threshold_rules(data.get("threshold_rules", [])),
    )


def apply_config(detector: AnomalyDetector, config: AnomalyConfig) -> None:
    """
    Replace an AnomalyDetector's rules with those from config.

    This rebuilds the internal sub-detectors while preserving
    the anomaly history and callback.
    """
    cb = detector._handle_anomaly

    detector._rate = RateDetector(rules=config.rate_rules, on_anomaly=cb)
    detector._pattern = PatternDetector(rules=config.pattern_rules, on_anomaly=cb)
    detector._threshold = ThresholdDetector(rules=config.threshold_rules, on_anomaly=cb)


def _builtin_defaults() -> AnomalyConfig:
    """Return the same defaults that AnomalyDetector.__init__ uses."""
    return AnomalyConfig(
        rate_rules=[
            RateRule("EXCP", max_count=5, window_sec=10.0,
                     severity=Severity.HIGH, title="Exception storm detected"),
            RateRule("PROC", max_count=20, window_sec=5.0,
                     severity=Severity.MEDIUM, title="Rapid process creation (possible fork bomb)"),
            RateRule("MOD", max_count=10, window_sec=30.0,
                     severity=Severity.MEDIUM, title="Module load storm"),
            RateRule("FAIL", max_count=3, window_sec=10.0,
                     severity=Severity.HIGH, title="Repeated failures detected"),
        ],
        pattern_rules=[
            PatternRule(name="exception-storm", sequence=["EXCP", "EXCP", "EXCP"],
                        window_sec=3.0, severity=Severity.CRITICAL),
            PatternRule(name="module-crash", sequence=["MOD", "EXCP"],
                        window_sec=2.0, severity=Severity.HIGH),
            PatternRule(name="repeated-privilege-fail", sequence=["DENY", "DENY", "DENY"],
                        window_sec=5.0, severity=Severity.HIGH),
        ],
        threshold_rules=[
            ThresholdRule("proc_count", max_val=32, severity=Severity.MEDIUM),
            ThresholdRule("module_count", max_val=8, severity=Severity.LOW),
            ThresholdRule("ipc_queue_len", max_val=100, severity=Severity.MEDIUM),
        ],
    )
