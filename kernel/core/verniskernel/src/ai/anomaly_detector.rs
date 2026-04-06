// anomaly_detector.rs — Rate, Pattern, and Threshold anomaly detectors
//
// Port of ai/anomaly_detector.py to Rust no_std.

use alloc::collections::VecDeque;
use alloc::string::String;
use alloc::vec::Vec;
use alloc::format;
use super::types::*;
use super::policy::{RateRuleConfig, PatternRuleConfig, ThresholdRuleConfig};

// =============================================================================
// Rate Detector — event frequency in sliding window
// =============================================================================

struct RateWindow {
    timestamps: VecDeque<u64>,
    event_type: EventType,
    max_count: u32,
    window_ticks: u64,
    severity: Severity,
}

impl RateWindow {
    fn new(event_type: EventType, max_count: u32, window_ticks: u64, severity: Severity) -> Self {
        Self {
            timestamps: VecDeque::with_capacity(64),
            event_type,
            max_count,
            window_ticks,
            severity,
        }
    }

    /// Feed a tick, slide window, return true if rate exceeded.
    fn feed(&mut self, now: u64) -> bool {
        self.timestamps.push_back(now);
        let cutoff = now.saturating_sub(self.window_ticks);
        while let Some(&front) = self.timestamps.front() {
            if front < cutoff {
                self.timestamps.pop_front();
            } else {
                break;
            }
        }
        self.timestamps.len() as u32 > self.max_count
    }

    fn count(&self) -> u32 {
        self.timestamps.len() as u32
    }
}

// =============================================================================
// Pattern Detector — multi-event sequence matching
// =============================================================================

struct PatternRule {
    name: [u8; 16],
    name_len: u8,
    sequence: [EventType; 5],
    seq_len: u8,
    window_ticks: u64,
    severity: Severity,
}

struct PatternHistory {
    entries: VecDeque<(u64, EventType)>, // (tick, type)
}

impl PatternHistory {
    fn new() -> Self {
        Self {
            entries: VecDeque::with_capacity(64),
        }
    }

    fn push(&mut self, tick: u64, evt: EventType) {
        if self.entries.len() >= 64 {
            self.entries.pop_front();
        }
        self.entries.push_back((tick, evt));
    }

    /// Check if a pattern sequence occurred within window.
    fn matches(&self, rule: &PatternRule, now: u64) -> bool {
        let cutoff = now.saturating_sub(rule.window_ticks);
        let seq_len = rule.seq_len as usize;
        let seq = &rule.sequence[..seq_len];
        let mut seq_idx = 0;

        for &(tick, ref evt) in self.entries.iter() {
            if tick < cutoff {
                continue;
            }
            if *evt == seq[seq_idx] {
                seq_idx += 1;
                if seq_idx >= seq_len {
                    return true;
                }
            }
        }
        false
    }
}

// =============================================================================
// Threshold Detector — numeric metric thresholds
// =============================================================================

struct ThresholdRule {
    metric: [u8; 16],
    metric_len: u8,
    max_val: f32,
    severity: Severity,
}

// =============================================================================
// Composite Anomaly Detector
// =============================================================================

pub struct AnomalyDetector {
    rate_windows: Vec<RateWindow>,
    pattern_history: PatternHistory,
    pattern_rules: Vec<PatternRule>,
    threshold_rules: Vec<ThresholdRule>,
    anomaly_history: VecDeque<Anomaly>,
    max_history: usize,
}

impl AnomalyDetector {
    pub fn new() -> Self {
        // Build rate windows from config
        let rate_windows = config::RATE_RULES.iter().map(|&(evt_str, max, window, sev)| {
            RateWindow::new(EventType::from_str(evt_str), max, window, sev)
        }).collect();

        // Build pattern rules from config
        let pattern_rules = config::PATTERN_RULES.iter().map(|&(name, seq, window, sev)| {
            let mut name_buf = [0u8; 16];
            let nb = name.as_bytes();
            let nlen = nb.len().min(16);
            name_buf[..nlen].copy_from_slice(&nb[..nlen]);

            let mut sequence = [EventType::Unknown; 5];
            let slen = seq.len().min(5);
            for (i, &s) in seq.iter().take(5).enumerate() {
                sequence[i] = EventType::from_str(s);
            }

            PatternRule {
                name: name_buf,
                name_len: nlen as u8,
                sequence,
                seq_len: slen as u8,
                window_ticks: window,
                severity: sev,
            }
        }).collect();

        // Build threshold rules from config
        let threshold_rules = config::THRESHOLD_RULES.iter().map(|&(metric, max_val, sev)| {
            let mut metric_buf = [0u8; 16];
            let mb = metric.as_bytes();
            let mlen = mb.len().min(16);
            metric_buf[..mlen].copy_from_slice(&mb[..mlen]);

            ThresholdRule {
                metric: metric_buf,
                metric_len: mlen as u8,
                max_val,
                severity: sev,
            }
        }).collect();

        Self {
            rate_windows,
            pattern_history: PatternHistory::new(),
            pattern_rules,
            threshold_rules,
            anomaly_history: VecDeque::with_capacity(200),
            max_history: 200,
        }
    }

