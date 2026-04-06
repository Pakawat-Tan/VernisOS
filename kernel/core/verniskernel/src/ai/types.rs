// types.rs — Shared types for the in-kernel AI engine
//
// All types are no_std compatible and use alloc where needed.

use alloc::string::String;

// =============================================================================
// Kernel time — monotonic tick counter provided by C timer IRQ
// =============================================================================

/// Monotonic timestamp based on kernel tick counter (10ms per tick at 100Hz PIT).
#[derive(Debug, Clone, Copy, PartialEq, Eq, PartialOrd, Ord)]
pub struct KernelInstant(pub u64);

impl KernelInstant {
    pub fn from_ticks(ticks: u64) -> Self {
        Self(ticks)
    }

    pub fn ticks(self) -> u64 {
        self.0
    }

    /// Seconds elapsed since this instant, given current tick count.
    pub fn elapsed_secs(self, now: u64) -> f32 {
        if now >= self.0 {
            (now - self.0) as f32 / 100.0 // 100 ticks per second
        } else {
            0.0
        }
    }

    /// Milliseconds elapsed since this instant.
    pub fn elapsed_ms(self, now: u64) -> u64 {
        if now >= self.0 {
            (now - self.0) * 10 // 10ms per tick
        } else {
            0
        }
    }
}

// =============================================================================
// Event types — mirrors AI_EVT_* from ai_bridge.h
// =============================================================================

#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum EventType {
    Boot,
    Stat,
    Exception,
    Process,
    Module,
    Deny,
    Fail,
    Syscall,
    Unknown,
}

impl EventType {
    pub fn from_str(s: &str) -> Self {
        match s {
            "BOOT" => Self::Boot,
            "STAT" => Self::Stat,
            "EXCP" => Self::Exception,
            "PROC" => Self::Process,
            "MOD"  => Self::Module,
            "DENY" => Self::Deny,
            "FAIL" => Self::Fail,
            "SYSCALL" => Self::Syscall,
            _ => Self::Unknown,
        }
    }

    /// O(1) conversion from numeric event code (Phase 15 optimization)
    pub fn from_u8(code: u8) -> Self {
        match code {
            1 => Self::Boot,
            2 => Self::Stat,
            3 => Self::Exception,
            4 => Self::Process,
            5 => Self::Module,
            6 => Self::Deny,
            7 => Self::Fail,
            8 => Self::Syscall,
            _ => Self::Unknown,
        }
    }

    pub fn as_str(&self) -> &'static str {
        match self {
            Self::Boot => "BOOT",
            Self::Stat => "STAT",
            Self::Exception => "EXCP",
            Self::Process => "PROC",
            Self::Module => "MOD",
            Self::Deny => "DENY",
            Self::Fail => "FAIL",
            Self::Syscall => "SYSCALL",
            Self::Unknown => "UNK",
        }
    }
}

// =============================================================================
// Severity levels
// =============================================================================

#[derive(Debug, Clone, Copy, PartialEq, Eq, PartialOrd, Ord)]
pub enum Severity {
    Low = 0,
    Medium = 1,
    High = 2,
    Critical = 3,
}

// =============================================================================
// Anomaly — output of anomaly detection
// =============================================================================

#[derive(Debug, Clone)]
pub struct Anomaly {
    pub detector: DetectorKind,
    pub severity: Severity,
    pub title: String,
    pub detail: String,
    pub timestamp: KernelInstant,
    pub source_pid: u32,
}

#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum DetectorKind {
    Rate,
    Pattern,
    Threshold,
}

impl DetectorKind {
    pub fn as_str(&self) -> &'static str {
        match self {
            Self::Rate => "rate",
            Self::Pattern => "pattern",
            Self::Threshold => "threshold",
        }
    }
}

// =============================================================================
// Trust levels for process tracking
// =============================================================================

#[derive(Debug, Clone, Copy, PartialEq, Eq, PartialOrd, Ord)]
pub enum TrustLevel {
    Trusted = 3,
    Normal = 2,
    Suspicious = 1,
    Untrusted = 0,
}

// =============================================================================
// Load levels for auto-tuner
// =============================================================================

#[derive(Debug, Clone, Copy, PartialEq, Eq, PartialOrd, Ord)]
pub enum LoadLevel {
    Low = 0,
    Normal = 1,
    High = 2,
    Critical = 3,
}

// =============================================================================
// Tuning decision — output of auto-tuner
// =============================================================================

#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum TuneAction {
    SchedQuantum,
    SchedPrio,
    MemPressure,
    Throttle,
    Idle,
}

impl TuneAction {
    pub fn as_str(&self) -> &'static str {
        match self {
            Self::SchedQuantum => "SCHED_QUANTUM",
            Self::SchedPrio => "SCHED_PRIO",
            Self::MemPressure => "MEM_PRESSURE",
            Self::Throttle => "THROTTLE",
            Self::Idle => "IDLE",
        }
    }
}

#[derive(Debug, Clone)]
pub struct TuningDecision {
    pub action: TuneAction,
    pub target: String,
    pub value: u32,
    pub load: LoadLevel,
    pub timestamp: KernelInstant,
}

// =============================================================================
// Remediation actions
// =============================================================================

#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum RemediationAction {
    Log,
    Throttle,
    Kill,
    Revoke,
    Suspend,
}

impl RemediationAction {
    pub fn as_str(&self) -> &'static str {
        match self {
            Self::Log => "log",
            Self::Throttle => "throttle",
            Self::Kill => "kill",
            Self::Revoke => "revoke",
            Self::Suspend => "suspend",
        }
    }
}

// =============================================================================
// Stored event record
// =============================================================================

