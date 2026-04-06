// event_store.rs — Fixed ring buffer event history with query API (Phase 15)
//
// Port of ai/event_store.py to Rust no_std.
// Uses a fixed-size array ring buffer — zero heap allocation for the buffer itself.

use alloc::vec::Vec;
use alloc::string::String;
use super::types::*;

const RING_CAPACITY: usize = 256;

pub struct EventStore {
    events: [Option<EventRecord>; RING_CAPACITY],
    head: usize,       // next write position
    count: usize,      // current number of events
    total_recorded: u64,
}

impl EventStore {
    pub fn new(_capacity: usize) -> Self {
        const NONE: Option<EventRecord> = None;
        Self {
            events: [NONE; RING_CAPACITY],
            head: 0,
            count: 0,
            total_recorded: 0,
        }
    }

    pub fn record(&mut self, event_type: EventType, data: &str, source_pid: u32, now: u64) {
        let record = EventRecord {
            timestamp: KernelInstant::from_ticks(now),
            event_type,
            data: String::from(data),
            source_pid,
        };

        self.events[self.head] = Some(record);
        self.head = (self.head + 1) % RING_CAPACITY;
        if self.count < RING_CAPACITY {
            self.count += 1;
        }
        self.total_recorded += 1;
    }

    pub fn total_recorded(&self) -> u64 {
        self.total_recorded
    }

    pub fn len(&self) -> usize {
        self.count
    }

    /// Return the last N events (newest first).
    pub fn query_recent(&self, last_n: usize) -> Vec<&EventRecord> {
        let n = last_n.min(self.count);
        let mut result = Vec::with_capacity(n);
        for i in 0..n {
            let idx = (self.head + RING_CAPACITY - 1 - i) % RING_CAPACITY;
            if let Some(ref e) = self.events[idx] {
                result.push(e);
            }
        }
        result
    }

    /// Return events of a specific type within the last `window_ticks`.
    pub fn query_by_type(&self, evt_type: EventType, now: u64, window_ticks: u64) -> Vec<&EventRecord> {
        let cutoff = now.saturating_sub(window_ticks);
        self.iter_events()
            .filter(|e| e.event_type == evt_type && e.timestamp.ticks() >= cutoff)
            .collect()
    }

    /// Return events from a specific PID within the last `window_ticks`.
    pub fn query_by_pid(&self, pid: u32, now: u64, window_ticks: u64) -> Vec<&EventRecord> {
        let cutoff = now.saturating_sub(window_ticks);
        self.iter_events()
            .filter(|e| e.source_pid == pid && e.timestamp.ticks() >= cutoff)
            .collect()
    }

    /// Count events by type within the last `window_ticks`.
    pub fn count_by_type(&self, now: u64, window_ticks: u64) -> Vec<(EventType, u32)> {
        let cutoff = now.saturating_sub(window_ticks);
        let mut counts = [0u32; 9];

        for e in self.iter_events() {
            if e.timestamp.ticks() >= cutoff {
                let idx = match e.event_type {
                    EventType::Boot => 0,
                    EventType::Stat => 1,
                    EventType::Exception => 2,
                    EventType::Process => 3,
                    EventType::Module => 4,
                    EventType::Deny => 5,
                    EventType::Fail => 6,
                    EventType::Syscall => 7,
                    EventType::Unknown => 8,
                };
                counts[idx] += 1;
            }
        }

        let types = [
            EventType::Boot, EventType::Stat, EventType::Exception,
            EventType::Process, EventType::Module, EventType::Deny,
            EventType::Fail, EventType::Syscall, EventType::Unknown,
        ];
        types.iter().zip(counts.iter())
            .filter(|(_, &c)| c > 0)
            .map(|(&t, &c)| (t, c))
            .collect()
    }

    /// Extract PID from event data based on event type.
    pub fn extract_pid(event_type: EventType, data: &str) -> u32 {
        let mut fields = [""; 4];
        parse_pipe_fields(data, &mut fields);
        match event_type {
            EventType::Process | EventType::Deny | EventType::Syscall => {
                parse_u32(fields[0])
            }
            EventType::Exception => {
                parse_u32(fields[2])
            }
            _ => 0,
        }
    }

    /// Iterate over stored events from oldest to newest.
    fn iter_events(&self) -> impl Iterator<Item = &EventRecord> {
        let start = if self.count < RING_CAPACITY { 0 } else { self.head };
        let count = self.count;
        (0..count).filter_map(move |i| {
            let idx = (start + i) % RING_CAPACITY;
            self.events[idx].as_ref()
        })
    }
}
