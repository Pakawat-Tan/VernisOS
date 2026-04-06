// ai/mod.rs — In-kernel AI Engine root module
//
// Integrates all AI components and exposes extern "C" FFI functions
// for the C kernel to call.

pub mod types;
pub mod event_store;
pub mod anomaly_detector;
pub mod process_tracker;
pub mod alert_dedup;
pub mod response_handler;
pub mod auto_tuner;
pub mod policy;

use alloc::boxed::Box;
use types::*;
use event_store::EventStore;
use anomaly_detector::AnomalyDetector;
use process_tracker::ProcessTracker;
use alert_dedup::AlertDeduplicator;
use response_handler::ResponseHandler;
use auto_tuner::AutoTuner;
use policy::PolicyConfig;
use crate::kernel_print_raw;

// =============================================================================
// AI Engine — composite struct holding all components
// =============================================================================

pub struct AiEngine {
    pub event_store: EventStore,
    pub anomaly_detector: AnomalyDetector,
    pub process_tracker: ProcessTracker,
    pub alert_dedup: AlertDeduplicator,
    pub response_handler: ResponseHandler,
    pub auto_tuner: AutoTuner,
    gc_counter: u32,
    policy_ver: u16,
    access_rules: alloc::vec::Vec<policy::AccessRuleConfig>,
}

impl AiEngine {
    pub fn new() -> Self {
        Self {
            event_store: EventStore::new(config::EVENT_STORE_CAPACITY),
            anomaly_detector: AnomalyDetector::new(),
            process_tracker: ProcessTracker::new(),
            alert_dedup: AlertDeduplicator::new(config::DEDUP_WINDOW_TICKS),
            response_handler: ResponseHandler::new(),
            auto_tuner: AutoTuner::new(),
            gc_counter: 0,
            policy_ver: 1,
            access_rules: alloc::vec::Vec::new(),
        }
    }

    /// Feed an event through all AI components.
    pub fn feed_event(&mut self, event_type_str: &str, data: &str, now: u64) {
        let event_type = EventType::from_str(event_type_str);
        self.feed_event_inner(event_type, data, now);
    }

    /// Feed an event using numeric event code — O(1) type dispatch (Phase 15)
    pub fn feed_event_code(&mut self, event_code: u8, data: &str, now: u64) {
        let event_type = EventType::from_u8(event_code);
        self.feed_event_inner(event_type, data, now);
    }

    fn feed_event_inner(&mut self, event_type: EventType, data: &str, now: u64) {
        let pid = EventStore::extract_pid(event_type, data);

        // 1. Record in event store
        self.event_store.record(event_type, data, pid, now);

        // 2. Route to process tracker
        match event_type {
            EventType::Process => self.process_tracker.on_proc_event(data, now),
            EventType::Exception => self.process_tracker.on_exception(data, now),
            EventType::Deny => self.process_tracker.on_denial(data, now),
            EventType::Syscall => self.process_tracker.on_syscall(data, now),
            _ => {}
        }

        // 3. Feed to anomaly detector
        let anomalies = self.anomaly_detector.feed_event(event_type, data, now);

        // 4. Check STAT thresholds
        if event_type == EventType::Stat {
            let mut fields = [""; 4];
            parse_pipe_fields(data, &mut fields);
            let value = parse_f32(fields[1]);
            let threshold_anomalies = self.anomaly_detector.check_threshold(fields[0], value, now);
            for a in threshold_anomalies {
                self.process_anomaly(&a, now);
            }
        }

        // 5. Process detected anomalies
        for a in anomalies {
            self.process_anomaly(&a, now);
        }

        // 6. Feed auto-tuner
        self.auto_tuner.feed_event(event_type, data, now);
    }

    fn process_anomaly(&mut self, anomaly: &Anomaly, now: u64) {
        // Dedup check
        if !self.alert_dedup.should_alert(anomaly, now) {
            return;
        }

        // Record anomaly against PID (silent — no serial output during IRQ)
        if anomaly.source_pid > 0 {
            self.process_tracker.record_anomaly(anomaly.source_pid, 0);
        }

        // Evaluate remediation
        self.response_handler.handle_anomaly(anomaly, now);
    }