#[derive(Debug, Clone)]
pub struct EventRecord {
    pub timestamp: KernelInstant,
    pub event_type: EventType,
    pub data: String,
    pub source_pid: u32,
}

// =============================================================================
// Pipe-separated data parser helper
// =============================================================================

/// Split a pipe-separated string into up to N fields.
/// Returns a fixed-size array of &str slices (empty string for missing fields).
pub fn parse_pipe_fields<'a>(data: &'a str, buf: &mut [&'a str]) {
    let mut idx = 0;
    for field in data.split('|') {
        if idx >= buf.len() {
            break;
        }
        buf[idx] = field;
        idx += 1;
    }
}

/// Parse a decimal number from a string slice. Returns 0 on failure.
pub fn parse_u32(s: &str) -> u32 {
    let mut result: u32 = 0;
    for b in s.bytes() {
        if b >= b'0' && b <= b'9' {
            result = result.saturating_mul(10).saturating_add((b - b'0') as u32);
        } else {
            break;
        }
    }
    result
}

/// Parse a decimal number from a string slice as f32.
pub fn parse_f32(s: &str) -> f32 {
    let mut integer: u32 = 0;
    let mut frac: u32 = 0;
    let mut frac_div: u32 = 1;
    let mut in_frac = false;

    for b in s.bytes() {
        if b == b'.' {
            in_frac = true;
            continue;
        }
        if b >= b'0' && b <= b'9' {
            if in_frac {
                frac = frac.saturating_mul(10).saturating_add((b - b'0') as u32);
                frac_div = frac_div.saturating_mul(10);
            } else {
                integer = integer.saturating_mul(10).saturating_add((b - b'0') as u32);
            }
        } else {
            break;
        }
    }
    integer as f32 + (frac as f32 / frac_div as f32)
}

// =============================================================================
// Callback types for C integration
// =============================================================================

/// Callback to apply a tuning decision (scheduler quantum, priority, etc.)
pub type TuneCallback = extern "C" fn(action: *const u8, action_len: usize,
                                       target: *const u8, target_len: usize,
                                       value: u32);

/// Callback to apply a remediation action (kill, throttle, etc.)
pub type RemediateCallback = extern "C" fn(action: *const u8, action_len: usize,
                                            pid: u32,
                                            param: u32);

/// Callback to print a log message
pub type LogCallback = extern "C" fn(msg: *const u8, msg_len: usize);

// =============================================================================
// Anomaly flags (bitmask for process tracker)
// =============================================================================

pub mod anomaly_flags {
    pub const FORK_BOMB: u32       = 1 << 0;
    pub const EXCEPTION_SPAM: u32  = 1 << 1;
    pub const DENIAL_SPAM: u32     = 1 << 2;
    pub const SYSCALL_FLOOD: u32   = 1 << 3;
    pub const MODULE_ABUSE: u32    = 1 << 4;
}

// =============================================================================
// Default configuration constants
// =============================================================================

pub mod config {
    use super::Severity;

    // Rate detector rules: (event_type_str, max_count, window_ticks, severity)
    pub const RATE_RULES: &[(&str, u32, u64, Severity)] = &[
        ("EXCP", 5, 1000, Severity::High),       // >5 exceptions in 10s
        ("PROC", 20, 500, Severity::Medium),      // >20 proc events in 5s
        ("DENY", 10, 500, Severity::Medium),      // >10 denials in 5s
        ("FAIL", 5, 1000, Severity::High),        // >5 failures in 10s
        ("MOD", 8, 1000, Severity::Medium),       // >8 module events in 10s
    ];

    // Pattern detector rules: (name, sequence, window_ticks, severity)
    pub const PATTERN_RULES: &[(&str, &[&str], u64, Severity)] = &[
        ("exception-storm", &["EXCP", "EXCP", "EXCP"], 300, Severity::Critical),
        ("bad-module", &["MOD", "EXCP"], 200, Severity::High),
        ("denial-cascade", &["DENY", "DENY", "DENY", "DENY"], 300, Severity::High),
        ("fork-bomb", &["PROC", "PROC", "PROC", "PROC", "PROC"], 200, Severity::Critical),
    ];

    // Threshold detector rules: (metric_name, max_value, severity)
    pub const THRESHOLD_RULES: &[(&str, f32, Severity)] = &[
        ("process_count", 32.0, Severity::Medium),
        ("process_count", 64.0, Severity::Critical),
        ("ipc_queue_len", 100.0, Severity::Medium),
        ("exception_rate", 5.0, Severity::High),
    ];

    // Process tracker thresholds
    pub const FAILURES_TO_SUSPICIOUS: u32 = 3;
    pub const FAILURES_TO_UNTRUSTED: u32 = 6;
    pub const DENIALS_TO_SUSPICIOUS: u32 = 2;
    pub const DENIALS_TO_UNTRUSTED: u32 = 5;
    pub const ANOMALIES_TO_SUSPICIOUS: u32 = 2;
    pub const ANOMALIES_TO_UNTRUSTED: u32 = 4;

    // Auto-tuner thresholds
    pub const HIGH_RATE: f32 = 20.0;
    pub const CRITICAL_RATE: f32 = 50.0;
    pub const HIGH_PROC_COUNT: u32 = 8;
    pub const CRITICAL_PROC_COUNT: u32 = 16;
    pub const DEFAULT_QUANTUM: u32 = 10;
    pub const TUNER_COOLDOWN_TICKS: u64 = 1500; // 15 seconds

    // Alert deduplicator
    pub const DEDUP_WINDOW_TICKS: u64 = 3000; // 30 seconds

    // Event store
    pub const EVENT_STORE_CAPACITY: usize = 2000;
    pub const EXITED_PROCESS_LIMIT: usize = 64;
}
