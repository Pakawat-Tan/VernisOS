// Window Manager — manages windows, z-ordering, focus, drag

use alloc::vec::Vec;
use core::ptr;
use crate::framebuffer::{FONT_WIDTH, FONT_HEIGHT, write_pixel_buf};
use crate::gui::compositor;

pub const TITLE_BAR_HEIGHT: u32 = 24;
pub const BORDER_WIDTH: u32 = 1;
pub const CLOSE_BTN_SIZE: u32 = 16;
pub const MAX_TITLE_LEN: usize = 63;

pub struct Window {
    pub id: u32,
    pub title: [u8; 64],
    pub title_len: usize,
    pub x: i32,
    pub y: i32,
    pub width: u32,    // total width including borders
    pub height: u32,   // total height including title bar + borders
    pub content_buf: Vec<u8>,
    pub content_pitch: u32,
    pub visible: bool,
    pub focused: bool,
}

impl Window {
    pub fn content_width(&self) -> u32 {
        self.width.saturating_sub(BORDER_WIDTH * 2)
    }

    pub fn content_height(&self) -> u32 {
        self.height.saturating_sub(TITLE_BAR_HEIGHT + BORDER_WIDTH)
    }

    pub fn content_x(&self) -> i32 {
        self.x + BORDER_WIDTH as i32
    }

    pub fn content_y(&self) -> i32 {
        self.y + TITLE_BAR_HEIGHT as i32
    }
}

pub struct WindowManager {
    pub windows: Vec<Window>,
    pub z_order: Vec<u32>,  // window IDs in z-order (last = topmost)
    pub next_id: u32,
    pub dragging: Option<u32>,
    pub drag_offset_x: i32,
    pub drag_offset_y: i32,
    pub bpp: u32,
    pub wins_dirty: bool,   // Set when window layout/visibility changes
}

static mut WM: WindowManager = WindowManager {
    windows: Vec::new(),
    z_order: Vec::new(),
    next_id: 1,
    dragging: None,
    drag_offset_x: 0,
    drag_offset_y: 0,
    bpp: 24,
    wins_dirty: true,  // Initially dirty (need full redraw)
};

pub unsafe fn wm_get() -> &'static mut WindowManager {
    &mut WM
}

pub unsafe fn wm_init(bpp: u32) {
    WM.windows = Vec::new();
    WM.z_order = Vec::new();
    WM.next_id = 1;
    WM.dragging = None;
    WM.bpp = bpp;
}

pub unsafe fn wm_create_window(title: &[u8], x: i32, y: i32, w: u32, h: u32) -> u32 {
    let id = WM.next_id;
    WM.next_id += 1;

    let mut title_buf = [0u8; 64];
    let len = if title.len() > MAX_TITLE_LEN {
        MAX_TITLE_LEN
    } else {
        title.len()
    };
    title_buf[..len].copy_from_slice(&title[..len]);

    let content_w = w.saturating_sub(BORDER_WIDTH * 2);
    let content_h = h.saturating_sub(TITLE_BAR_HEIGHT + BORDER_WIDTH);
    let bpp_bytes = WM.bpp / 8;
    let content_pitch = content_w * bpp_bytes;
    let buf_size = (content_pitch * content_h) as usize;

    let mut content_buf = Vec::with_capacity(buf_size);
    content_buf.resize(buf_size, 0);

    let win = Window {
        id,
        title: title_buf,
        title_len: len,
        x,
        y,
        width: w,
        height: h,
        content_buf,
        content_pitch,
        visible: true,
        focused: true,
    };

    // Unfocus all others
    for w in WM.windows.iter_mut() {
        w.focused = false;
    }

    WM.windows.push(win);
    WM.z_order.push(id);

    id
}

pub unsafe fn wm_close_window(id: u32) {
    WM.z_order.retain(|&wid| wid != id);
    WM.windows.retain(|w| w.id != id);
    if let Some(&top_id) = WM.z_order.last() {
        if let Some(w) = WM.windows.iter_mut().find(|w| w.id == top_id) {
            w.focused = true;
        }
    }
    WM.wins_dirty = true;  // Window list changed
}

pub unsafe fn wm_focus_window(id: u32) {
    // Move to top of z_order
    WM.z_order.retain(|&wid| wid != id);
    WM.z_order.push(id);
    for w in WM.windows.iter_mut() {
        w.focused = w.id == id;
    }
    WM.wins_dirty = true;  // Z-order and focus changed
}

