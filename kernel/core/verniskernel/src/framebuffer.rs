// Framebuffer driver — low-level pixel operations + bitmap font rendering
// Uses VBE linear framebuffer (24bpp or 32bpp)

use core::ptr;

use crate::font8x16::VGA_FONT_8X16;

pub const FONT_WIDTH: u32 = 8;
pub const FONT_HEIGHT: u32 = 16;

struct FrameBuffer {
    addr: *mut u8,
    width: u32,
    height: u32,
    pitch: u32, // bytes per scanline
    bpp: u32,   // bits per pixel (24 or 32)
    initialized: bool,
}

static mut FB: FrameBuffer = FrameBuffer {
    addr: core::ptr::null_mut(),
    width: 0,
    height: 0,
    pitch: 0,
    bpp: 0,
    initialized: false,
};

/// Write a pixel at byte offset. Handles 24bpp and 32bpp.
#[inline(always)]
unsafe fn write_pixel(base: *mut u8, color: u32) {
    if FB.bpp == 32 {
        ptr::write_volatile(base as *mut u32, color);
    } else {
        // 24bpp: write B, G, R bytes
        ptr::write_volatile(base, color as u8);
        ptr::write_volatile(base.add(1), (color >> 8) as u8);
        ptr::write_volatile(base.add(2), (color >> 16) as u8);
    }
}

#[no_mangle]
pub unsafe extern "C" fn fb_init(
    addr: usize,
    width: u32,
    height: u32,
    pitch: u32,
    bpp: u32,
) {
    FB.addr = addr as *mut u8;
    FB.width = width;
    FB.height = height;
    FB.pitch = pitch;
    FB.bpp = bpp;
    FB.initialized = true;
}

#[no_mangle]
pub unsafe extern "C" fn fb_put_pixel(x: u32, y: u32, color: u32) {
    if !FB.initialized || x >= FB.width || y >= FB.height {
        return;
    }
    let offset = (y * FB.pitch + x * (FB.bpp / 8)) as usize;
    write_pixel(FB.addr.add(offset), color);
}

#[no_mangle]
pub unsafe extern "C" fn fb_fill_rect(
    x: u32,
    y: u32,
    w: u32,
    h: u32,
    color: u32,
) {
    if !FB.initialized {
        return;
    }
    let bpp_bytes = FB.bpp / 8;
    let x_end = if x + w > FB.width { FB.width } else { x + w };
    let y_end = if y + h > FB.height { FB.height } else { y + h };

    for row in y..y_end {
        let row_base = FB.addr.add((row * FB.pitch) as usize);
        for col in x..x_end {
            write_pixel(row_base.add((col * bpp_bytes) as usize), color);
        }
    }
}

#[no_mangle]
pub unsafe extern "C" fn fb_draw_char(
    x: u32,
    y: u32,
    ch: u8,
    fg: u32,
    bg: u32,
) {
    if !FB.initialized {
        return;
    }
    let glyph_offset = (ch as usize) * (FONT_HEIGHT as usize);
    let font_len = VGA_FONT_8X16.len();
    let bpp_bytes = FB.bpp / 8;

    for row in 0..FONT_HEIGHT {
        if y + row >= FB.height {
            break;
        }
        let idx = glyph_offset + row as usize;
        let glyph_row = if idx < font_len { VGA_FONT_8X16[idx] } else { 0 };
        let row_base = FB.addr.add(((y + row) * FB.pitch) as usize);

        for col in 0..FONT_WIDTH {
            if x + col >= FB.width {
                break;
            }
            let pixel_addr = row_base.add(((x + col) * bpp_bytes) as usize);
            let color = if (glyph_row >> (7 - col)) & 1 != 0 {
                fg
            } else {
                bg
            };
            write_pixel(pixel_addr, color);
        }
    }
}

