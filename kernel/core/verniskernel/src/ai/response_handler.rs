// response_handler.rs — Remediation rule evaluation
//
// Port of ai/response_handler.py to Rust no_std.
// Evaluates anomalies against severity-based rules and triggers remediation actions.

use alloc::collections::BTreeMap;
use alloc::vec;
use alloc::vec::Vec;
use super::types::*;
use super::policy::RemediationRuleConfig;

// =============================================================================
// Remediation rule
// =============================================================================

struct RemediationRule {
    min_severity: Severity,
    action: RemediationAction,
    param: u32,           // e.g., quantum ms for throttle
    repeat_limit: u32,
    cooldown_ticks: u64,
}

// =============================================================================
// History record for cooldown tracking
// =============================================================================

struct ActionRecord {
    last_tick: u64,
    count: u32,
}

// =============================================================================
// Response Handler
// =============================================================================

pub struct ResponseHandler {
    rules: Vec<RemediationRule>,
    /// Key: "action:pid" → cooldown record
    history: BTreeMap<u64, ActionRecord>, // hash-like key from action+pid
    tune_cb: Option<TuneCallback>,
    remediate_cb: Option<RemediateCallback>,
}

impl ResponseHandler {
    pub fn new() -> Self {
        let rules = vec![
            RemediationRule {
                min_severity: Severity::Critical,
                action: RemediationAction::Kill,
                param: 0,
                repeat_limit: 1,
                cooldown_ticks: 6000, // 60s
            },
            RemediationRule {
                min_severity: Severity::High,
                action: RemediationAction::Throttle,
                param: 25, // quantum ms
                repeat_limit: 3,
                cooldown_ticks: 3000, // 30s
            },
            RemediationRule {
                min_severity: Severity::Medium,
                action: RemediationAction::Log,
                param: 0,
                repeat_limit: 10,
                cooldown_ticks: 1000, // 10s
            },
            RemediationRule {
                min_severity: Severity::Low,
                action: RemediationAction::Log,
                param: 0,
                repeat_limit: 20,
                cooldown_ticks: 500, // 5s
            },
        ];

        Self {
            rules,
            history: BTreeMap::new(),
            tune_cb: None,
            remediate_cb: None,
        }
    }

    pub fn set_tune_callback(&mut self, cb: TuneCallback) {
        self.tune_cb = Some(cb);
    }

    pub fn set_remediate_callback(&mut self, cb: RemediateCallback) {
        self.remediate_cb = Some(cb);
    }

    /// Evaluate an anomaly and trigger remediation if appropriate.
    /// Returns the action taken, or None if suppressed by cooldown.
    pub fn handle_anomaly(&mut self, anomaly: &Anomaly, now: u64) -> Option<RemediationAction> {
        let pid = anomaly.source_pid;

        // Find first matching rule (ordered by severity, highest first)
        let matched_rule = self.rules.iter().find(|r| anomaly.severity >= r.min_severity)?;

        let action = matched_rule.action;
        let param = matched_rule.param;
        let cooldown = matched_rule.cooldown_ticks;
        let limit = matched_rule.repeat_limit;

        // Cooldown key: combine action discriminant and pid
        let key = (action as u64) * 100000 + pid as u64;

        if let Some(record) = self.history.get(&key) {
            if now.saturating_sub(record.last_tick) < cooldown {
                return None; // On cooldown
            }
            if record.count >= limit {
                return None; // Repeat limit reached
            }
        }

        // Execute the action
        match action {
            RemediationAction::Kill => {
                if let Some(cb) = self.remediate_cb {
                    let a = b"kill\0";
                    cb(a.as_ptr(), 4, pid, 0);
                }
            }
            RemediationAction::Throttle => {
                if let Some(cb) = self.remediate_cb {
                    let a = b"throttle\0";
                    cb(a.as_ptr(), 8, pid, param);
                }
            }
            RemediationAction::Log => {
                // Just log — no kernel action needed
            }
            RemediationAction::Revoke | RemediationAction::Suspend => {
                if let Some(cb) = self.remediate_cb {
                    let a_str = action.as_str();
                    cb(a_str.as_ptr(), a_str.len(), pid, param);
                }
            }
        }

        // Update history
        let record = self.history.entry(key).or_insert(ActionRecord {
            last_tick: 0,
            count: 0,
        });
        record.last_tick = now;
        record.count += 1;

        Some(action)
    }

    /// Clean up old history entries.
    pub fn gc(&mut self, now: u64) {
        self.history.retain(|_, r| now.saturating_sub(r.last_tick) < 12000); // 2 min
    }

    /// Reload remediation rules from policy.
    pub fn reload_config(&mut self, rules: &[RemediationRuleConfig]) {
        self.rules.clear();
        for r in rules {
            self.rules.push(RemediationRule {
                min_severity: r.min_severity,
                action: r.action,
                param: r.param as u32,
                repeat_limit: r.repeat_limit,
                cooldown_ticks: r.cooldown_ticks,
            });
        }
        // Clear action history on reload
        self.history.clear();
    }
}
