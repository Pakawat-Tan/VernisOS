// Terminal Widget — text-mode terminal emulator inside a GUI window

use crate::framebuffer::{FONT_WIDTH, FONT_HEIGHT};
use crate::gui::window;

const TERM_COLS: usize = 80;
const TERM_ROWS: usize = 40;
const TERM_INPUT_MAX: usize = 256;

// VGA 16-color palette → BGRA (used for terminal text colors)
const VGA_PALETTE: [u32; 16] = [
    0x000000, // 0  black
    0x0000AA, // 1  blue
    0x00AA00, // 2  green
    0x00AAAA, // 3  cyan
    0xAA0000, // 4  red
    0xAA00AA, // 5  magenta
    0xAA5500, // 6  brown
    0xAAAAAA, // 7  light grey
    0x555555, // 8  dark grey
    0x5555FF, // 9  light blue
    0x55FF55, // 10 light green
    0x55FFFF, // 11 light cyan
    0xFF5555, // 12 light red
    0xFF55FF, // 13 light magenta
    0xFFFF55, // 14 yellow
    0xFFFFFF, // 15 white
];

pub struct Terminal {
    pub window_id: u32,
    pub chars: [[u8; TERM_COLS]; TERM_ROWS],
    pub fg_colors: [[u8; TERM_COLS]; TERM_ROWS],
    pub bg_colors: [[u8; TERM_COLS]; TERM_ROWS],
    pub cursor_row: usize,
    pub cursor_col: usize,
    pub fg: u8,
    pub bg: u8,
    pub input_line: [u8; TERM_INPUT_MAX],
    pub input_len: usize,
    pub input_cursor: usize,
    pub initialized: bool,
    pub prompt_shown: bool,
    pub dirty: bool,               // ANY row dirty = trigger render pass
    pub dirty_rows: [bool; TERM_ROWS],  // per-row dirty tracking (fine-grain)
    pub prev_cursor_row: usize,    // previous cursor position for partial redraw
    pub prev_cursor_col: usize,
}

static mut TERM: Terminal = Terminal {
    window_id: 0,
    chars: [[b' '; TERM_COLS]; TERM_ROWS],
    fg_colors: [[7; TERM_COLS]; TERM_ROWS],
    bg_colors: [[0; TERM_COLS]; TERM_ROWS],
    cursor_row: 0,
    cursor_col: 0,
    fg: 7,
    bg: 0,
    input_line: [0; TERM_INPUT_MAX],
    input_len: 0,
    input_cursor: 0,
    initialized: false,
    prompt_shown: false,
    dirty: true,
    dirty_rows: [true; TERM_ROWS],  // All rows dirty initially
    prev_cursor_row: 0,
    prev_cursor_col: 0,
};

pub unsafe fn terminal_get() -> &'static mut Terminal {
    &mut TERM
}

pub unsafe fn terminal_create(x: i32, y: i32) -> u32 {
    // Calculate window size to fit TERM_COLS x TERM_ROWS characters
    let content_w = (TERM_COLS as u32) * FONT_WIDTH;
    let content_h = (TERM_ROWS as u32) * FONT_HEIGHT;
    let win_w = content_w + window::BORDER_WIDTH * 2;
    let win_h = content_h + window::TITLE_BAR_HEIGHT + window::BORDER_WIDTH;

    let id = window::wm_create_window(b"Terminal", x, y, win_w, win_h);
    TERM.window_id = id;
    TERM.cursor_row = 0;
    TERM.cursor_col = 0;
    TERM.fg = 7;  // light grey
    TERM.bg = 0;  // black
    TERM.input_len = 0;
    TERM.input_cursor = 0;
    TERM.initialized = true;
    TERM.prompt_shown = false;
    TERM.dirty = true;
    TERM.dirty_rows = [true; TERM_ROWS];  // All rows dirty initially
    TERM.prev_cursor_row = 0;
    TERM.prev_cursor_col = 0;

    // Clear terminal buffer
    for r in 0..TERM_ROWS {
        for c in 0..TERM_COLS {
            TERM.chars[r][c] = b' ';
            TERM.fg_colors[r][c] = 7;
            TERM.bg_colors[r][c] = 0;
        }
    }

    // Fill window content with black
    window::wm_window_fill(id, 0x000000);

    id
}

