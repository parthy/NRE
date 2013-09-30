/*
 * Copyright (C) 2012, Nils Asmussen <nils@os.inf.tu-dresden.de>
 * Economic rights: Technische Universitaet Dresden (Germany)
 *
 * This file is part of NRE (NOVA runtime environment).
 *
 * NRE is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * NRE is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
 * General Public License version 2 for more details.
 */

#include <util/Math.h>

#include "VESAScreen.h"
#include "VESAFont.h"

#define PIXEL_SET(c,x,y)    ((font8x16)[(c) * FONT_HEIGHT + (y)] & (1 << (FONT_WIDTH - (x) - 1)))

static uint8_t colors[][3] = {
    /* BLACK   */ {0x00,0x00,0x00},
    /* BLUE    */ {0x00,0x00,0xA8},
    /* GREEN   */ {0x00,0xA8,0x00},
    /* CYAN    */ {0x00,0xA8,0xA8},
    /* RED     */ {0xA8,0x00,0x00},
    /* MARGENT */ {0xA8,0x00,0xA8},
    /* ORANGE  */ {0xA8,0x57,0x00},
    /* WHITE   */ {0xA8,0xA8,0xA8},
    /* GRAY    */ {0x57,0x57,0x57},
    /* LIBLUE  */ {0x57,0x57,0xFF},
    /* LIGREEN */ {0x57,0xFF,0x57},
    /* LICYAN  */ {0x57,0xFF,0xFF},
    /* LIRED   */ {0xFF,0x57,0x57},
    /* LIMARGE */ {0xFF,0x57,0xFF},
    /* LIORANG */ {0xFF,0xFF,0x57},
    /* LIWHITE */ {0xFF,0xFF,0xFF},
};

void VESAScreen::set_regs(const nre::Console::Register &regs, bool) {
    if(_last.mode != regs.mode)
        _vbe.get_mode_info(regs.mode, _info);
    _last = regs;
}

void VESAScreen::write_tag(const char *tag, size_t len, uint8_t color) {
    for(uint x = 0; x < _info.resolution[0]; x += FONT_WIDTH) {
        if(len > 0) {
            draw_char(x, 0, *tag, color);
            tag++;
            len--;
        }
        else
            draw_char(x, 0, ' ', color);
    }
}

void VESAScreen::refresh(const char *src, size_t size) {
    size_t firstline = _info.resolution[0] * FONT_HEIGHT * (_info.bpp / 8);
    size_t len = nre::Math::min<size_t>(size,
            _info.resolution[0] * _info.resolution[1] * (_info.bpp / 8));
    memcpy(reinterpret_cast<void*>(_ds.virt() + firstline), src + firstline, len - firstline);
}

void VESAScreen::draw_char(unsigned xoff, unsigned yoff, char c, uint8_t color) {
    uint8_t cf1 = colors[color & 0xf][2];
    uint8_t cf2 = colors[color & 0xf][1];
    uint8_t cf3 = colors[color & 0xf][0];
    uint8_t cb1 = colors[color >> 4][2];
    uint8_t cb2 = colors[color >> 4][1];
    uint8_t cb3 = colors[color >> 4][0];
    for(unsigned y = 0; y < FONT_HEIGHT; y++) {
        for(unsigned x = 0; x < FONT_WIDTH; x++) {
            if(x < FONT_WIDTH) {
                if(y < FONT_HEIGHT && PIXEL_SET(c,x,y))
                    set_pixel(xoff + x, yoff + y, cf3, cf2, cf1);
                else
                    set_pixel(xoff + x, yoff + y, cb3, cb2, cb1);
            }
            else
                set_pixel(xoff + x, yoff + y, cb3, cb2, cb1);
        }
    }
}

void VESAScreen::set_pixel(unsigned x, unsigned y, uint8_t r, uint8_t g, uint8_t b) {
    uint8_t red = r >> (8 - _info.red_mask_size);
    uint8_t green = g >> (8 - _info.green_mask_size);
    uint8_t blue = b >> (8 - _info.blue_mask_size);
    uint32_t val = (red << _info.red_field_pos) |
            (green << _info.green_field_pos) |
            (blue << _info.blue_field_pos);
    uintptr_t start = _ds.virt() + (y * _info.resolution[0] + x) * (_info.bpp / 8);
    uint8_t *screen = reinterpret_cast<uint8_t*>(start);
    if(_info.memory_model == 6) {
        switch(_info.bpp) {
            case 32:
                screen[3] = val >> 24;
            case 24:
                screen[2] = val >> 16;
            case 16:
                screen[1] = val >> 8;
            case 8:
                screen[0] = val;
                break;
        }
    }
}