pub unsafe fn wm_get_focused_id() -> Option<u32> {
    WM.z_order.last().copied()
}

fn find_window(wm: &mut WindowManager, id: u32) -> Option<&mut Window> {
    wm.windows.iter_mut().find(|w| w.id == id)
}

pub unsafe fn wm_move_window(id: u32, x: i32, y: i32) {
    if let Some(w) = find_window(&mut WM, id) {
        w.x = x;
        w.y = y;
        WM.wins_dirty = true;  // Layout changed
    }
}

/// Fill window content area with a solid color.
pub unsafe fn wm_window_fill(id: u32, color: u32) {
    if let Some(w) = find_window(&mut WM, id) {
        let buf = w.content_buf.as_mut_ptr();
        let total = w.content_buf.len();

        if color == 0 {
            // Fast path: memset zero
            ptr::write_bytes(buf, 0, total);
        } else if WM.bpp == 24 {
            let b0 = color as u8;
            let b1 = (color >> 8) as u8;
            let b2 = (color >> 16) as u8;
            let mut off = 0usize;
            while off + 3 <= total {
                *buf.add(off) = b0;
                *buf.add(off + 1) = b1;
                *buf.add(off + 2) = b2;
                off += 3;
            }
        } else {
            let buf32 = buf as *mut u32;
            let count = total / 4;
            for i in 0..count {
                ptr::write(buf32.add(i), color);
            }
        }
    }
}

/// Draw a character into a window's content buffer.
pub unsafe fn wm_window_draw_char(id: u32, x: u32, y: u32, ch: u8, fg: u32, bg: u32) {
    let bpp = WM.bpp;
    if let Some(w) = find_window(&mut WM, id) {
        let cw = w.content_width();
        let ch_h = w.content_height();
        let pitch = w.content_pitch;
        let bpp_bytes = bpp / 8;
        let buf = w.content_buf.as_mut_ptr();
        let font = &crate::font8x16::VGA_FONT_8X16;
        let glyph_offset = (ch as usize) * (FONT_HEIGHT as usize);

        for row in 0..FONT_HEIGHT {
            if y + row >= ch_h {
                break;
            }
            let idx = glyph_offset + row as usize;
            let glyph_row = if idx < font.len() { font[idx] } else { 0 };
            let row_base = buf.add(((y + row) * pitch) as usize);

            for col in 0..FONT_WIDTH {
                if x + col >= cw {
                    break;
                }
                let color = if (glyph_row >> (7 - col)) & 1 != 0 {
                    fg
                } else {
                    bg
                };
                write_pixel_buf(
                    row_base.add(((x + col) * bpp_bytes) as usize),
                    color,
                    bpp,
                );
            }
        }
    }
}

/// Draw a string into a window's content buffer.
pub unsafe fn wm_window_draw_string(id: u32, x: u32, y: u32, s: &[u8], fg: u32, bg: u32) {
    let mut cx = x;
    for &ch in s {
        if ch == 0 {
            break;
        }
        wm_window_draw_char(id, cx, y, ch, fg, bg);
        cx += FONT_WIDTH;
    }
}

/// Fast batch rendering: render multiple characters in a row without repeated find_window() calls.
/// Called by terminal_render() to avoid O(n) linear search per character.
pub unsafe fn wm_render_rows_direct(id: u32, row: u32, chars: &[u8], fgs: &[u32], bgs: &[u32]) {
    if let Some(w) = find_window(&mut WM, id) {
        let cw = w.content_width();
        let ch_h = w.content_height();
        let pitch = w.content_pitch;
        let bpp = WM.bpp;
        let bpp_bytes = bpp / 8;
        let buf = w.content_buf.as_mut_ptr();
        let font = &crate::font8x16::VGA_FONT_8X16;
        
        let base_y = row * FONT_HEIGHT;
        let mut col = 0u32;
        
        for &ch in chars {
            if col >= 80 {
                break;
            }
            
            let base_x = col * FONT_WIDTH;
            if base_x >= cw {
                break;
            }
            
            let fg = fgs[col as usize];
            let bg = bgs[col as usize];
            let glyph_offset = (ch as usize) * (FONT_HEIGHT as usize);
            
            // Render character glyph
            for glyph_row in 0..FONT_HEIGHT {
                if base_y + glyph_row >= ch_h {
                    break;
                }
                
                let idx = glyph_offset + glyph_row as usize;
                let glyph_bits = if idx < font.len() { font[idx] } else { 0 };
                let row_base = buf.add(((base_y + glyph_row) * pitch) as usize);
                
                for glyph_col in 0..FONT_WIDTH {
                    if base_x + glyph_col >= cw {
                        break;
                    }
                    
                    let pixel_x = base_x + glyph_col;
                    let color = if (glyph_bits >> (7 - glyph_col)) & 1 != 0 {
                        fg
                    } else {
                        bg
                    };
                    
                    write_pixel_buf(
                        row_base.add((pixel_x * bpp_bytes) as usize),
                        color,
                        bpp,
                    );
                }
            }
            
            col += 1;
        }
        
        // Mark dirty region in compositor: entire row
        compositor::compositor_mark_dirty_rect(0, (row * FONT_HEIGHT) as i32, cw, FONT_HEIGHT);
    }
}