unsafe fn terminal_scroll() {
    // Move all rows up by 1
    for r in 0..TERM_ROWS - 1 {
        TERM.chars[r] = TERM.chars[r + 1];
        TERM.fg_colors[r] = TERM.fg_colors[r + 1];
        TERM.bg_colors[r] = TERM.bg_colors[r + 1];
        TERM.dirty_rows[r] = true;  // All rows dirty after scroll
    }
    // Clear last row
    let last = TERM_ROWS - 1;
    for c in 0..TERM_COLS {
        TERM.chars[last][c] = b' ';
        TERM.fg_colors[last][c] = TERM.fg;
        TERM.bg_colors[last][c] = TERM.bg;
    }
    TERM.dirty_rows[last] = true;
}

pub unsafe fn terminal_putchar(ch: u8) {
    if !TERM.initialized {
        return;
    }
    TERM.dirty = true;
    match ch {
        b'\n' => {
            TERM.dirty_rows[TERM.cursor_row] = true;
            TERM.cursor_col = 0;
            TERM.cursor_row += 1;
            if TERM.cursor_row >= TERM_ROWS {
                terminal_scroll();
                TERM.cursor_row = TERM_ROWS - 1;
            }
            TERM.dirty_rows[TERM.cursor_row] = true;
        }
        b'\r' => {
            TERM.dirty_rows[TERM.cursor_row] = true;
            TERM.cursor_col = 0;
        }
        8 | 127 => {
            // Backspace
            TERM.dirty_rows[TERM.cursor_row] = true;
            if TERM.cursor_col > 0 {
                TERM.cursor_col -= 1;
                TERM.chars[TERM.cursor_row][TERM.cursor_col] = b' ';
            }
        }
        _ => {
            if TERM.cursor_col >= TERM_COLS {
                TERM.dirty_rows[TERM.cursor_row] = true;
                TERM.cursor_col = 0;
                TERM.cursor_row += 1;
                if TERM.cursor_row >= TERM_ROWS {
                    terminal_scroll();
                    TERM.cursor_row = TERM_ROWS - 1;
                }
            }
            TERM.chars[TERM.cursor_row][TERM.cursor_col] = ch;
            TERM.fg_colors[TERM.cursor_row][TERM.cursor_col] = TERM.fg;
            TERM.bg_colors[TERM.cursor_row][TERM.cursor_col] = TERM.bg;
            TERM.dirty_rows[TERM.cursor_row] = true;
            TERM.cursor_col += 1;
        }
    }
}

pub unsafe fn terminal_write_string(s: &[u8]) {
    for &ch in s {
        if ch == 0 {
            break;
        }
        terminal_putchar(ch);
    }
}

pub unsafe fn terminal_set_color(fg: u8, bg: u8) {
    TERM.fg = fg & 0x0F;
    TERM.bg = bg & 0x0F;
}

/// Render the terminal buffer into the window's content buffer.
/// Only re-renders when content has changed.
pub unsafe fn terminal_render() {
    if !TERM.initialized || !TERM.dirty {
        return;
    }
    
    // Check if any row is dirty
    let mut any_dirty = false;
    for row in 0..TERM_ROWS {
        if TERM.dirty_rows[row] {
            any_dirty = true;
            break;
        }
    }
    
    if !any_dirty {
        TERM.dirty = false;
        return;
    }
    
    let id = TERM.window_id;
    
    // Use fast batch rendering: only one find_window() call per row instead of 80!
    for row in 0..TERM_ROWS {
        if !TERM.dirty_rows[row] {
            continue;
        }
        
        // Batch render entire row (80 chars) in one call + single find_window()
        let mut chars = [0u8; TERM_COLS];
        let mut fgs = [0u32; TERM_COLS];
        let mut bgs = [0u32; TERM_COLS];
        
        for col in 0..TERM_COLS {
            chars[col] = TERM.chars[row][col];
            fgs[col] = VGA_PALETTE[TERM.fg_colors[row][col] as usize & 0x0F];
            bgs[col] = VGA_PALETTE[TERM.bg_colors[row][col] as usize & 0x0F];
        }
        
        // Fast path: one window lookup for 80 characters
        window::wm_render_rows_direct(id, row as u32, &chars, &fgs, &bgs);
        
        TERM.dirty_rows[row] = false;
    }
    
    // Draw cursor
    if TERM.cursor_row < TERM_ROWS && TERM.cursor_col < TERM_COLS {
        let cx = (TERM.cursor_col as u32) * FONT_WIDTH;
        let cy = (TERM.cursor_row as u32) * FONT_HEIGHT;
        window::wm_window_fill_rect(id, cx, cy, FONT_WIDTH, FONT_HEIGHT, 0xAAAAAAu32);
    }
    
    TERM.dirty = false;
}

/// Show the shell prompt.
pub unsafe fn terminal_show_prompt() {
    terminal_set_color(10, 0); // light green
    terminal_write_string(b"root@vernisOS");
    terminal_set_color(7, 0);  // light grey
    terminal_write_string(b":~$ ");
    TERM.prompt_shown = true;
}

