// policy.rs — Binary policy blob parser + runtime config distribution
//
// Phase 12: Parses VPOL binary format into PolicyConfig, distributes to AI components.
//
// Binary format (little-endian):
//   Header (8 bytes):
//     [0..4]  magic: "VPOL"
//     [4..6]  version: u16
//     [6..8]  section_count: u16
//
//   Sections (variable):
//     [0]     section_type: u8
//     [1]     _pad: u8
//     [2..4]  entry_count: u16
//     [4..8]  data_size: u32 (bytes of entries that follow)
//     [8..]   entries[entry_count]
//
// Section types:
//   1 = RateRule (20 bytes each)
//   2 = PatternRule (48 bytes each)
//   3 = ThresholdRule (28 bytes each)
//   4 = TunerConfig (32 bytes, single entry)
//   5 = RemediationRule (16 bytes each)
//   6 = DedupConfig (8 bytes, single entry)
//   7 = TrustConfig (24 bytes, single entry)
//   8 = AccessRule (36 bytes each)

use alloc::string::String;
use alloc::vec::Vec;
use super::types::*;

pub const VPOL_MAGIC: &[u8; 4] = b"VPOL";
pub const VPOL_VERSION: u16 = 1;
pub const HEADER_SIZE: usize = 8;
pub const SECTION_HEADER_SIZE: usize = 8;

pub const SEC_RATE_RULES: u8 = 1;
pub const SEC_PATTERN_RULES: u8 = 2;
pub const SEC_THRESHOLD_RULES: u8 = 3;
pub const SEC_TUNER_CONFIG: u8 = 4;
pub const SEC_REMEDIATION_RULES: u8 = 5;
pub const SEC_DEDUP_CONFIG: u8 = 6;
pub const SEC_TRUST_CONFIG: u8 = 7;
pub const SEC_ACCESS_RULES: u8 = 8;

// Fixed-size entry sizes
pub const RATE_RULE_SIZE: usize = 20;
pub const PATTERN_RULE_SIZE: usize = 48;
pub const THRESHOLD_RULE_SIZE: usize = 28;
pub const TUNER_CONFIG_SIZE: usize = 32;
pub const REMEDIATION_RULE_SIZE: usize = 16;
pub const DEDUP_CONFIG_SIZE: usize = 8;
pub const TRUST_CONFIG_SIZE: usize = 24;
pub const ACCESS_RULE_SIZE: usize = 36;

// =============================================================================
// Parsed policy config — holds all configurable parameters
// =============================================================================

#[derive(Clone)]
pub struct RateRuleConfig {
    pub event_type: EventType,
    pub max_count: u32,
    pub window_ticks: u64,
    pub severity: Severity,
}

#[derive(Clone)]
pub struct PatternRuleConfig {
    pub name: [u8; 16],
    pub name_len: u8,
    pub sequence: [EventType; 5],
    pub seq_len: u8,
    pub window_ticks: u64,
    pub severity: Severity,
}

#[derive(Clone)]
pub struct ThresholdRuleConfig {
    pub metric: [u8; 16],
    pub metric_len: u8,
    pub max_val: f32,
    pub severity: Severity,
}

#[derive(Clone)]
pub struct TunerConfig {
    pub high_rate: f32,
    pub critical_rate: f32,
    pub high_proc_count: u32,
    pub critical_proc_count: u32,
    pub default_quantum: u32,
    pub cooldown_ticks: u64,
}

#[derive(Clone)]
pub struct RemediationRuleConfig {
    pub min_severity: Severity,
    pub action: RemediationAction,
    pub param: u16,
    pub repeat_limit: u32,
    pub cooldown_ticks: u64,
}

#[derive(Clone, Copy)]
pub struct DedupConfig {
    pub window_ticks: u64,
}

#[derive(Clone, Copy)]
pub struct TrustConfig {
    pub failures_to_suspicious: u32,
    pub failures_to_untrusted: u32,
    pub denials_to_suspicious: u32,
    pub denials_to_untrusted: u32,
    pub anomalies_to_suspicious: u32,
    pub anomalies_to_untrusted: u32,
}

#[derive(Clone)]
pub struct AccessRuleConfig {
    pub pattern: [u8; 32],
    pub pattern_len: u8,
    pub min_privilege: u8,
}

