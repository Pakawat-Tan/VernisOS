// Basic widget toolkit — Button, Label, TextInput

use crate::framebuffer::{FONT_WIDTH, FONT_HEIGHT};
use crate::gui::window;

const MAX_WIDGETS_PER_WINDOW: usize = 16;
const MAX_LABEL_LEN: usize = 127;
const MAX_INPUT_LEN: usize = 255;
const MAX_BUTTON_LABEL: usize = 31;

#[derive(Copy, Clone)]
pub enum WidgetKind {
    Button {
        label: [u8; 32],
        label_len: usize,
        callback_id: u32,
    },
    Label {
        text: [u8; 128],
        text_len: usize,
    },
    TextInput {
        buffer: [u8; 256],
        len: usize,
        cursor_pos: usize,
        focused: bool,
    },
}

#[derive(Copy, Clone)]
pub struct Widget {
    pub kind: WidgetKind,
    pub x: u32,
    pub y: u32,
    pub w: u32,
    pub h: u32,
    pub active: bool,
}

pub struct WidgetList {
    pub window_id: u32,
    pub widgets: [Widget; MAX_WIDGETS_PER_WINDOW],
    pub count: usize,
}

const EMPTY_WIDGET: Widget = Widget {
    kind: WidgetKind::Label {
        text: [0u8; 128],
        text_len: 0,
    },
    x: 0,
    y: 0,
    w: 0,
    h: 0,
    active: false,
};

// Global widget storage (max 8 windows with widgets)
const MAX_WIDGET_LISTS: usize = 8;
static mut WIDGET_LISTS: [WidgetList; MAX_WIDGET_LISTS] = {
    const INIT: WidgetList = WidgetList {
        window_id: 0,
        widgets: [EMPTY_WIDGET; MAX_WIDGETS_PER_WINDOW],
        count: 0,
    };
    [INIT; MAX_WIDGET_LISTS]
};
static mut WIDGET_LIST_COUNT: usize = 0;

unsafe fn find_widget_list(window_id: u32) -> Option<&'static mut WidgetList> {
    for i in 0..WIDGET_LIST_COUNT {
        if WIDGET_LISTS[i].window_id == window_id {
            return Some(&mut WIDGET_LISTS[i]);
        }
    }
    None
}

unsafe fn get_or_create_widget_list(window_id: u32) -> Option<&'static mut WidgetList> {
    // Try existing first
    for i in 0..WIDGET_LIST_COUNT {
        if WIDGET_LISTS[i].window_id == window_id {
            return Some(&mut WIDGET_LISTS[i]);
        }
    }
    // Create new
    if WIDGET_LIST_COUNT < MAX_WIDGET_LISTS {
        let idx = WIDGET_LIST_COUNT;
        WIDGET_LIST_COUNT += 1;
        WIDGET_LISTS[idx].window_id = window_id;
        WIDGET_LISTS[idx].count = 0;
        return Some(&mut WIDGET_LISTS[idx]);
    }
    None
}

pub unsafe fn widget_add_button(
    window_id: u32,
    x: u32,
    y: u32,
    w: u32,
    h: u32,
    label: &[u8],
    callback_id: u32,
) -> Option<usize> {
    let list = get_or_create_widget_list(window_id)?;
    if list.count >= MAX_WIDGETS_PER_WINDOW {
        return None;
    }
    let idx = list.count;
    let mut label_buf = [0u8; 32];
    let len = label.len().min(MAX_BUTTON_LABEL);
    label_buf[..len].copy_from_slice(&label[..len]);
    list.widgets[idx] = Widget {
        kind: WidgetKind::Button {
            label: label_buf,
            label_len: len,
            callback_id,
        },
        x,
        y,
        w,
        h,
        active: true,
    };
    list.count += 1;
    Some(idx)
}

pub unsafe fn widget_add_label(
    window_id: u32,
    x: u32,
    y: u32,
    text: &[u8],
) -> Option<usize> {
    let list = get_or_create_widget_list(window_id)?;
    if list.count >= MAX_WIDGETS_PER_WINDOW {
        return None;
    }
    let idx = list.count;
    let mut text_buf = [0u8; 128];
    let len = text.len().min(MAX_LABEL_LEN);
    text_buf[..len].copy_from_slice(&text[..len]);
    let w = (len as u32) * FONT_WIDTH;
    let h = FONT_HEIGHT;
    list.widgets[idx] = Widget {
        kind: WidgetKind::Label {
            text: text_buf,
            text_len: len,
        },
        x,
        y,
        w,
        h,
        active: true,
    };
    list.count += 1;
    Some(idx)
}

pub unsafe fn widget_add_text_input(
    window_id: u32,
    x: u32,
    y: u32,
    w: u32,
) -> Option<usize> {
    let list = get_or_create_widget_list(window_id)?;
    if list.count >= MAX_WIDGETS_PER_WINDOW {
        return None;
    }
    let idx = list.count;
    list.widgets[idx] = Widget {
        kind: WidgetKind::TextInput {
            buffer: [0u8; 256],
            len: 0,
            cursor_pos: 0,
            focused: false,
        },
        x,
        y,
        w,
        h: FONT_HEIGHT + 8, // padding
        active: true,
    };
    list.count += 1;
    Some(idx)
}

