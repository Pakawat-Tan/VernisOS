// PS/2 Mouse driver — tracks position and button state
// Called from C IRQ12 handler via FFI

struct MouseState {
    x: i32,
    y: i32,
    buttons: u8, // bit 0=left, bit 1=right, bit 2=middle
    screen_w: i32,
    screen_h: i32,
    initialized: bool,
}

static mut MOUSE: MouseState = MouseState {
    x: 0,
    y: 0,
    buttons: 0,
    screen_w: 0,
    screen_h: 0,
    initialized: false,
};

#[no_mangle]
pub unsafe extern "C" fn mouse_init(screen_w: u32, screen_h: u32) {
    MOUSE.screen_w = screen_w as i32;
    MOUSE.screen_h = screen_h as i32;
    MOUSE.x = (screen_w / 2) as i32;
    MOUSE.y = (screen_h / 2) as i32;
    MOUSE.buttons = 0;
    MOUSE.initialized = true;
}

/// Called from C IRQ12 handler with the 3-byte PS/2 mouse packet.
/// flags: byte 0 of packet (buttons + sign + overflow bits)
/// dx: byte 1 (X movement, sign-extended)
/// dy: byte 2 (Y movement, sign-extended)
#[no_mangle]
pub unsafe extern "C" fn mouse_handle_packet(flags: u8, dx: i8, dy: i8) {
    if !MOUSE.initialized {
        return;
    }

    // Update buttons from flags byte (bits 0-2)
    MOUSE.buttons = flags & 0x07;

    // Update position (PS/2 Y is inverted: positive = up)
    MOUSE.x += dx as i32;
    MOUSE.y -= dy as i32; // invert Y for screen coordinates

    // Clamp to screen bounds
    if MOUSE.x < 0 {
        MOUSE.x = 0;
    }
    if MOUSE.x >= MOUSE.screen_w {
        MOUSE.x = MOUSE.screen_w - 1;
    }
    if MOUSE.y < 0 {
        MOUSE.y = 0;
    }
    if MOUSE.y >= MOUSE.screen_h {
        MOUSE.y = MOUSE.screen_h - 1;
    }
}

#[no_mangle]
pub unsafe extern "C" fn mouse_get_x() -> i32 {
    MOUSE.x
}

#[no_mangle]
pub unsafe extern "C" fn mouse_get_y() -> i32 {
    MOUSE.y
}

#[no_mangle]
pub unsafe extern "C" fn mouse_get_buttons() -> u8 {
    MOUSE.buttons
}
