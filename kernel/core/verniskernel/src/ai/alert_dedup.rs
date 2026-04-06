// alert_dedup.rs — Duplicate alert suppression
//
// Port of ai/alert_deduplicator.py to Rust no_std.
// Suppresses repeated anomalies with the same detector:title key within a window.

use alloc::collections::BTreeMap;
use alloc::string::String;
use alloc::format;
use super::types::*;

pub struct AlertDeduplicator {
    window_ticks: u64,
    last_sent: BTreeMap<String, u64>,  // key → tick when last allowed
    suppressed: BTreeMap<String, u32>, // key → suppressed count
    total_suppressed: u64,
}

impl AlertDeduplicator {
    pub fn new(window_ticks: u64) -> Self {
        Self {
            window_ticks,
            last_sent: BTreeMap::new(),
            suppressed: BTreeMap::new(),
            total_suppressed: 0,
        }
    }

    /// Returns true if the anomaly should be forwarded (not a duplicate).
    pub fn should_alert(&mut self, anomaly: &Anomaly, now: u64) -> bool {
        let key = format!("{}:{}", anomaly.detector.as_str(), anomaly.title);

        if let Some(&last_tick) = self.last_sent.get(&key) {
            if now.saturating_sub(last_tick) < self.window_ticks {
                *self.suppressed.entry(key).or_insert(0) += 1;
                self.total_suppressed += 1;
                return false;
            }
        }

        self.last_sent.insert(key.clone(), now);
        // Reset suppressed counter for this key
        self.suppressed.remove(&key);
        true
    }

    pub fn total_suppressed(&self) -> u64 {
        self.total_suppressed
    }

    /// Clean up old entries (call periodically to prevent memory growth).
    pub fn gc(&mut self, now: u64) {
        self.last_sent.retain(|_, &mut tick| now.saturating_sub(tick) < self.window_ticks * 2);
        self.suppressed.retain(|k, _| self.last_sent.contains_key(k));
    }

    /// Reload dedup config.
    pub fn reload_config(&mut self, window_ticks: u64) {
        self.window_ticks = window_ticks;
    }
}