// Scancode to ASCII conversion (simplified US QWERTY)
const SCANCODE_TABLE: [u8; 58] = [
    0, 27, b'1', b'2', b'3', b'4', b'5', b'6', b'7', b'8', b'9', b'0', b'-', b'=', 8,
    b'\t', b'q', b'w', b'e', b'r', b't', b'y', b'u', b'i', b'o', b'p', b'[', b']', b'\n',
    0, b'a', b's', b'd', b'f', b'g', b'h', b'j', b'k', b'l', b';', b'\'', b'`',
    0, b'\\', b'z', b'x', b'c', b'v', b'b', b'n', b'm', b',', b'.', b'/', 0,
    b'*', 0, b' ',
];

const SCANCODE_TABLE_SHIFT: [u8; 58] = [
    0, 27, b'!', b'@', b'#', b'$', b'%', b'^', b'&', b'*', b'(', b')', b'_', b'+', 8,
    b'\t', b'Q', b'W', b'E', b'R', b'T', b'Y', b'U', b'I', b'O', b'P', b'{', b'}', b'\n',
    0, b'A', b'S', b'D', b'F', b'G', b'H', b'J', b'K', b'L', b':', b'"', b'~',
    0, b'|', b'Z', b'X', b'C', b'V', b'B', b'N', b'M', b'<', b'>', b'?', 0,
    b'*', 0, b' ',
];

static mut SHIFT_HELD: bool = false;

extern "C" {
    fn cli_process_line_gui(line: *const u8, len: u32);
    fn cli_gui_ps_handle_key(ch: u8) -> u8;
}

/// Handle a key scancode for the terminal.
pub unsafe fn terminal_handle_key(scancode: u8) {
    // Handle shift key tracking
    if scancode == 0x2A || scancode == 0x36 {
        SHIFT_HELD = true;
        return;
    }
    if scancode == 0xAA || scancode == 0xB6 {
        SHIFT_HELD = false;
        return;
    }

    // Ignore key releases (high bit set)
    if scancode & 0x80 != 0 {
        return;
    }

    let ascii = if (scancode as usize) < SCANCODE_TABLE.len() {
        if SHIFT_HELD {
            SCANCODE_TABLE_SHIFT[scancode as usize]
        } else {
            SCANCODE_TABLE[scancode as usize]
        }
    } else {
        0
    };

    if ascii == 0 {
        return;
    }

    // If ps realtime monitor is active, let C-side monitor consume keys
    // (q/Esc to stop) and suppress regular terminal echo/edit behavior.
    if cli_gui_ps_handle_key(ascii) != 0 {
        return;
    }

    if !TERM.prompt_shown {
        terminal_show_prompt();
    }

    match ascii {
        b'\n' => {
            terminal_putchar(b'\n');
            // Process the input line
            if TERM.input_len > 0 {
                // Call into C CLI processor
                cli_process_line_gui(TERM.input_line.as_ptr(), TERM.input_len as u32);
            }
            TERM.input_len = 0;
            TERM.input_cursor = 0;
            terminal_show_prompt();
        }
        8 => {
            // Backspace
            if TERM.input_len > 0 {
                TERM.input_len -= 1;
                TERM.input_cursor -= 1;
                terminal_putchar(8);
            }
        }
        _ => {
            if TERM.input_len < TERM_INPUT_MAX - 1 {
                TERM.input_line[TERM.input_len] = ascii;
                TERM.input_len += 1;
                TERM.input_cursor += 1;
                terminal_putchar(ascii);
            }
        }
    }
}

/// C FFI: terminal output callback for print functions.
#[no_mangle]
pub unsafe extern "C" fn gui_terminal_putchar(ch: u8) {
    terminal_putchar(ch);
}

/// C FFI: clear the GUI terminal (called by `clear` command).
#[no_mangle]
pub unsafe extern "C" fn gui_terminal_clear() {
    if !TERM.initialized {
        return;
    }
    // Clear all character cells
    for r in 0..TERM_ROWS {
        for c in 0..TERM_COLS {
            TERM.chars[r][c] = b' ';
            TERM.fg_colors[r][c] = 7;
            TERM.bg_colors[r][c] = 0;
        }
        TERM.dirty_rows[r] = true;  // Mark all rows dirty
    }
    TERM.cursor_row = 0;
    TERM.cursor_col = 0;
    TERM.dirty = true;
}

/// Get the terminal window ID.
pub unsafe fn terminal_window_id() -> u32 {
    TERM.window_id
}
