// GUI Main Module — orchestrates all GUI subsystems

pub mod event;
pub mod compositor;
pub mod window;
pub mod desktop;
pub mod cursor;
pub mod widgets;
pub mod terminal;

use crate::mouse;

extern "C" {
    fn kernel_get_ticks() -> u32;
    fn kernel_get_timer_hz() -> u32;
    fn cli_gui_tick();
}

/// Previous mouse button state for detecting press/release edges.
static mut PREV_BUTTONS: u8 = 0;
static mut LAST_FRAME_TICK: u32 = 0;
static mut LAST_CURSOR_X: i32 = -1;  // Cache cursor position to skip redundant renders
static mut LAST_CURSOR_Y: i32 = -1;

const GUI_TARGET_FPS: u32 = 240;  // Match kernel PIT@240Hz for smooth rendering (4.17ms per frame)
const GUI_MAX_EVENTS_PER_TICK: usize = 64;

#[inline(always)]
unsafe fn gui_frame_interval_ticks() -> u32 {
    let hz = kernel_get_timer_hz();
    if hz == 0 {
        return 1;
    }
    let interval = (hz + (GUI_TARGET_FPS - 1)) / GUI_TARGET_FPS;
    if interval == 0 { 1 } else { interval }
}

/// Initialize all GUI subsystems.
#[no_mangle]
pub unsafe extern "C" fn gui_init(screen_w: u32, screen_h: u32) {
    let bpp = crate::framebuffer::fb_get_bpp();

    // Init compositor (allocates back buffer)
    compositor::compositor_init(screen_w, screen_h, bpp);

    // Init window manager
    window::wm_init(bpp);

    // Init desktop
    desktop::desktop_init(screen_w, screen_h);

    // Create terminal window centered on screen
    let term_w = 648u32; // 80 cols * 8 + 2*border(1) + padding = ~642+6
    let term_h = 680u32; // 40 rows * 16 + titlebar(24) + border(1) = ~665
    let tx = ((screen_w.saturating_sub(term_w)) / 2) as i32;
    let ty = ((screen_h.saturating_sub(term_h + desktop::taskbar_height())) / 2) as i32;
    terminal::terminal_create(tx, ty);

    // Show welcome message in terminal
    terminal::terminal_set_color(10, 0);
    terminal::terminal_write_string(b"VernisOS GUI Terminal\n");
    terminal::terminal_set_color(7, 0);
    terminal::terminal_write_string(b"Type 'help' for available commands.\n\n");
    terminal::terminal_show_prompt();

    PREV_BUTTONS = 0;
    LAST_FRAME_TICK = kernel_get_ticks();
    LAST_CURSOR_X = -1;
    LAST_CURSOR_Y = -1;
}

/// Main loop tick — called repeatedly from kernel main loop.
/// Processes events, updates state, composes frame, presents to screen.
/// Skips rendering when nothing has changed (idle optimization).
#[no_mangle]
pub unsafe extern "C" fn gui_main_loop_tick() {
    // Allow C-side CLI background tasks (e.g. ps realtime in GUI terminal)
    // to update without blocking this render loop.
    cli_gui_tick();

    // CRITICAL: Throttle to GUI_TARGET_FPS to sync with PIT timer
    // Without this, loop runs ahead of framebuffer capacity, causing stutter
    let current_tick = kernel_get_ticks();
    let ticks_since_last = current_tick.wrapping_sub(LAST_FRAME_TICK);
    let frame_interval = gui_frame_interval_ticks();
    if ticks_since_last < frame_interval {
        // Not enough time has passed—skip this frame
        return;
    }
    LAST_FRAME_TICK = current_tick;

    let mut had_events = false;
    let mut had_mouse_move = false;
    let mut had_non_move_event = false;
    let mut processed_events = 0usize;

    // 1. Process all queued events
    while processed_events < GUI_MAX_EVENTS_PER_TICK {
        let Some(ev) = event::event_pop() else { break; };
        processed_events += 1;
        had_events = true;
        match ev.kind {
            event::EventKind::MouseDown => {
                had_non_move_event = true;
                handle_mouse_down(ev.mouse_x, ev.mouse_y, ev.button);
            }
            event::EventKind::MouseUp => {
                had_non_move_event = true;
                handle_mouse_up(ev.mouse_x, ev.mouse_y);
            }
            event::EventKind::MouseMove => {
                had_mouse_move = true;
                handle_mouse_move(ev.mouse_x, ev.mouse_y);
            }
            event::EventKind::KeyPress => {
                had_non_move_event = true;
                handle_key_press(ev.scancode);
            }
            event::EventKind::KeyRelease | event::EventKind::None => {}
        }
    }

    // 2. Render terminal content (only if dirty — skips internally)
    let term_was_dirty = terminal::terminal_get().dirty;
    if term_was_dirty {
        terminal::terminal_render();
    }

    // 3. Only compose and present if something changed
    let need_compose = had_events || term_was_dirty;
    if !need_compose {
        return;
    }

    // Fast path: only mouse movement (no dragging / no terminal updates)
    // -> update cursor incrementally and present only dirty cursor region.
    let cursor_only = had_mouse_move
        && !had_non_move_event
        && !term_was_dirty
        && !window::wm_is_dragging();

    if cursor_only {
        let mx = mouse::mouse_get_x();
        let my = mouse::mouse_get_y();
                // Skip redundant cursor renders for same position during frame skip intervals
                // (multiple move events can queue up while we're throttled)
                if mx == LAST_CURSOR_X && my == LAST_CURSOR_Y {
                    return;
                }
                LAST_CURSOR_X = mx;
                LAST_CURSOR_Y = my;
        if let Some((dx, dy, dw, dh)) = cursor::cursor_move_incremental(mx, my) {
            compositor::compositor_present_rect(dx, dy, dw, dh);
        }
        return;
    }

    // Render affected components only — don't always render background/taskbar!
    // Background + Taskbar only needed when window layout changes, not for terminal output
    if had_non_move_event || window::wm_windows_dirty() {
        // Full composition when windows change
        desktop::desktop_draw_background();
        window::wm_compose_all();
        desktop::desktop_draw_taskbar();
        window::wm_windows_rendered();
    } else if !term_was_dirty {
        // No changes at all—this shouldn't happen (caught earlier), but safety check
        return;
    }
    // Note: if only terminal_was_dirty, we skip background/taskbar (already rendered) and only render terminal

    // 5. Draw mouse cursor on top
    let mx = mouse::mouse_get_x();
    let my = mouse::mouse_get_y();
    cursor::cursor_draw_fresh(mx, my);
    LAST_CURSOR_X = mx;
    LAST_CURSOR_Y = my;

    // 6. Present to framebuffer (only dirty region via dirty rect system)
    compositor::compositor_present();
}