pub struct PolicyConfig {
    pub rate_rules: Vec<RateRuleConfig>,
    pub pattern_rules: Vec<PatternRuleConfig>,
    pub threshold_rules: Vec<ThresholdRuleConfig>,
    pub tuner: TunerConfig,
    pub remediation_rules: Vec<RemediationRuleConfig>,
    pub dedup: DedupConfig,
    pub trust: TrustConfig,
    pub access_rules: Vec<AccessRuleConfig>,
    pub version: u16,
}

// =============================================================================
// Default config — matches the const values in types.rs::config
// =============================================================================

impl PolicyConfig {
    pub fn default() -> Self {
        let rate_rules = config::RATE_RULES.iter().map(|&(evt_str, max, window, sev)| {
            RateRuleConfig {
                event_type: EventType::from_str(evt_str),
                max_count: max,
                window_ticks: window,
                severity: sev,
            }
        }).collect();

        let pattern_rules = config::PATTERN_RULES.iter().map(|&(name, seq, window, sev)| {
            let mut name_buf = [0u8; 16];
            let name_bytes = name.as_bytes();
            let name_len = name_bytes.len().min(16);
            name_buf[..name_len].copy_from_slice(&name_bytes[..name_len]);

            let mut sequence = [EventType::Unknown; 5];
            let seq_len = seq.len().min(5);
            for (i, &s) in seq.iter().take(5).enumerate() {
                sequence[i] = EventType::from_str(s);
            }

            PatternRuleConfig {
                name: name_buf,
                name_len: name_len as u8,
                sequence,
                seq_len: seq_len as u8,
                window_ticks: window,
                severity: sev,
            }
        }).collect();

        let threshold_rules = config::THRESHOLD_RULES.iter().map(|&(metric, max_val, sev)| {
            let mut metric_buf = [0u8; 16];
            let m = metric.as_bytes();
            let mlen = m.len().min(16);
            metric_buf[..mlen].copy_from_slice(&m[..mlen]);

            ThresholdRuleConfig {
                metric: metric_buf,
                metric_len: mlen as u8,
                max_val,
                severity: sev,
            }
        }).collect();

        let tuner = TunerConfig {
            high_rate: config::HIGH_RATE,
            critical_rate: config::CRITICAL_RATE,
            high_proc_count: config::HIGH_PROC_COUNT,
            critical_proc_count: config::CRITICAL_PROC_COUNT,
            default_quantum: config::DEFAULT_QUANTUM,
            cooldown_ticks: config::TUNER_COOLDOWN_TICKS,
        };

        let remediation_rules = alloc::vec![
            RemediationRuleConfig {
                min_severity: Severity::Critical,
                action: RemediationAction::Kill,
                param: 0,
                repeat_limit: 1,
                cooldown_ticks: 6000,
            },
            RemediationRuleConfig {
                min_severity: Severity::High,
                action: RemediationAction::Throttle,
                param: 25,
                repeat_limit: 3,
                cooldown_ticks: 3000,
            },
            RemediationRuleConfig {
                min_severity: Severity::Medium,
                action: RemediationAction::Log,
                param: 0,
                repeat_limit: 10,
                cooldown_ticks: 1000,
            },
            RemediationRuleConfig {
                min_severity: Severity::Low,
                action: RemediationAction::Log,
                param: 0,
                repeat_limit: 20,
                cooldown_ticks: 500,
            },
        ];

        let dedup = DedupConfig {
            window_ticks: config::DEDUP_WINDOW_TICKS,
        };

        let trust = TrustConfig {
            failures_to_suspicious: config::FAILURES_TO_SUSPICIOUS,
            failures_to_untrusted: config::FAILURES_TO_UNTRUSTED,
            denials_to_suspicious: config::DENIALS_TO_SUSPICIOUS,
            denials_to_untrusted: config::DENIALS_TO_UNTRUSTED,
            anomalies_to_suspicious: config::ANOMALIES_TO_SUSPICIOUS,
            anomalies_to_untrusted: config::ANOMALIES_TO_UNTRUSTED,
        };

        Self {
            rate_rules,
            pattern_rules,
            threshold_rules,
            tuner,
            remediation_rules,
            dedup,
            trust,
            access_rules: Vec::new(),
            version: VPOL_VERSION,
        }
    }
}

// =============================================================================
// Binary blob parser
// =============================================================================

fn read_u16_le(data: &[u8], offset: usize) -> u16 {
    if offset + 2 > data.len() { return 0; }
    u16::from_le_bytes([data[offset], data[offset + 1]])
}