    /// Called periodically from timer IRQ (~every 500ms = 50 ticks).
    pub fn tick(&mut self, now: u64) {
        // Compute auto-tuning decision
        self.auto_tuner.compute_decision(now);

        // Periodic GC every ~30s (60 ticks at 500ms cadence)
        self.gc_counter += 1;
        if self.gc_counter >= 60 {
            self.gc_counter = 0;
            self.alert_dedup.gc(now);
            self.response_handler.gc(now);
        }
    }

    pub fn set_tune_callback(&mut self, cb: TuneCallback) {
        self.auto_tuner.set_tune_callback(cb);
    }

    pub fn set_remediate_callback(&mut self, cb: RemediateCallback) {
        self.response_handler.set_remediate_callback(cb);
    }

    /// Load a new policy configuration. Distributes to all components.
    /// Returns true on success.
    pub fn load_policy(&mut self, config: PolicyConfig) -> bool {
        self.anomaly_detector.reload_config(
            &config.rate_rules,
            &config.pattern_rules,
            &config.threshold_rules,
        );
        self.auto_tuner.reload_config(&config.tuner);
        self.response_handler.reload_config(&config.remediation_rules);
        self.alert_dedup.reload_config(config.dedup.window_ticks);
        self.process_tracker.reload_config(&config.trust);
        // Store access rules for command-level enforcement
        self.access_rules = config.access_rules;
        true
    }

    /// Check if a command is allowed at the given privilege level.
    /// Returns the required privilege (0=root, 50=admin, 100=user), or 255 if no rule matches.
    /// The caller should allow if session_privilege >= returned value (lower = more privileged).
    pub fn check_access(&self, command: &str) -> u8 {
        let cmd_bytes = command.as_bytes();
        for rule in &self.access_rules {
            let pattern = &rule.pattern[..rule.pattern_len as usize];
            // Pattern is pipe-separated alternatives: "shutdown|restart|halt"
            // Check each alternative
            let mut start = 0;
            for i in 0..=pattern.len() {
                if i == pattern.len() || pattern[i] == b'|' {
                    let alt = &pattern[start..i];
                    if Self::cmd_matches(cmd_bytes, alt) {
                        return rule.min_privilege;
                    }
                    start = i + 1;
                }
            }
        }
        255 // No rule matched
    }

    /// Check if a command matches an alternative pattern.
    /// Matches if the command starts with the pattern (e.g., "shutdown" matches "shutdown --force").
    fn cmd_matches(cmd: &[u8], pattern: &[u8]) -> bool {
        if cmd.len() < pattern.len() { return false; }
        if &cmd[..pattern.len()] != pattern { return false; }
        // Must be exact match or followed by space
        cmd.len() == pattern.len() || cmd[pattern.len()] == b' '
    }

    /// Get current policy version number.
    pub fn policy_version(&self) -> u16 {
        self.policy_ver
    }
}

// =============================================================================
// FFI Exports — called from C kernel
// =============================================================================

/// Create a new AI engine instance. Returns an opaque pointer.
#[no_mangle]
pub extern "C" fn ai_engine_new() -> *mut AiEngine {
    let engine = Box::new(AiEngine::new());
    Box::into_raw(engine)
}

/// Feed an event into the AI engine.
/// `event_type` and `data` are C strings (pointer + length).
#[no_mangle]
pub unsafe extern "C" fn ai_engine_feed_event(
    engine: *mut AiEngine,
    event_type: *const u8, event_type_len: usize,
    data: *const u8, data_len: usize,
    now_ticks: u64,
) {
    if engine.is_null() { return; }
    let eng = &mut *engine;

    let evt_str = core::str::from_utf8_unchecked(
        core::slice::from_raw_parts(event_type, event_type_len)
    );
    let data_str = core::str::from_utf8_unchecked(
        core::slice::from_raw_parts(data, data_len)
    );

    eng.feed_event(evt_str, data_str, now_ticks);
}

/// Feed event using numeric code — avoids string parsing overhead (Phase 15)
#[no_mangle]
pub unsafe extern "C" fn ai_engine_feed_event_code(
    engine: *mut AiEngine,
    event_code: u8,
    data: *const u8, data_len: usize,
    now_ticks: u64,
) {
    if engine.is_null() { return; }
    let eng = &mut *engine;
    let data_str = core::str::from_utf8_unchecked(
        core::slice::from_raw_parts(data, data_len)
    );
    eng.feed_event_code(event_code, data_str, now_ticks);
}