    /// Feed an event into all detectors. Returns any triggered anomalies.
    pub fn feed_event(&mut self, event_type: EventType, data: &str, now: u64) -> Vec<Anomaly> {
        let mut anomalies = Vec::new();

        // Rate detection
        for window in self.rate_windows.iter_mut() {
            if window.event_type == event_type {
                if window.feed(now) {
                    let anomaly = Anomaly {
                        detector: DetectorKind::Rate,
                        severity: window.severity,
                        title: format!("{} rate exceeded", event_type.as_str()),
                        detail: format!("{} events in window (max {})",
                                       window.count(), window.max_count),
                        timestamp: KernelInstant::from_ticks(now),
                        source_pid: EventStore::extract_pid_static(event_type, data),
                    };
                    anomalies.push(anomaly);
                }
            }
        }

        // Pattern detection
        self.pattern_history.push(now, event_type);
        for rule in self.pattern_rules.iter() {
            if self.pattern_history.matches(rule, now) {
                let rname = core::str::from_utf8(&rule.name[..rule.name_len as usize]).unwrap_or("?");
                let anomaly = Anomaly {
                    detector: DetectorKind::Pattern,
                    severity: rule.severity,
                    title: String::from(rname),
                    detail: format!("Pattern detected: {}", rname),
                    timestamp: KernelInstant::from_ticks(now),
                    source_pid: EventStore::extract_pid_static(event_type, data),
                };
                anomalies.push(anomaly);
            }
        }

        // Store anomalies in history
        for a in anomalies.iter() {
            if self.anomaly_history.len() >= self.max_history {
                self.anomaly_history.pop_front();
            }
            self.anomaly_history.push_back(a.clone());
        }

        anomalies
    }

    /// Check a numeric metric against threshold rules.
    pub fn check_threshold(&mut self, metric: &str, value: f32, now: u64) -> Vec<Anomaly> {
        let mut anomalies = Vec::new();

        for rule in self.threshold_rules.iter() {
            let rmetric = core::str::from_utf8(&rule.metric[..rule.metric_len as usize]).unwrap_or("");
            if rmetric == metric && value > rule.max_val {
                let anomaly = Anomaly {
                    detector: DetectorKind::Threshold,
                    severity: rule.severity,
                    title: format!("{} threshold exceeded", metric),
                    detail: format!("{} = {:.1} (max {:.1})", metric, value, rule.max_val),
                    timestamp: KernelInstant::from_ticks(now),
                    source_pid: 0,
                };
                anomalies.push(anomaly.clone());

                if self.anomaly_history.len() >= self.max_history {
                    self.anomaly_history.pop_front();
                }
                self.anomaly_history.push_back(anomaly);
            }
        }

        anomalies
    }

    pub fn anomaly_count(&self) -> usize {
        self.anomaly_history.len()
    }

    /// Reload configuration from a parsed PolicyConfig.
    pub fn reload_config(
        &mut self,
        rate_rules: &[RateRuleConfig],
        pattern_rules: &[PatternRuleConfig],
        threshold_rules: &[ThresholdRuleConfig],
    ) {
        // Rebuild rate windows
        self.rate_windows.clear();
        for r in rate_rules {
            self.rate_windows.push(
                RateWindow::new(r.event_type, r.max_count, r.window_ticks, r.severity)
            );
        }

        // Rebuild pattern rules
        self.pattern_rules.clear();
        for p in pattern_rules {
            self.pattern_rules.push(PatternRule {
                name: p.name,
                name_len: p.name_len,
                sequence: p.sequence,
                seq_len: p.seq_len,
                window_ticks: p.window_ticks,
                severity: p.severity,
            });
        }

        // Rebuild threshold rules
        self.threshold_rules.clear();
        for t in threshold_rules {
            self.threshold_rules.push(ThresholdRule {
                metric: t.metric,
                metric_len: t.metric_len,
                max_val: t.max_val,
                severity: t.severity,
            });
        }
    }
}

// Helper to avoid circular dependency — inline PID extraction
struct EventStore;
impl EventStore {
    fn extract_pid_static(event_type: EventType, data: &str) -> u32 {
        let mut fields = [""; 4];
        parse_pipe_fields(data, &mut fields);
        match event_type {
            EventType::Process | EventType::Deny | EventType::Syscall => parse_u32(fields[0]),
            EventType::Exception => parse_u32(fields[2]),
            _ => 0,
        }
    }
}