fn read_u32_le(data: &[u8], offset: usize) -> u32 {
    if offset + 4 > data.len() { return 0; }
    u32::from_le_bytes([data[offset], data[offset + 1], data[offset + 2], data[offset + 3]])
}

fn read_u64_le(data: &[u8], offset: usize) -> u64 {
    if offset + 8 > data.len() { return 0; }
    u64::from_le_bytes([
        data[offset], data[offset + 1], data[offset + 2], data[offset + 3],
        data[offset + 4], data[offset + 5], data[offset + 6], data[offset + 7],
    ])
}

fn severity_from_u8(v: u8) -> Severity {
    match v {
        0 => Severity::Low,
        1 => Severity::Medium,
        2 => Severity::High,
        3 => Severity::Critical,
        _ => Severity::Low,
    }
}

fn action_from_u8(v: u8) -> RemediationAction {
    match v {
        0 => RemediationAction::Kill,
        1 => RemediationAction::Throttle,
        2 => RemediationAction::Log,
        3 => RemediationAction::Revoke,
        4 => RemediationAction::Suspend,
        _ => RemediationAction::Log,
    }
}

fn event_type_from_4bytes(buf: &[u8]) -> EventType {
    // Find null terminator or end of buf
    let end = buf.iter().position(|&b| b == 0).unwrap_or(buf.len());
    let s = core::str::from_utf8(&buf[..end]).unwrap_or("");
    EventType::from_str(s)
}