/// Periodic tick — call from timer IRQ every 50 ticks (~500ms).
#[no_mangle]
pub unsafe extern "C" fn ai_engine_tick(engine: *mut AiEngine, now_ticks: u64) {
    if engine.is_null() { return; }
    let eng = &mut *engine;
    eng.tick(now_ticks);
}

/// Register a tune callback (scheduler quantum/priority adjustments).
#[no_mangle]
pub unsafe extern "C" fn ai_engine_set_tune_cb(engine: *mut AiEngine, cb: TuneCallback) {
    if engine.is_null() { return; }
    let eng = &mut *engine;
    eng.set_tune_callback(cb);
}

/// Register a remediate callback (kill/throttle/revoke actions).
#[no_mangle]
pub unsafe extern "C" fn ai_engine_set_remediate_cb(engine: *mut AiEngine, cb: RemediateCallback) {
    if engine.is_null() { return; }
    let eng = &mut *engine;
    eng.set_remediate_callback(cb);
}

/// Get event store total recorded count.
#[no_mangle]
pub unsafe extern "C" fn ai_engine_event_count(engine: *const AiEngine) -> u64 {
    if engine.is_null() { return 0; }
    let eng = &*engine;
    eng.event_store.total_recorded()
}

/// Get anomaly count.
#[no_mangle]
pub unsafe extern "C" fn ai_engine_anomaly_count(engine: *const AiEngine) -> u32 {
    if engine.is_null() { return 0; }
    let eng = &*engine;
    eng.anomaly_detector.anomaly_count() as u32
}

/// Get active process count.
#[no_mangle]
pub unsafe extern "C" fn ai_engine_active_procs(engine: *const AiEngine) -> u32 {
    if engine.is_null() { return 0; }
    let eng = &*engine;
    eng.process_tracker.active_count()
}

/// Get auto-tuner decision count.
#[no_mangle]
pub unsafe extern "C" fn ai_engine_decision_count(engine: *const AiEngine) -> u32 {
    if engine.is_null() { return 0; }
    let eng = &*engine;
    eng.auto_tuner.decision_count() as u32
}

/// Free the AI engine (unlikely to be called in kernel, but good practice).
#[no_mangle]
pub unsafe extern "C" fn ai_engine_free(engine: *mut AiEngine) {
    if !engine.is_null() {
        drop(Box::from_raw(engine));
    }
}

/// Load a binary policy blob into the AI engine.
/// Returns 1 on success, 0 on failure (bad magic/version/format).
#[no_mangle]
pub unsafe extern "C" fn ai_engine_load_policy(
    engine: *mut AiEngine,
    blob: *const u8,
    blob_len: usize,
) -> u32 {
    if engine.is_null() || blob.is_null() || blob_len < policy::HEADER_SIZE {
        return 0;
    }
    let eng = &mut *engine;
    let data = core::slice::from_raw_parts(blob, blob_len);

    match policy::parse_policy_blob(data) {
        Some(cfg) => {
            let ver = cfg.version;
            if eng.load_policy(cfg) {
                eng.policy_ver = ver;
                1
            } else {
                0
            }
        }
        None => 0,
    }
}

/// Get the currently loaded policy version.
#[no_mangle]
pub unsafe extern "C" fn ai_engine_policy_version(engine: *const AiEngine) -> u16 {
    if engine.is_null() { return 0; }
    let eng = &*engine;
    eng.policy_version()
}

/// Check access for a command at a given privilege level.
/// Returns: required privilege (0=root, 50=admin, 100=user), or 255 if no rule.
#[no_mangle]
pub unsafe extern "C" fn ai_engine_check_access(
    engine: *const AiEngine,
    command: *const u8,
    command_len: usize,
) -> u8 {
    if engine.is_null() || command.is_null() || command_len == 0 {
        return 255;
    }
    let eng = &*engine;
    let cmd_str = core::str::from_utf8_unchecked(
        core::slice::from_raw_parts(command, command_len)
    );
    eng.check_access(cmd_str)
}

/// Get the number of loaded access rules.
#[no_mangle]
pub unsafe extern "C" fn ai_engine_access_rule_count(engine: *const AiEngine) -> u32 {
    if engine.is_null() { return 0; }
    let eng = &*engine;
    eng.access_rules.len() as u32
}