/// Draw a filled rectangle in a window's content buffer.
pub unsafe fn wm_window_fill_rect(id: u32, x: u32, y: u32, w: u32, h: u32, color: u32) {
    let bpp = WM.bpp;
    if let Some(win) = find_window(&mut WM, id) {
        let cw = win.content_width();
        let ch = win.content_height();
        let pitch = win.content_pitch;
        let bpp_bytes = bpp / 8;
        let buf = win.content_buf.as_mut_ptr();

        let x_end = if x + w > cw { cw } else { x + w };
        let y_end = if y + h > ch { ch } else { y + h };

        if x >= x_end || y >= y_end {
            return;
        }

        let cols = x_end - x;

        if color == 0 {
            let row_bytes = (cols * bpp_bytes) as usize;
            for row in y..y_end {
                let dst = buf.add((row * pitch + x * bpp_bytes) as usize);
                ptr::write_bytes(dst, 0, row_bytes);
            }
        } else if bpp_bytes == 3 {
            // Fill first row, memcpy to rest
            let row0 = buf.add((y * pitch + x * 3) as usize);
            let b0 = color as u8;
            let b1 = (color >> 8) as u8;
            let b2 = (color >> 16) as u8;
            for col in 0..cols {
                let off = (col * 3) as usize;
                *row0.add(off) = b0;
                *row0.add(off + 1) = b1;
                *row0.add(off + 2) = b2;
            }
            let row_bytes = (cols * 3) as usize;
            for row in (y + 1)..y_end {
                let dst = buf.add((row * pitch + x * 3) as usize);
                ptr::copy_nonoverlapping(row0, dst, row_bytes);
            }
        } else {
            let row0 = buf.add((y * pitch + x * 4) as usize) as *mut u32;
            for col in 0..cols {
                ptr::write(row0.add(col as usize), color);
            }
            let row_bytes = (cols * 4) as usize;
            let row0_src = row0 as *const u8;
            for row in (y + 1)..y_end {
                let dst = buf.add((row * pitch + x * 4) as usize);
                ptr::copy_nonoverlapping(row0_src, dst, row_bytes);
            }
        }
    }
}