/// Parse a VPOL binary blob into PolicyConfig.
/// Returns None if magic/version mismatch or data is corrupt.
pub fn parse_policy_blob(data: &[u8]) -> Option<PolicyConfig> {
    if data.len() < HEADER_SIZE { return None; }
    if &data[0..4] != VPOL_MAGIC { return None; }

    let version = read_u16_le(data, 4);
    if version == 0 { return None; }

    let section_count = read_u16_le(data, 6);
    let mut config = PolicyConfig::default();
    config.version = version;

    let mut offset = HEADER_SIZE;

    for _ in 0..section_count {
        if offset + SECTION_HEADER_SIZE > data.len() { break; }

        let sec_type = data[offset];
        let entry_count = read_u16_le(data, offset + 2) as usize;
        let data_size = read_u32_le(data, offset + 4) as usize;
        offset += SECTION_HEADER_SIZE;

        if offset + data_size > data.len() { break; }

        let sec_data = &data[offset..offset + data_size];

        match sec_type {
            SEC_RATE_RULES => {
                config.rate_rules.clear();
                for i in 0..entry_count {
                    let base = i * RATE_RULE_SIZE;
                    if base + RATE_RULE_SIZE > sec_data.len() { break; }
                    config.rate_rules.push(RateRuleConfig {
                        event_type: event_type_from_4bytes(&sec_data[base..base + 4]),
                        max_count: read_u32_le(sec_data, base + 4),
                        window_ticks: read_u64_le(sec_data, base + 8),
                        severity: severity_from_u8(sec_data[base + 16]),
                    });
                }
            }
            SEC_PATTERN_RULES => {
                config.pattern_rules.clear();
                for i in 0..entry_count {
                    let base = i * PATTERN_RULE_SIZE;
                    if base + PATTERN_RULE_SIZE > sec_data.len() { break; }

                    let mut name = [0u8; 16];
                    name.copy_from_slice(&sec_data[base..base + 16]);
                    let name_len = name.iter().position(|&b| b == 0).unwrap_or(16) as u8;

                    let seq_count = sec_data[base + 16].min(5);
                    let severity = severity_from_u8(sec_data[base + 17]);
                    // [18..20] padding

                    let mut sequence = [EventType::Unknown; 5];
                    for s in 0..seq_count as usize {
                        let sbase = base + 20 + s * 4;
                        sequence[s] = event_type_from_4bytes(&sec_data[sbase..sbase + 4]);
                    }
                    // window_ticks at offset 40
                    let window_ticks = read_u64_le(sec_data, base + 40);

                    config.pattern_rules.push(PatternRuleConfig {
                        name,
                        name_len,
                        sequence,
                        seq_len: seq_count,
                        window_ticks,
                        severity,
                    });
                }
            }
            SEC_THRESHOLD_RULES => {
                config.threshold_rules.clear();
                for i in 0..entry_count {
                    let base = i * THRESHOLD_RULE_SIZE;
                    if base + THRESHOLD_RULE_SIZE > sec_data.len() { break; }

                    let mut metric = [0u8; 16];
                    metric.copy_from_slice(&sec_data[base..base + 16]);
                    let metric_len = metric.iter().position(|&b| b == 0).unwrap_or(16) as u8;

                    let max_val_x100 = read_u32_le(sec_data, base + 16);
                    let severity = severity_from_u8(sec_data[base + 20]);

                    config.threshold_rules.push(ThresholdRuleConfig {
                        metric,
                        metric_len,
                        max_val: max_val_x100 as f32 / 100.0,
                        severity,
                    });
                }
            }
            SEC_TUNER_CONFIG => {
                if sec_data.len() >= TUNER_CONFIG_SIZE {
                    config.tuner = TunerConfig {
                        high_rate: read_u32_le(sec_data, 0) as f32 / 100.0,
                        critical_rate: read_u32_le(sec_data, 4) as f32 / 100.0,
                        high_proc_count: read_u32_le(sec_data, 8),
                        critical_proc_count: read_u32_le(sec_data, 12),
                        default_quantum: read_u32_le(sec_data, 16),
                        cooldown_ticks: read_u64_le(sec_data, 24),
                    };
                }
            }
            SEC_REMEDIATION_RULES => {
                config.remediation_rules.clear();
                for i in 0..entry_count {
                    let base = i * REMEDIATION_RULE_SIZE;
                    if base + REMEDIATION_RULE_SIZE > sec_data.len() { break; }
                    config.remediation_rules.push(RemediationRuleConfig {
                        min_severity: severity_from_u8(sec_data[base]),
                        action: action_from_u8(sec_data[base + 1]),
                        param: read_u16_le(sec_data, base + 2),
                        repeat_limit: read_u32_le(sec_data, base + 4),
                        cooldown_ticks: read_u64_le(sec_data, base + 8),
                    });
                }
            }
            SEC_DEDUP_CONFIG => {
                if sec_data.len() >= DEDUP_CONFIG_SIZE {
                    config.dedup = DedupConfig {
                        window_ticks: read_u64_le(sec_data, 0),
                    };
                }
            }
            SEC_TRUST_CONFIG => {
                if sec_data.len() >= TRUST_CONFIG_SIZE {
                    config.trust = TrustConfig {
                        failures_to_suspicious: read_u32_le(sec_data, 0),
                        failures_to_untrusted: read_u32_le(sec_data, 4),
                        denials_to_suspicious: read_u32_le(sec_data, 8),
                        denials_to_untrusted: read_u32_le(sec_data, 12),
                        anomalies_to_suspicious: read_u32_le(sec_data, 16),
                        anomalies_to_untrusted: read_u32_le(sec_data, 20),
                    };
                }
            }
            SEC_ACCESS_RULES => {
                config.access_rules.clear();
                for i in 0..entry_count {
                    let base = i * ACCESS_RULE_SIZE;
                    if base + ACCESS_RULE_SIZE > sec_data.len() { break; }

                    let mut pattern = [0u8; 32];
                    pattern.copy_from_slice(&sec_data[base..base + 32]);
                    let pattern_len = pattern.iter().position(|&b| b == 0).unwrap_or(32) as u8;

                    config.access_rules.push(AccessRuleConfig {
                        pattern,
                        pattern_len,
                        min_privilege: sec_data[base + 32],
                    });
                }
            }
            _ => {} // Unknown section — skip
        }

        offset += data_size;
    }

    Some(config)
}

/// Get a pattern rule name as &str.
impl PatternRuleConfig {
    pub fn name_str(&self) -> &str {
        let len = self.name_len as usize;
        core::str::from_utf8(&self.name[..len]).unwrap_or("?")
    }

    pub fn sequence_slice(&self) -> &[EventType] {
        &self.sequence[..self.seq_len as usize]
    }
}

impl ThresholdRuleConfig {
    pub fn metric_str(&self) -> &str {
        let len = self.metric_len as usize;
        core::str::from_utf8(&self.metric[..len]).unwrap_or("?")
    }
}

impl AccessRuleConfig {
    pub fn pattern_str(&self) -> &str {
        let len = self.pattern_len as usize;
        core::str::from_utf8(&self.pattern[..len]).unwrap_or("")
    }
}
