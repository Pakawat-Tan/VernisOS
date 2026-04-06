#!/usr/bin/env python3
"""
policy_compile.py — Compile VernisOS policy YAML to VPOL binary blob.

Usage:
    python3 ai/tools/policy_compile.py ai/config/policy.yaml -o policy.bin

Binary format (little-endian):
    Header (8 bytes):
        [0..4]  magic: "VPOL"
        [4..6]  version: u16
        [6..8]  section_count: u16

    Per section (8-byte header + data):
        [0]     section_type: u8
        [1]     _pad: u8
        [2..4]  entry_count: u16
        [4..8]  data_size: u32
        [8..]   entries

Section types and entry sizes:
    1 = RateRule       (20 bytes)
    2 = PatternRule    (48 bytes)
    3 = ThresholdRule  (28 bytes)
    4 = TunerConfig    (32 bytes, 1 entry)
    5 = RemediationRule(16 bytes)
    6 = DedupConfig    (8 bytes, 1 entry)
    7 = TrustConfig    (24 bytes, 1 entry)
    8 = AccessRule     (36 bytes)
"""

import struct
import sys
import os

# Try importing yaml
try:
    import yaml
except ImportError:
    try:
        from ruamel.yaml import YAML
        _ruamel = True
    except ImportError:
        print("ERROR: PyYAML or ruamel.yaml required. Install with: pip install pyyaml", file=sys.stderr)
        sys.exit(1)
    else:
        _ruamel = False
else:
    _ruamel = False

SEVERITY_MAP = {"LOW": 0, "MEDIUM": 1, "HIGH": 2, "CRITICAL": 3}
ACTION_MAP = {"kill": 0, "throttle": 1, "log": 2, "revoke": 3, "suspend": 4}

# Ticks per second (100 Hz PIT)
TICKS_PER_SEC = 100


def load_yaml(path):
    """Load YAML file."""
    with open(path, "r") as f:
        if _ruamel:
            y = YAML()
            return y.load(f)
        else:
            return yaml.safe_load(f)


def pad_bytes(s, size):
    """Encode string to bytes, truncate/pad to fixed size."""
    b = s.encode("ascii", errors="replace")[:size]
    return b.ljust(size, b"\x00")


def sec_to_ticks(sec):
    """Convert seconds (float) to kernel ticks (u64)."""
    return int(float(sec) * TICKS_PER_SEC)


def encode_rate_rules(rules):
    """Encode rate_rules section. Each entry = 20 bytes."""
    data = b""
    for r in rules:
        evt = pad_bytes(r["event_type"], 4)
        max_count = int(r["max_count"])
        window = sec_to_ticks(r["window_sec"])
        sev = SEVERITY_MAP.get(r["severity"].upper(), 0)
        data += struct.pack("<4sIQ BBB x", evt, max_count, window, sev, 0, 0)
    return 1, len(rules), data


def encode_pattern_rules(rules):
    """Encode pattern_rules section. Each entry = 48 bytes."""
    data = b""
    for r in rules:
        name = pad_bytes(r["name"], 16)
        seq = r["sequence"]
        seq_count = min(len(seq), 5)
        sev = SEVERITY_MAP.get(r["severity"].upper(), 0)
        window = sec_to_ticks(r["window_sec"])

        entry = name                              # [0..16]  name
        entry += struct.pack("<BB xx", seq_count, sev)  # [16..20] seq_count, severity, pad
        for i in range(5):                         # [20..40] sequence (5 × 4 bytes)
            if i < seq_count:
                entry += pad_bytes(seq[i], 4)
            else:
                entry += b"\x00\x00\x00\x00"
        entry += struct.pack("<Q", window)         # [40..48] window_ticks
        data += entry
    return 2, len(rules), data


def encode_threshold_rules(rules):
    """Encode threshold_rules section. Each entry = 28 bytes."""
    data = b""
    for r in rules:
        metric = pad_bytes(r["metric"], 16)
        max_val_x100 = int(float(r["max_val"]) * 100)
        sev = SEVERITY_MAP.get(r["severity"].upper(), 0)
        data += struct.pack("<16s I B xxx", metric, max_val_x100, sev)
    return 3, len(rules), data


def encode_tuner_config(cfg):
    """Encode tuner_config section. Single entry = 32 bytes."""
    data = struct.pack("<II II I xxxx Q",
        int(float(cfg["high_rate"]) * 100),
        int(float(cfg["critical_rate"]) * 100),
        int(cfg["high_proc_count"]),
        int(cfg["critical_proc_count"]),
        int(cfg["default_quantum"]),
        sec_to_ticks(cfg["cooldown_sec"]),
    )
    return 4, 1, data


