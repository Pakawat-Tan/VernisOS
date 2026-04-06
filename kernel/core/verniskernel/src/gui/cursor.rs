// Mouse cursor rendering — 12x19 arrow bitmap

use crate::gui::compositor;

// Arrow cursor bitmap (12 wide x 19 tall)
// Each row: bits represent pixels, MSB first
const CURSOR_W: u32 = 12;
const CURSOR_H: u32 = 19;
const CURSOR_MAX_BYTES: usize = (CURSOR_W as usize) * (CURSOR_H as usize) * 4;

// Bit mask: 1 = draw pixel, 0 = transparent
const CURSOR_MASK: [u16; 19] = [
    0b1000_0000_0000_0000,
    0b1100_0000_0000_0000,
    0b1110_0000_0000_0000,
    0b1111_0000_0000_0000,
    0b1111_1000_0000_0000,
    0b1111_1100_0000_0000,
    0b1111_1110_0000_0000,
    0b1111_1111_0000_0000,
    0b1111_1111_1000_0000,
    0b1111_1111_1100_0000,
    0b1111_1111_1110_0000,
    0b1111_1111_1111_0000,
    0b1111_1110_0000_0000,
    0b1110_1111_0000_0000,
    0b1100_0111_1000_0000,
    0b1000_0111_1000_0000,
    0b0000_0011_1100_0000,
    0b0000_0011_1100_0000,
    0b0000_0001_1000_0000,
];

// Shape: 1 = white interior, 0 = black outline (only within CURSOR_MASK)
const CURSOR_SHAPE: [u16; 19] = [
    0b0000_0000_0000_0000,
    0b0100_0000_0000_0000,
    0b0110_0000_0000_0000,
    0b0111_0000_0000_0000,
    0b0111_1000_0000_0000,
    0b0111_1100_0000_0000,
    0b0111_1110_0000_0000,
    0b0111_1111_0000_0000,
    0b0111_1111_1000_0000,
    0b0111_1111_1100_0000,
    0b0111_1111_1110_0000,
    0b0111_1110_0000_0000,
    0b0111_0110_0000_0000,
    0b0110_0111_0000_0000,
    0b0100_0011_0000_0000,
    0b0000_0011_0000_0000,
    0b0000_0001_1000_0000,
    0b0000_0001_1000_0000,
    0b0000_0000_0000_0000,
];

static mut CURSOR_SAVED: [u8; CURSOR_MAX_BYTES] = [0; CURSOR_MAX_BYTES];
static mut CURSOR_HAS_SAVED: bool = false;
static mut CURSOR_X: i32 = 0;
static mut CURSOR_Y: i32 = 0;
static mut CURSOR_W_CLIP: u32 = 0;
static mut CURSOR_H_CLIP: u32 = 0;

#[inline(always)]
unsafe fn rect_clip(x: i32, y: i32, w: u32, h: u32, sw: u32, sh: u32) -> Option<(i32, i32, u32, u32)> {
    let x0 = if x < 0 { 0 } else { x as u32 };
    let y0 = if y < 0 { 0 } else { y as u32 };
    if x0 >= sw || y0 >= sh {
        return None;
    }

    let x_end = {
        let xe = x.saturating_add(w as i32);
        if xe <= 0 {
            0
        } else {
            let xe_u = xe as u32;
            if xe_u > sw { sw } else { xe_u }
        }
    };

    let y_end = {
        let ye = y.saturating_add(h as i32);
        if ye <= 0 {
            0
        } else {
            let ye_u = ye as u32;
            if ye_u > sh { sh } else { ye_u }
        }
    };

    if x_end <= x0 || y_end <= y0 {
        return None;
    }
    Some((x0 as i32, y0 as i32, x_end - x0, y_end - y0))
}

unsafe fn cursor_save_under(x: i32, y: i32) {
    let comp = compositor::compositor_get();
    if !comp.initialized {
        CURSOR_HAS_SAVED = false;
        return;
    }

    let Some((cx, cy, cw, ch)) = rect_clip(x, y, CURSOR_W, CURSOR_H, comp.width, comp.height) else {
        CURSOR_HAS_SAVED = false;
        return;
    };

    let bpp_bytes = (comp.bpp / 8) as usize;
    let row_bytes = (cw as usize) * bpp_bytes;
    let buf = comp.back_buffer.as_ptr();

    for row in 0..(ch as usize) {
        let src = buf.add(((cy as u32 + row as u32) * comp.pitch + (cx as u32) * (comp.bpp / 8)) as usize);
        let dst = CURSOR_SAVED.as_mut_ptr().add(row * row_bytes);
        core::ptr::copy_nonoverlapping(src, dst, row_bytes);
    }

    CURSOR_X = cx;
    CURSOR_Y = cy;
    CURSOR_W_CLIP = cw;
    CURSOR_H_CLIP = ch;
    CURSOR_HAS_SAVED = true;
}