/// Compose all windows onto the compositor back buffer.
pub unsafe fn wm_compose_all() {
    let comp = compositor::compositor_get();
    if !comp.initialized {
        return;
    }

    // Draw windows in z-order (back to front)
    // We need to iterate z_order and find corresponding windows
    let z_len = WM.z_order.len();
    for zi in 0..z_len {
        let wid = WM.z_order[zi];
        if let Some(w) = WM.windows.iter().find(|w| w.id == wid) {
            if !w.visible {
                continue;
            }

            let focused = w.focused;
            let wx = w.x;
            let wy = w.y;
            let ww = w.width;
            let wh = w.height;

            // Draw window border
            let border_color: u32 = if focused { 0x4488CC } else { 0x666666 };
            // Top border
            compositor::compositor_fill_rect(wx, wy, ww, BORDER_WIDTH, border_color);
            // Left border
            compositor::compositor_fill_rect(wx, wy, BORDER_WIDTH, wh, border_color);
            // Right border
            compositor::compositor_fill_rect(
                wx + ww as i32 - BORDER_WIDTH as i32,
                wy,
                BORDER_WIDTH,
                wh,
                border_color,
            );
            // Bottom border
            compositor::compositor_fill_rect(
                wx,
                wy + wh as i32 - BORDER_WIDTH as i32,
                ww,
                BORDER_WIDTH,
                border_color,
            );

            // Draw title bar
            let title_color: u32 = if focused { 0x335599 } else { 0x444444 };
            compositor::compositor_fill_rect(
                wx + BORDER_WIDTH as i32,
                wy + BORDER_WIDTH as i32,
                ww - BORDER_WIDTH * 2,
                TITLE_BAR_HEIGHT - BORDER_WIDTH,
                title_color,
            );

            // Draw title text
            let title_x = wx + BORDER_WIDTH as i32 + 6;
            let title_y = wy + BORDER_WIDTH as i32 + 4;
            compositor::compositor_draw_string(
                title_x,
                title_y,
                &w.title[..w.title_len],
                0xFFFFFF,
                title_color,
            );

            // Draw close button [X]
            let close_x = wx + ww as i32 - BORDER_WIDTH as i32 - CLOSE_BTN_SIZE as i32 - 4;
            let close_y = wy + BORDER_WIDTH as i32 + 4;
            compositor::compositor_fill_rect(
                close_x,
                close_y,
                CLOSE_BTN_SIZE,
                CLOSE_BTN_SIZE,
                0xCC3333,
            );
            compositor::compositor_draw_char(close_x + 4, close_y, b'X', 0xFFFFFF, 0xCC3333);

            // Blit window content
            if !w.content_buf.is_empty() {
                compositor::compositor_blit(
                    w.content_buf.as_ptr(),
                    w.content_x(),
                    w.content_y(),
                    w.content_width(),
                    w.content_height(),
                    w.content_pitch,
                );
            }
        }
    }
}

/// Hit-test: which window is under (mx, my)? Returns window ID or None.
/// Also returns whether the click was on the title bar, close button, or content area.
#[derive(Copy, Clone, PartialEq)]
pub enum HitArea {
    TitleBar,
    CloseButton,
    Content,
}

pub unsafe fn wm_hit_test(mx: i32, my: i32) -> Option<(u32, HitArea)> {
    // Check windows from top to bottom (reverse z-order)
    for i in (0..WM.z_order.len()).rev() {
        let wid = WM.z_order[i];
        if let Some(w) = WM.windows.iter().find(|w| w.id == wid) {
            if !w.visible {
                continue;
            }
            if mx >= w.x
                && mx < w.x + w.width as i32
                && my >= w.y
                && my < w.y + w.height as i32
            {
                // Check close button
                let close_x =
                    w.x + w.width as i32 - BORDER_WIDTH as i32 - CLOSE_BTN_SIZE as i32 - 4;
                let close_y = w.y + BORDER_WIDTH as i32 + 4;
                if mx >= close_x
                    && mx < close_x + CLOSE_BTN_SIZE as i32
                    && my >= close_y
                    && my < close_y + CLOSE_BTN_SIZE as i32
                {
                    return Some((wid, HitArea::CloseButton));
                }

                // Check title bar
                if my < w.y + TITLE_BAR_HEIGHT as i32 {
                    return Some((wid, HitArea::TitleBar));
                }

                // Content area
                return Some((wid, HitArea::Content));
            }
        }
    }
    None
}

pub unsafe fn wm_start_drag(id: u32, mx: i32, my: i32) {
    if let Some(w) = find_window(&mut WM, id) {
        WM.dragging = Some(id);
        WM.drag_offset_x = mx - w.x;
        WM.drag_offset_y = my - w.y;
    }
}

pub unsafe fn wm_drag_to(mx: i32, my: i32) {
    if let Some(id) = WM.dragging {
        let new_x = mx - WM.drag_offset_x;
        let new_y = my - WM.drag_offset_y;
        wm_move_window(id, new_x, new_y);
    }
}

pub unsafe fn wm_stop_drag() {
    WM.dragging = None;
}

pub unsafe fn wm_is_dragging() -> bool {
    WM.dragging.is_some()
}

/// Check if any window layout changed (position, visibility, z-order, etc.)
pub unsafe fn wm_windows_dirty() -> bool {
    WM.wins_dirty
}

/// Mark windows as clean (layout rendered, no changes since last render)
pub unsafe fn wm_windows_rendered() {
    WM.wins_dirty = false;
}

/// Mark windows as dirty (need to recompose)
pub unsafe fn wm_mark_windows_dirty() {
    WM.wins_dirty = true;
}
