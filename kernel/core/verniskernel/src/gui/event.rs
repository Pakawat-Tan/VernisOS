// GUI Event System — unified ring buffer for keyboard + mouse events

const EVENT_QUEUE_SIZE: usize = 256;

#[derive(Copy, Clone)]
#[repr(u8)]
pub enum EventKind {
    None = 0,
    KeyPress = 1,
    KeyRelease = 2,
    MouseMove = 3,
    MouseDown = 4,
    MouseUp = 5,
}

#[derive(Copy, Clone)]
pub struct Event {
    pub kind: EventKind,
    pub scancode: u8,   // for key events
    pub button: u8,     // for mouse events (bit 0=left, 1=right, 2=middle)
    pub mouse_x: i32,
    pub mouse_y: i32,
}

impl Event {
    pub const fn empty() -> Self {
        Event {
            kind: EventKind::None,
            scancode: 0,
            button: 0,
            mouse_x: 0,
            mouse_y: 0,
        }
    }
}

struct EventQueue {
    buf: [Event; EVENT_QUEUE_SIZE],
    read_pos: usize,
    write_pos: usize,
}

static mut QUEUE: EventQueue = EventQueue {
    buf: [Event {
        kind: EventKind::None,
        scancode: 0,
        button: 0,
        mouse_x: 0,
        mouse_y: 0,
    }; EVENT_QUEUE_SIZE],
    read_pos: 0,
    write_pos: 0,
};

// Coalesce mouse-move storms: keep only the latest cursor position.
static mut PENDING_MOUSE_MOVE: Option<(i32, i32)> = None;

pub unsafe fn event_push(ev: Event) {
    let next = (QUEUE.write_pos + 1) % EVENT_QUEUE_SIZE;
    if next == QUEUE.read_pos {
        return; // queue full, drop event
    }
    QUEUE.buf[QUEUE.write_pos] = ev;
    QUEUE.write_pos = next;
}

pub unsafe fn event_pop() -> Option<Event> {
    if QUEUE.read_pos == QUEUE.write_pos {
        if let Some((x, y)) = PENDING_MOUSE_MOVE.take() {
            return Some(Event {
                kind: EventKind::MouseMove,
                scancode: 0,
                button: 0,
                mouse_x: x,
                mouse_y: y,
            });
        }
        return None;
    }
    let ev = QUEUE.buf[QUEUE.read_pos];
    QUEUE.read_pos = (QUEUE.read_pos + 1) % EVENT_QUEUE_SIZE;
    Some(ev)
}

// C FFI wrappers — called from interrupt handlers

#[no_mangle]
pub unsafe extern "C" fn gui_push_key_event(scancode: u8, pressed: u8) {
    let kind = if pressed != 0 {
        EventKind::KeyPress
    } else {
        EventKind::KeyRelease
    };
    event_push(Event {
        kind,
        scancode,
        button: 0,
        mouse_x: 0,
        mouse_y: 0,
    });
}

#[no_mangle]
pub unsafe extern "C" fn gui_push_mouse_move(x: i32, y: i32) {
    PENDING_MOUSE_MOVE = Some((x, y));
}

#[no_mangle]
pub unsafe extern "C" fn gui_push_mouse_button(x: i32, y: i32, button: u8, pressed: u8) {
    let kind = if pressed != 0 {
        EventKind::MouseDown
    } else {
        EventKind::MouseUp
    };
    event_push(Event {
        kind,
        scancode: 0,
        button,
        mouse_x: x,
        mouse_y: y,
    });
}