unsafe fn cursor_restore_under() {
    if !CURSOR_HAS_SAVED || CURSOR_W_CLIP == 0 || CURSOR_H_CLIP == 0 {
        return;
    }

    let comp = compositor::compositor_get();
    if !comp.initialized {
        CURSOR_HAS_SAVED = false;
        return;
    }

    let bpp_bytes = (comp.bpp / 8) as usize;
    let row_bytes = (CURSOR_W_CLIP as usize) * bpp_bytes;
    let dst_buf = comp.back_buffer.as_mut_ptr();

    for row in 0..(CURSOR_H_CLIP as usize) {
        let src = CURSOR_SAVED.as_ptr().add(row * row_bytes);
        let dst = dst_buf.add(((CURSOR_Y as u32 + row as u32) * comp.pitch + (CURSOR_X as u32) * (comp.bpp / 8)) as usize);
        core::ptr::copy_nonoverlapping(src, dst, row_bytes);
    }
}

unsafe fn cursor_union_rect(nx: i32, ny: i32, nw: u32, nh: u32) -> Option<(i32, i32, u32, u32)> {
    let comp = compositor::compositor_get();
    let old = if CURSOR_HAS_SAVED {
        rect_clip(CURSOR_X, CURSOR_Y, CURSOR_W_CLIP, CURSOR_H_CLIP, comp.width, comp.height)
    } else {
        None
    };
    let newr = rect_clip(nx, ny, nw, nh, comp.width, comp.height);

    match (old, newr) {
        (None, None) => None,
        (Some(r), None) | (None, Some(r)) => Some(r),
        (Some((ox, oy, ow, oh)), Some((nx2, ny2, nw2, nh2))) => {
            let x0 = if ox < nx2 { ox } else { nx2 };
            let y0 = if oy < ny2 { oy } else { ny2 };
            let ox1 = ox.saturating_add(ow as i32);
            let oy1 = oy.saturating_add(oh as i32);
            let nx1 = nx2.saturating_add(nw2 as i32);
            let ny1 = ny2.saturating_add(nh2 as i32);
            let x1 = if ox1 > nx1 { ox1 } else { nx1 };
            let y1 = if oy1 > ny1 { oy1 } else { ny1 };
            Some((x0, y0, (x1 - x0) as u32, (y1 - y0) as u32))
        }
    }
}

fn draw_cursor_shape(x: i32, y: i32) {
    unsafe {
    let comp = compositor::compositor_get();
    if !comp.initialized {
        return;
    }

    let bpp_bytes = comp.bpp / 8;
    let buf = comp.back_buffer.as_mut_ptr();

    for row in 0..CURSOR_H {
        let py = y + row as i32;
        if py < 0 || py >= comp.height as i32 {
            continue;
        }
        let mask_row = CURSOR_MASK[row as usize];
        let shape_row = CURSOR_SHAPE[row as usize];
        let row_base = buf.add((py as u32 * comp.pitch) as usize);

        for col in 0..CURSOR_W {
            let px = x + col as i32;
            if px < 0 || px >= comp.width as i32 {
                continue;
            }
            let bit = 15 - col; // MSB first
            if (mask_row >> bit) & 1 != 0 {
                let color = if (shape_row >> bit) & 1 != 0 {
                    0xFFFFFF // white interior
                } else {
                    0x000000 // black outline
                };
                crate::framebuffer::write_pixel_buf(
                    row_base.add((px as u32 * bpp_bytes) as usize),
                    color,
                    comp.bpp,
                );
            }
        }
    }
    }
}

/// Draw cursor on a freshly composed frame (without restoring old cursor).
pub unsafe fn cursor_draw_fresh(x: i32, y: i32) {
    cursor_save_under(x, y);
    draw_cursor_shape(x, y);
}

/// Move cursor incrementally: restore previous underlay, draw at new position,
/// and return a dirty rectangle that covers both old and new cursor areas.
pub unsafe fn cursor_move_incremental(x: i32, y: i32) -> Option<(i32, i32, u32, u32)> {
    let dirty = cursor_union_rect(x, y, CURSOR_W, CURSOR_H);
    cursor_restore_under();
    cursor_save_under(x, y);
    draw_cursor_shape(x, y);
    dirty
}
