// Desktop + Taskbar rendering

use crate::gui::compositor;
use crate::gui::window;
use crate::framebuffer::FONT_WIDTH;

const TASKBAR_HEIGHT: u32 = 32;
const DESKTOP_BG_COLOR: u32 = 0x1A1A2E;   // dark navy
const TASKBAR_BG_COLOR: u32 = 0x16213E;   // slightly lighter
const TASKBAR_TEXT_COLOR: u32 = 0xC0C0C0;
const TASKBAR_ACTIVE_COLOR: u32 = 0x335599;

static mut SCREEN_W: u32 = 0;
static mut SCREEN_H: u32 = 0;

pub unsafe fn desktop_init(screen_w: u32, screen_h: u32) {
    SCREEN_W = screen_w;
    SCREEN_H = screen_h;
}

pub unsafe fn desktop_draw_background() {
    compositor::compositor_clear(DESKTOP_BG_COLOR);
}

pub unsafe fn desktop_draw_taskbar() {
    let bar_y = SCREEN_H.saturating_sub(TASKBAR_HEIGHT) as i32;

    // Taskbar background
    compositor::compositor_fill_rect(0, bar_y, SCREEN_W, TASKBAR_HEIGHT, TASKBAR_BG_COLOR);

    // Top separator line
    compositor::compositor_fill_rect(0, bar_y, SCREEN_W, 1, 0x333355);

    // "VernisOS" label on the left
    compositor::compositor_draw_string(8, bar_y + 8, b"VernisOS", 0x55AAFF, TASKBAR_BG_COLOR);

    // Window entries
    let wm = window::wm_get();
    let mut tx = 100i32; // start after the logo

    for i in 0..wm.z_order.len() {
        let wid = wm.z_order[i];
        if let Some(w) = wm.windows.iter().find(|w| w.id == wid) {
            if !w.visible {
                continue;
            }
            let bg = if w.focused {
                TASKBAR_ACTIVE_COLOR
            } else {
                TASKBAR_BG_COLOR
            };

            // Draw button background
            let btn_w = (w.title_len as u32 * FONT_WIDTH + 16).min(150);
            compositor::compositor_fill_rect(tx, bar_y + 4, btn_w, TASKBAR_HEIGHT - 8, bg);

            // Draw title
            compositor::compositor_draw_string(
                tx + 4,
                bar_y + 8,
                &w.title[..w.title_len],
                TASKBAR_TEXT_COLOR,
                bg,
            );

            tx += btn_w as i32 + 4;
        }
    }
}

/// Returns the taskbar area height.
pub fn taskbar_height() -> u32 {
    TASKBAR_HEIGHT
}

/// Hit-test taskbar: returns window ID to focus, or None.
pub unsafe fn taskbar_hit_test(mx: i32, my: i32) -> Option<u32> {
    let bar_y = SCREEN_H.saturating_sub(TASKBAR_HEIGHT) as i32;
    if my < bar_y || my >= SCREEN_H as i32 {
        return None;
    }

    let wm = window::wm_get();
    let mut tx = 100i32;

    for i in 0..wm.z_order.len() {
        let wid = wm.z_order[i];
        if let Some(w) = wm.windows.iter().find(|w| w.id == wid) {
            if !w.visible {
                continue;
            }
            let btn_w = (w.title_len as u32 * FONT_WIDTH + 16).min(150) as i32;
            if mx >= tx && mx < tx + btn_w {
                return Some(wid);
            }
            tx += btn_w + 4;
        }
    }
    None
}