/// Handle keyboard input — called from C IRQ handler.
#[no_mangle]
pub unsafe extern "C" fn gui_handle_key(scancode: u8, _pressed: u8) {
    // Push key event to queue
    event::gui_push_key_event(scancode, _pressed);
}

/// Handle mouse input — called from C IRQ12 handler.
/// This processes the raw PS/2 packet, updates mouse position,
/// and generates high-level events.
#[no_mangle]
pub unsafe extern "C" fn gui_handle_mouse(flags: u8, dx: i8, dy: i8) {
    // Update mouse position via the mouse module
    mouse::mouse_handle_packet(flags, dx, dy);

    let mx = mouse::mouse_get_x();
    let my = mouse::mouse_get_y();
    let buttons = mouse::mouse_get_buttons();

    // Generate movement event
    event::gui_push_mouse_move(mx, my);

    // Generate button press/release events (edge detection)
    let prev = PREV_BUTTONS;
    for bit in 0..3u8 {
        let mask = 1u8 << bit;
        let was = prev & mask;
        let now = buttons & mask;
        if was == 0 && now != 0 {
            event::gui_push_mouse_button(mx, my, bit, 1);
        } else if was != 0 && now == 0 {
            event::gui_push_mouse_button(mx, my, bit, 0);
        }
    }
    PREV_BUTTONS = buttons;
}

// --- Internal event handlers ---

unsafe fn handle_mouse_down(mx: i32, my: i32, button: u8) {
    if button != 0 {
        return; // Only handle left button
    }

    // Check taskbar first
    if let Some(wid) = desktop::taskbar_hit_test(mx, my) {
        window::wm_focus_window(wid);
        return;
    }

    // Hit-test windows
    if let Some((wid, area)) = window::wm_hit_test(mx, my) {
        window::wm_focus_window(wid);

        match area {
            window::HitArea::CloseButton => {
                widgets::widget_remove_all(wid);
                window::wm_close_window(wid);
            }
            window::HitArea::TitleBar => {
                window::wm_start_drag(wid, mx, my);
            }
            window::HitArea::Content => {
                // Convert to window-local coordinates and handle widget click
                let wm = window::wm_get();
                if let Some(w) = wm.windows.iter().find(|w| w.id == wid) {
                    let local_x = (mx - w.content_x()) as u32;
                    let local_y = (my - w.content_y()) as u32;
                    widgets::widget_handle_click(wid, local_x, local_y);
                }
            }
        }
    }
}

unsafe fn handle_mouse_up(_mx: i32, _my: i32) {
    if window::wm_is_dragging() {
        window::wm_stop_drag();
    }
}

unsafe fn handle_mouse_move(mx: i32, my: i32) {
    if window::wm_is_dragging() {
        window::wm_drag_to(mx, my);
    }
}

unsafe fn handle_key_press(scancode: u8) {
    // Route to focused window's terminal
    let focused = window::wm_get_focused_id();
    if let Some(wid) = focused {
        let term = terminal::terminal_get();
        if term.initialized && term.window_id == wid {
            terminal::terminal_handle_key(scancode);
        }
    }
}