/// Draw all widgets into the window's content buffer.
pub unsafe fn widget_draw_all(window_id: u32) {
    let list = match find_widget_list(window_id) {
        Some(l) => l,
        None => return,
    };

    for i in 0..list.count {
        let w = &list.widgets[i];
        if !w.active {
            continue;
        }
        match &w.kind {
            WidgetKind::Button {
                label,
                label_len,
                ..
            } => {
                // Button: raised rectangle + centered text
                window::wm_window_fill_rect(window_id, w.x, w.y, w.w, w.h, 0x444466);
                // Highlight top/left
                window::wm_window_fill_rect(window_id, w.x, w.y, w.w, 1, 0x666688);
                window::wm_window_fill_rect(window_id, w.x, w.y, 1, w.h, 0x666688);
                // Text centered
                let text_x = w.x + (w.w.saturating_sub(*label_len as u32 * FONT_WIDTH)) / 2;
                let text_y = w.y + (w.h.saturating_sub(FONT_HEIGHT)) / 2;
                window::wm_window_draw_string(
                    window_id,
                    text_x,
                    text_y,
                    &label[..*label_len],
                    0xFFFFFF,
                    0x444466,
                );
            }
            WidgetKind::Label { text, text_len } => {
                window::wm_window_draw_string(
                    window_id,
                    w.x,
                    w.y,
                    &text[..*text_len],
                    0xCCCCCC,
                    0x000000, // transparent-ish -- use window bg
                );
            }
            WidgetKind::TextInput {
                buffer,
                len,
                cursor_pos,
                focused,
            } => {
                // Input field: border + text + cursor
                let border_color = if *focused { 0x4488CC } else { 0x555555 };
                let bg = 0x1A1A1A;
                window::wm_window_fill_rect(window_id, w.x, w.y, w.w, w.h, bg);
                // Border
                window::wm_window_fill_rect(window_id, w.x, w.y, w.w, 1, border_color);
                window::wm_window_fill_rect(
                    window_id,
                    w.x,
                    w.y + w.h - 1,
                    w.w,
                    1,
                    border_color,
                );
                window::wm_window_fill_rect(window_id, w.x, w.y, 1, w.h, border_color);
                window::wm_window_fill_rect(
                    window_id,
                    w.x + w.w - 1,
                    w.y,
                    1,
                    w.h,
                    border_color,
                );
                // Text
                window::wm_window_draw_string(
                    window_id,
                    w.x + 4,
                    w.y + 4,
                    &buffer[..*len],
                    0xCCCCCC,
                    bg,
                );
                // Cursor
                if *focused {
                    let cx = w.x + 4 + (*cursor_pos as u32) * FONT_WIDTH;
                    window::wm_window_fill_rect(window_id, cx, w.y + 3, 1, FONT_HEIGHT + 2, 0xFFFFFF);
                }
            }
        }
    }
}

/// Handle a click within a window's widget area.
/// Returns callback_id for button clicks, or None.
pub unsafe fn widget_handle_click(window_id: u32, x: u32, y: u32) -> Option<u32> {
    let list = match find_widget_list(window_id) {
        Some(l) => l,
        None => return None,
    };

    // Unfocus all text inputs first
    for i in 0..list.count {
        if let WidgetKind::TextInput { focused, .. } = &mut list.widgets[i].kind {
            *focused = false;
        }
    }

    for i in 0..list.count {
        let w = &mut list.widgets[i];
        if !w.active {
            continue;
        }
        if x >= w.x && x < w.x + w.w && y >= w.y && y < w.y + w.h {
            match &mut w.kind {
                WidgetKind::Button { callback_id, .. } => {
                    return Some(*callback_id);
                }
                WidgetKind::TextInput { focused, .. } => {
                    *focused = true;
                    return None;
                }
                _ => {}
            }
        }
    }
    None
}

/// Handle a keypress for the focused text input in a window.
pub unsafe fn widget_handle_key(window_id: u32, ascii: u8) {
    let list = match find_widget_list(window_id) {
        Some(l) => l,
        None => return,
    };

    for i in 0..list.count {
        if let WidgetKind::TextInput {
            buffer,
            len,
            cursor_pos,
            focused,
        } = &mut list.widgets[i].kind
        {
            if !*focused {
                continue;
            }
            match ascii {
                8 | 127 => {
                    // Backspace
                    if *cursor_pos > 0 {
                        // Shift chars left
                        let mut j = *cursor_pos - 1;
                        while j + 1 < *len {
                            buffer[j] = buffer[j + 1];
                            j += 1;
                        }
                        *len -= 1;
                        *cursor_pos -= 1;
                    }
                }
                b'\n' | b'\r' => {
                    // Enter — don't handle here, let terminal handle it
                }
                _ => {
                    if *len < MAX_INPUT_LEN && ascii >= 32 {
                        // Insert char at cursor
                        let mut j = *len;
                        while j > *cursor_pos {
                            buffer[j] = buffer[j - 1];
                            j -= 1;
                        }
                        buffer[*cursor_pos] = ascii;
                        *len += 1;
                        *cursor_pos += 1;
                    }
                }
            }
            return;
        }
    }
}

/// Remove all widgets for a window (called on window close).
pub unsafe fn widget_remove_all(window_id: u32) {
    for i in 0..WIDGET_LIST_COUNT {
        if WIDGET_LISTS[i].window_id == window_id {
            // Shift remaining lists down
            let mut j = i;
            while j + 1 < WIDGET_LIST_COUNT {
                // Manual field copy since we can't move the whole array element easily
                WIDGET_LISTS[j].window_id = WIDGET_LISTS[j + 1].window_id;
                WIDGET_LISTS[j].count = WIDGET_LISTS[j + 1].count;
                WIDGET_LISTS[j].widgets = WIDGET_LISTS[j + 1].widgets;
                j += 1;
            }
            WIDGET_LIST_COUNT -= 1;
            return;
        }
    }
}