#[no_mangle]
pub unsafe extern "C" fn fb_scroll(lines: u32) {
    if !FB.initialized || lines == 0 {
        return;
    }
    let pixel_rows = lines * FONT_HEIGHT;
    if pixel_rows >= FB.height {
        fb_clear(0x00000000);
        return;
    }
    let bytes_to_copy = ((FB.height - pixel_rows) * FB.pitch) as usize;
    let src = FB.addr.add((pixel_rows * FB.pitch) as usize);
    ptr::copy(src, FB.addr, bytes_to_copy);

    // Clear the bottom area
    let clear_start = FB.addr.add(bytes_to_copy);
    let clear_bytes = (pixel_rows * FB.pitch) as usize;
    ptr::write_bytes(clear_start, 0, clear_bytes);
}

#[no_mangle]
pub unsafe extern "C" fn fb_clear(color: u32) {
    if !FB.initialized {
        return;
    }
    if color == 0 {
        // Fast path: memset to 0
        ptr::write_bytes(FB.addr, 0, (FB.height * FB.pitch) as usize);
    } else {
        let bpp_bytes = FB.bpp / 8;
        for row in 0..FB.height {
            let row_base = FB.addr.add((row * FB.pitch) as usize);
            for col in 0..FB.width {
                let pixel = row_base.add((col * bpp_bytes) as usize) as *mut u32;
                ptr::write_volatile(pixel, color);
            }
        }
    }
}

#[no_mangle]
pub unsafe extern "C" fn fb_get_width() -> u32 {
    FB.width
}

#[no_mangle]
pub unsafe extern "C" fn fb_get_height() -> u32 {
    FB.height
}

#[no_mangle]
pub unsafe extern "C" fn fb_get_addr() -> usize {
    FB.addr as usize
}

#[no_mangle]
pub unsafe extern "C" fn fb_get_pitch() -> u32 {
    FB.pitch
}

#[no_mangle]
pub unsafe extern "C" fn fb_get_bpp() -> u32 {
    FB.bpp
}

/// Blit a rectangular region from src buffer to the framebuffer.
/// src_pitch is the byte stride of the source buffer.
#[no_mangle]
pub unsafe extern "C" fn fb_blit(
    src: *const u8,
    x: u32,
    y: u32,
    w: u32,
    h: u32,
    src_pitch: u32,
) {
    if !FB.initialized || src.is_null() {
        return;
    }
    let bpp_bytes = FB.bpp / 8;
    let x_end = if x + w > FB.width { FB.width } else { x + w };
    let y_end = if y + h > FB.height { FB.height } else { y + h };
    let copy_w = x_end.saturating_sub(x);
    if copy_w == 0 {
        return;
    }
    let row_bytes = (copy_w * bpp_bytes) as usize;

    for row in y..y_end {
        let src_row = src.add(((row - y) * src_pitch) as usize);
        let dst_row = FB.addr.add((row * FB.pitch + x * bpp_bytes) as usize);
        ptr::copy_nonoverlapping(src_row, dst_row, row_bytes);
    }
}

/// Write pixel into an arbitrary buffer (not the framebuffer).
/// Used by compositor/window drawing. Handles 24bpp and 32bpp.
#[inline(always)]
pub unsafe fn write_pixel_buf(base: *mut u8, color: u32, bpp: u32) {
    if bpp == 32 {
        ptr::write(base as *mut u32, color);
    } else {
        ptr::write(base, color as u8);
        ptr::write(base.add(1), (color >> 8) as u8);
        ptr::write(base.add(2), (color >> 16) as u8);
    }
}

/// Read pixel from an arbitrary buffer. Returns 0xRRGGBB.
#[inline(always)]
pub unsafe fn read_pixel_buf(base: *const u8, bpp: u32) -> u32 {
    if bpp == 32 {
        ptr::read(base as *const u32)
    } else {
        let b = ptr::read(base) as u32;
        let g = ptr::read(base.add(1)) as u32;
        let r = ptr::read(base.add(2)) as u32;
        b | (g << 8) | (r << 16)
    }
}