def encode_remediation_rules(rules):
    """Encode remediation_rules section. Each entry = 16 bytes."""
    data = b""
    for r in rules:
        sev = SEVERITY_MAP.get(r["min_severity"].upper(), 0)
        act = ACTION_MAP.get(r["action"].lower(), 2)
        param = int(r.get("param", 0))
        repeat = int(r.get("repeat_limit", 1))
        cooldown = sec_to_ticks(r["cooldown_sec"])
        data += struct.pack("<BB H I Q", sev, act, param, repeat, cooldown)
    return 5, len(rules), data


def encode_dedup_config(cfg):
    """Encode dedup_config section. Single entry = 8 bytes."""
    data = struct.pack("<Q", sec_to_ticks(cfg["window_sec"]))
    return 6, 1, data


def encode_trust_config(cfg):
    """Encode trust_config section. Single entry = 24 bytes."""
    data = struct.pack("<6I",
        int(cfg["failures_to_suspicious"]),
        int(cfg["failures_to_untrusted"]),
        int(cfg["denials_to_suspicious"]),
        int(cfg["denials_to_untrusted"]),
        int(cfg["anomalies_to_suspicious"]),
        int(cfg["anomalies_to_untrusted"]),
    )
    return 7, 1, data


def encode_access_rules(rules):
    """Encode access_rules section. Each entry = 36 bytes."""
    data = b""
    for r in rules:
        pattern = pad_bytes(r["pattern"], 32)
        priv_level = int(r["min_privilege"])
        data += struct.pack("<32s B xxx", pattern, priv_level)
    return 8, len(rules), data


def compile_policy(yaml_path, output_path):
    """Compile YAML policy to binary blob."""
    cfg = load_yaml(yaml_path)
    version = int(cfg.get("version", 1))

    sections = []

    if "rate_rules" in cfg and cfg["rate_rules"]:
        sections.append(encode_rate_rules(cfg["rate_rules"]))

    if "pattern_rules" in cfg and cfg["pattern_rules"]:
        sections.append(encode_pattern_rules(cfg["pattern_rules"]))

    if "threshold_rules" in cfg and cfg["threshold_rules"]:
        sections.append(encode_threshold_rules(cfg["threshold_rules"]))

    if "tuner" in cfg and cfg["tuner"]:
        sections.append(encode_tuner_config(cfg["tuner"]))

    if "remediation_rules" in cfg and cfg["remediation_rules"]:
        sections.append(encode_remediation_rules(cfg["remediation_rules"]))

    if "dedup" in cfg and cfg["dedup"]:
        sections.append(encode_dedup_config(cfg["dedup"]))

    if "trust" in cfg and cfg["trust"]:
        sections.append(encode_trust_config(cfg["trust"]))

    if "access_rules" in cfg and cfg["access_rules"]:
        sections.append(encode_access_rules(cfg["access_rules"]))

    # Build header
    header = struct.pack("<4s HH", b"VPOL", version, len(sections))

    # Build section data
    body = b""
    for sec_type, entry_count, entries in sections:
        sec_header = struct.pack("<B x H I", sec_type, entry_count, len(entries))
        body += sec_header + entries

    blob = header + body

    with open(output_path, "wb") as f:
        f.write(blob)

    print(f"Compiled {yaml_path} -> {output_path}")
    print(f"  Version: {version}")
    print(f"  Sections: {len(sections)}")
    print(f"  Total size: {len(blob)} bytes")

    # Dump section breakdown
    for sec_type, entry_count, entries in sections:
        names = {1: "rate", 2: "pattern", 3: "threshold", 4: "tuner",
                 5: "remediation", 6: "dedup", 7: "trust", 8: "access"}
        name = names.get(sec_type, f"unknown({sec_type})")
        print(f"    [{name}] {entry_count} entries, {len(entries)} bytes")


def main():
    import argparse
    parser = argparse.ArgumentParser(description="Compile VernisOS policy YAML to VPOL binary")
    parser.add_argument("input", help="Input YAML file")
    parser.add_argument("-o", "--output", default="policy.bin", help="Output binary file")
    args = parser.parse_args()

    if not os.path.exists(args.input):
        print(f"ERROR: {args.input} not found", file=sys.stderr)
        sys.exit(1)

    compile_policy(args.input, args.output)


if __name__ == "__main__":
    main()
