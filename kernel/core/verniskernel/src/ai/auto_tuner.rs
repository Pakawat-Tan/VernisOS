// auto_tuner.rs — Real-time kernel auto-tuning engine
//
// Port of ai/auto_tuner.py to Rust no_std.
// Assesses system load and generates tuning decisions.

use alloc::collections::VecDeque;
use alloc::string::String;
use super::types::*;
use super::policy::TunerConfig as PolicyTunerConfig;

// =============================================================================
// Rate Window — sliding event counter for events/sec estimation
// =============================================================================

struct RateWindow {
    timestamps: VecDeque<u64>,
    window_ticks: u64,
}

impl RateWindow {
    fn new(window_ticks: u64) -> Self {
        Self {
            timestamps: VecDeque::with_capacity(128),
            window_ticks,
        }
    }

    fn push(&mut self, tick: u64) {
        self.timestamps.push_back(tick);
        let cutoff = tick.saturating_sub(self.window_ticks);
        while let Some(&front) = self.timestamps.front() {
            if front < cutoff {
                self.timestamps.pop_front();
            } else {
                break;
            }
        }
    }

    /// Events per second (approximate).
    fn rate(&self) -> f32 {
        let count = self.timestamps.len() as f32;
        let window_sec = self.window_ticks as f32 / 100.0;
        if window_sec > 0.0 { count / window_sec } else { 0.0 }
    }
}

// =============================================================================
// Auto Tuner
// =============================================================================

pub struct AutoTuner {
    event_rate: RateWindow,
    process_count: u32,
    exception_count: u32,
    last_decision_tick: u64,
    decisions: VecDeque<TuningDecision>,
    /// Per-action cooldown: action discriminant → last tick
    cooldowns: [u64; 5], // one per TuneAction variant
    tune_cb: Option<TuneCallback>,
    // Dynamic config
    high_rate: f32,
    critical_rate: f32,
    high_proc_count: u32,
    critical_proc_count: u32,
    default_quantum: u32,
    cooldown_ticks: u64,
}

impl AutoTuner {
    pub fn new() -> Self {
        Self {
            event_rate: RateWindow::new(1000), // 10-second window
            process_count: 0,
            exception_count: 0,
            last_decision_tick: 0,
            decisions: VecDeque::with_capacity(32),
            cooldowns: [0u64; 5],
            tune_cb: None,
            high_rate: config::HIGH_RATE,
            critical_rate: config::CRITICAL_RATE,
            high_proc_count: config::HIGH_PROC_COUNT,
            critical_proc_count: config::CRITICAL_PROC_COUNT,
            default_quantum: config::DEFAULT_QUANTUM,
            cooldown_ticks: config::TUNER_COOLDOWN_TICKS,
        }
    }

    pub fn set_tune_callback(&mut self, cb: TuneCallback) {
        self.tune_cb = Some(cb);
    }

    /// Feed an event to update internal metrics.
    pub fn feed_event(&mut self, event_type: EventType, data: &str, now: u64) {
        self.event_rate.push(now);

        match event_type {
            EventType::Stat => {
                // Parse "key|value" from STAT events
                let mut fields = [""; 4];
                parse_pipe_fields(data, &mut fields);
                if fields[0] == "process_count" {
                    self.process_count = parse_u32(fields[1]);
                }
            }
            EventType::Exception => {
                self.exception_count += 1;
            }
            EventType::Process => {
                // Update process count from tracker if available
                let mut fields = [""; 4];
                parse_pipe_fields(data, &mut fields);
                if fields[1] == "fork" {
                    self.process_count = self.process_count.saturating_add(1);
                } else if fields[1] == "exit" || fields[1] == "kill" {
                    self.process_count = self.process_count.saturating_sub(1);
                }
            }
            _ => {}
        }
    }

    /// Assess current system load level.
    fn assess_load(&self) -> LoadLevel {
        let rate = self.event_rate.rate();
        let procs = self.process_count;

        if rate > self.critical_rate || procs >= self.critical_proc_count {
            LoadLevel::Critical
        } else if rate > self.high_rate || procs >= self.high_proc_count {
            LoadLevel::High
        } else if rate > 5.0 || procs >= 3 {
            LoadLevel::Normal
        } else {
            LoadLevel::Low
        }
    }

    /// Called periodically (every ~500ms) from timer interrupt.
    /// Returns a tuning decision if one should be applied.
    pub fn compute_decision(&mut self, now: u64) -> Option<TuningDecision> {
        let load = self.assess_load();
        let rate = self.event_rate.rate();
        let _procs = self.process_count;

        let decision = match load {
            LoadLevel::Critical => {
                if !self.on_cooldown(TuneAction::Throttle, now) {
                    // Widen quantum to reduce context switches under pressure
                    let quantum = 20 + (rate as u32).min(30);
                    Some(TuningDecision {
                        action: TuneAction::SchedQuantum,
                        target: String::from("scheduler"),
                        value: quantum,
                        load,
                        timestamp: KernelInstant::from_ticks(now),
                    })
                } else {
                    None
                }
            }
            LoadLevel::High => {
                if !self.on_cooldown(TuneAction::SchedQuantum, now) {
                    let quantum = 15 + (rate as u32 / 2).min(15);
                    Some(TuningDecision {
                        action: TuneAction::SchedQuantum,
                        target: String::from("scheduler"),
                        value: quantum,
                        load,
                        timestamp: KernelInstant::from_ticks(now),
                    })
                } else {
                    None
                }
            }
            LoadLevel::Low => {
                if !self.on_cooldown(TuneAction::SchedQuantum, now) {
                    Some(TuningDecision {
                        action: TuneAction::SchedQuantum,
                        target: String::from("scheduler"),
                        value: 5, // narrow quantum for responsiveness
                        load,
                        timestamp: KernelInstant::from_ticks(now),
                    })
                } else {
                    None
                }
            }
            LoadLevel::Normal => {
                if self.exception_count > 0 && !self.on_cooldown(TuneAction::SchedPrio, now) {
                    self.exception_count = 0;
                    Some(TuningDecision {
                        action: TuneAction::SchedPrio,
                        target: String::from("scheduler"),
                        value: self.default_quantum,
                        load,
                        timestamp: KernelInstant::from_ticks(now),
                    })
                } else {
                    None
                }
            }
        };

        if let Some(ref d) = decision {
            let action_idx = d.action as usize;
            if action_idx < 5 {
                self.cooldowns[action_idx] = now;
            }

            // Execute callback
            if let Some(cb) = self.tune_cb {
                let action_str = d.action.as_str();
                let target_str = d.target.as_str();
                cb(action_str.as_ptr(), action_str.len(),
                   target_str.as_ptr(), target_str.len(),
                   d.value);
            }

            if self.decisions.len() >= 32 {
                self.decisions.pop_front();
            }
            self.decisions.push_back(d.clone());
        }

        decision
    }

    fn on_cooldown(&self, action: TuneAction, now: u64) -> bool {
        let idx = action as usize;
        if idx >= 5 { return false; }
        now.saturating_sub(self.cooldowns[idx]) < self.cooldown_ticks
    }

    pub fn decision_count(&self) -> usize {
        self.decisions.len()
    }

    /// Reload tuner config from policy.
    pub fn reload_config(&mut self, cfg: &PolicyTunerConfig) {
        self.high_rate = cfg.high_rate;
        self.critical_rate = cfg.critical_rate;
        self.high_proc_count = cfg.high_proc_count;
        self.critical_proc_count = cfg.critical_proc_count;
        self.default_quantum = cfg.default_quantum;
        self.cooldown_ticks = cfg.cooldown_ticks;
    }
}
