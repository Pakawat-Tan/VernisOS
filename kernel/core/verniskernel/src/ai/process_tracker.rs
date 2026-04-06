// process_tracker.rs — Per-PID process lifecycle tracking + trust scoring
//
// Port of ai/process_tracker.py to Rust no_std.

use alloc::collections::BTreeMap;
use alloc::string::String;
use alloc::vec::Vec;
use alloc::collections::VecDeque;
use super::types::*;
use super::policy::TrustConfig;

// =============================================================================
// Process Profile
// =============================================================================

pub struct ProcessProfile {
    pub pid: u32,
    pub name: String,
    pub parent_pid: u32,
    pub created_at: KernelInstant,
    pub exited_at: Option<KernelInstant>,

    pub syscall_count: u32,
    pub failure_count: u32,
    pub denial_count: u32,
    pub exception_count: u32,
    pub anomaly_count: u32,

    pub trust: TrustLevel,
    pub anomaly_flags: u32, // bitmask from anomaly_flags module
}

impl ProcessProfile {
    fn new(pid: u32, name: &str, parent_pid: u32, now: u64) -> Self {
        Self {
            pid,
            name: String::from(name),
            parent_pid,
            created_at: KernelInstant::from_ticks(now),
            exited_at: None,
            syscall_count: 0,
            failure_count: 0,
            denial_count: 0,
            exception_count: 0,
            anomaly_count: 0,
            trust: TrustLevel::Normal,
            anomaly_flags: 0,
        }
    }

    fn recompute_trust(&mut self, tc: &TrustConfig) {
        if self.failure_count >= tc.failures_to_untrusted
            || self.denial_count >= tc.denials_to_untrusted
            || self.anomaly_count >= tc.anomalies_to_untrusted
        {
            self.trust = TrustLevel::Untrusted;
        } else if self.failure_count >= tc.failures_to_suspicious
            || self.denial_count >= tc.denials_to_suspicious
            || self.anomaly_count >= tc.anomalies_to_suspicious
        {
            self.trust = TrustLevel::Suspicious;
        }
        // Trust never goes back up automatically
    }

    pub fn is_alive(&self) -> bool {
        self.exited_at.is_none()
    }
}

// =============================================================================
// Process Tracker
// =============================================================================

pub struct ProcessTracker {
    procs: BTreeMap<u32, ProcessProfile>,
    exited_pids: VecDeque<u32>,
    trust_cfg: TrustConfig,
}

impl ProcessTracker {
    pub fn new() -> Self {
        Self {
            procs: BTreeMap::new(),
            exited_pids: VecDeque::with_capacity(config::EXITED_PROCESS_LIMIT),
            trust_cfg: TrustConfig {
                failures_to_suspicious: config::FAILURES_TO_SUSPICIOUS,
                failures_to_untrusted: config::FAILURES_TO_UNTRUSTED,
                denials_to_suspicious: config::DENIALS_TO_SUSPICIOUS,
                denials_to_untrusted: config::DENIALS_TO_UNTRUSTED,
                anomalies_to_suspicious: config::ANOMALIES_TO_SUSPICIOUS,
                anomalies_to_untrusted: config::ANOMALIES_TO_UNTRUSTED,
            },
        }
    }

    /// Handle PROC event: "pid|action|name"
    /// Actions: fork, exec, exit, kill
    pub fn on_proc_event(&mut self, data: &str, now: u64) {
        let mut fields = [""; 4];
        parse_pipe_fields(data, &mut fields);
        let pid = parse_u32(fields[0]);
        let action = fields[1];
        let name = fields[2];

        if pid == 0 { return; }

        match action {
            "fork" | "exec" => {
                if !self.procs.contains_key(&pid) {
                    let profile = ProcessProfile::new(pid, name, 0, now);
                    self.procs.insert(pid, profile);
                } else if action == "exec" {
                    if let Some(p) = self.procs.get_mut(&pid) {
                        if !name.is_empty() {
                            p.name = String::from(name);
                        }
                    }
                }
            }
            "exit" | "kill" => {
                if let Some(p) = self.procs.get_mut(&pid) {
                    p.exited_at = Some(KernelInstant::from_ticks(now));
                    self.exited_pids.push_back(pid);
                    self.gc_exited();
                }
            }
            _ => {}
        }
    }

    /// Handle EXCP event: "code|addr|pid"
    pub fn on_exception(&mut self, data: &str, _now: u64) {
        let mut fields = [""; 4];
        parse_pipe_fields(data, &mut fields);
        let pid = parse_u32(fields[2]);
        let tc = self.trust_cfg;

        if let Some(p) = self.procs.get_mut(&pid) {
            p.exception_count += 1;
            p.failure_count += 1;
            p.recompute_trust(&tc);
        }
    }

    /// Handle DENY event: "pid|syscall_num"
    pub fn on_denial(&mut self, data: &str, _now: u64) {
        let mut fields = [""; 4];
        parse_pipe_fields(data, &mut fields);
        let pid = parse_u32(fields[0]);
        let tc = self.trust_cfg;

        if let Some(p) = self.procs.get_mut(&pid) {
            p.denial_count += 1;
            p.recompute_trust(&tc);
        }
    }

    /// Handle SYSCALL event: "pid|syscall_num"
    pub fn on_syscall(&mut self, data: &str, _now: u64) {
        let mut fields = [""; 4];
        parse_pipe_fields(data, &mut fields);
        let pid = parse_u32(fields[0]);

        if let Some(p) = self.procs.get_mut(&pid) {
            p.syscall_count += 1;
        }
    }

    /// Record an anomaly against a PID.
    pub fn record_anomaly(&mut self, pid: u32, flag: u32) {
        let tc = self.trust_cfg;
        if let Some(p) = self.procs.get_mut(&pid) {
            p.anomaly_count += 1;
            p.anomaly_flags |= flag;
            p.recompute_trust(&tc);
        }
    }

    /// Get process profile by PID.
    pub fn get(&self, pid: u32) -> Option<&ProcessProfile> {
        self.procs.get(&pid)
    }

    /// Count of active (non-exited) processes.
    pub fn active_count(&self) -> u32 {
        self.procs.values().filter(|p| p.is_alive()).count() as u32
    }

    /// List suspicious/untrusted processes.
    pub fn suspicious_pids(&self) -> Vec<u32> {
        self.procs.values()
            .filter(|p| p.is_alive() && p.trust <= TrustLevel::Suspicious)
            .map(|p| p.pid)
            .collect()
    }

    fn gc_exited(&mut self) {
        while self.exited_pids.len() > config::EXITED_PROCESS_LIMIT {
            if let Some(old_pid) = self.exited_pids.pop_front() {
                self.procs.remove(&old_pid);
            }
        }
    }

    /// Reload trust thresholds from policy.
    pub fn reload_config(&mut self, cfg: &TrustConfig) {
        self.trust_cfg = *cfg;
    }
}
