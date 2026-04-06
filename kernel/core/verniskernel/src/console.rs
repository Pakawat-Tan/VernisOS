// Text console on top of framebuffer
// Tracks cursor position, handles control characters, VGA color translation

use crate::framebuffer::{
    fb_clear, fb_draw_char, fb_fill_rect, fb_scroll,
    FONT_HEIGHT, FONT_WIDTH,
};

/// VGA 16-color palette → 32-bit BGRA
const VGA_PALETTE: [u32; 16] = [
    0x00000000, // 0: black
    0x00AA0000, // 1: blue       (BGRA: B=AA, G=00, R=00)
    0x0000AA00, // 2: green      (BGRA: B=00, G=AA, R=00)
    0x00AAAA00, // 3: cyan
    0x000000AA, // 4: red        (BGRA: B=00, G=00, R=AA)
    0x00AA00AA, // 5: magenta
    0x000055AA, // 6: brown
    0x00AAAAAA, // 7: light grey
    0x00555555, // 8: dark grey
    0x00FF5555, // 9: light blue
    0x0055FF55, // 10: light green
    0x00FFFF55, // 11: light cyan
    0x005555FF, // 12: light red
    0x00FF55FF, // 13: light magenta
    0x0055FFFF, // 14: yellow
    0x00FFFFFF, // 15: white
];

struct Console {
    cursor_row: u32,
    cursor_col: u32,
    max_rows: u32,
    max_cols: u32,
    fg_color: u32,
    bg_color: u32,
    initialized: bool,
}

static mut CONSOLE: Console = Console {
    cursor_row: 0,
    cursor_col: 0,
    max_rows: 0,
    max_cols: 0,
    fg_color: 0x00AAAAAA, // light grey
    bg_color: 0x00000000, // black
    initialized: false,
};

#[no_mangle]
pub unsafe extern "C" fn console_init(width: u32, height: u32) {
    CONSOLE.max_cols = width / FONT_WIDTH;
    CONSOLE.max_rows = height / FONT_HEIGHT;
    CONSOLE.cursor_row = 0;
    CONSOLE.cursor_col = 0;
    CONSOLE.fg_color = VGA_PALETTE[15]; // white
    CONSOLE.bg_color = VGA_PALETTE[0];  // black
    CONSOLE.initialized = true;
    fb_clear(CONSOLE.bg_color);
}

#[no_mangle]
pub unsafe extern "C" fn console_putchar(c: u8) {
    if !CONSOLE.initialized {
        return;
    }
    match c {
        b'\n' => {
            CONSOLE.cursor_col = 0;
            CONSOLE.cursor_row += 1;
        }
        b'\r' => {
            CONSOLE.cursor_col = 0;
        }
        0x08 => {
            // Backspace
            if CONSOLE.cursor_col > 0 {
                CONSOLE.cursor_col -= 1;
                let px = CONSOLE.cursor_col * FONT_WIDTH;
                let py = CONSOLE.cursor_row * FONT_HEIGHT;
                fb_fill_rect(px, py, FONT_WIDTH, FONT_HEIGHT, CONSOLE.bg_color);
            }
        }
        _ => {
            let px = CONSOLE.cursor_col * FONT_WIDTH;
            let py = CONSOLE.cursor_row * FONT_HEIGHT;
            fb_draw_char(px, py, c, CONSOLE.fg_color, CONSOLE.bg_color);
            CONSOLE.cursor_col += 1;
            if CONSOLE.cursor_col >= CONSOLE.max_cols {
                CONSOLE.cursor_col = 0;
                CONSOLE.cursor_row += 1;
            }
        }
    }

    // Scroll if needed
    if CONSOLE.cursor_row >= CONSOLE.max_rows {
        fb_scroll(1);
        CONSOLE.cursor_row = CONSOLE.max_rows - 1;
    }
}

#[no_mangle]
pub unsafe extern "C" fn console_writestring(s: *const u8, len: u32) {
    if s.is_null() {
        return;
    }
    for i in 0..len {
        console_putchar(*s.add(i as usize));
    }
}

#[no_mangle]
pub unsafe extern "C" fn console_clear() {
    if !CONSOLE.initialized {
        return;
    }
    fb_clear(CONSOLE.bg_color);
    CONSOLE.cursor_row = 0;
    CONSOLE.cursor_col = 0;
}

#[no_mangle]
pub unsafe extern "C" fn console_set_color(fg: u32, bg: u32) {
    CONSOLE.fg_color = fg;
    CONSOLE.bg_color = bg;
}

#[no_mangle]
pub unsafe extern "C" fn console_set_color_vga(fg_index: u8, bg_index: u8) {
    if (fg_index as usize) < VGA_PALETTE.len() {
        CONSOLE.fg_color = VGA_PALETTE[fg_index as usize];
    }
    if (bg_index as usize) < VGA_PALETTE.len() {
        CONSOLE.bg_color = VGA_PALETTE[bg_index as usize];
    }
}

#[no_mangle]
pub unsafe extern "C" fn console_set_pos(row: u32, col: u32) {
    CONSOLE.cursor_row = row;
    CONSOLE.cursor_col = col;
}

#[no_mangle]
pub unsafe extern "C" fn console_get_pos(row: *mut u32, col: *mut u32) {
    if !row.is_null() {
        *row = CONSOLE.cursor_row;
    }
    if !col.is_null() {
        *col = CONSOLE.cursor_col;
    }
}

#[no_mangle]
pub unsafe extern "C" fn console_clear_to_eol(row: u32, col: u32) {
    if !CONSOLE.initialized {
        return;
    }
    let px = col * FONT_WIDTH;
    let py = row * FONT_HEIGHT;
    let remaining_width = CONSOLE.max_cols.saturating_sub(col) * FONT_WIDTH;
    fb_fill_rect(px, py, remaining_width, FONT_HEIGHT, CONSOLE.bg_color);
}
