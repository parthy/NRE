/*
 * Copyright (C) 2012, Nils Asmussen <nils@os.inf.tu-dresden.de>
 * Copyright (C) 2009, Bernhard Kauer <bk@vmmon.org>
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
#include <stream/Serial.h>

#include "VGAScreen.h"

using namespace nre;

void VGAScreen::set_regs(const nre::Console::Register &regs, bool force) {
    if(force || regs.cursor_style != _last.cursor_style) {
        write(CURSOR_HI, regs.cursor_style >> 8);
        write(CURSOR_LO, regs.cursor_style);
    }
    if(force || regs.cursor_pos != _last.cursor_pos) {
        uint16_t cursor_offset = regs.cursor_pos - (TEXT_OFF >> 1);
        write(CURSOR_LOC_LO, cursor_offset);
        write(CURSOR_LOC_HI, cursor_offset >> 8);
    }
    if(force || regs.offset != _last.offset) {
        uintptr_t offset = regs.offset - (TEXT_OFF >> 1);
        write(START_ADDR_HI, offset >> 8);
        write(START_ADDR_LO, offset);
    }
    _last = regs;
}

void VGAScreen::write_tag(const char *tag, size_t len, uint8_t color) {
    char *screen = reinterpret_cast<char*>(_ds.virt() + (_last.offset << 1));
    for(uint x = 0; x < COLS; ++x) {
        screen[x * 2] = x < len ? tag[x] : ' ';
        screen[x * 2 + 1] = color;
    }
}

void VGAScreen::refresh(const char *src, size_t size) {
    size_t offset = _last.offset << 1;
    memcpy(reinterpret_cast<void*>(_ds.virt() + offset + COLS * 2),
           src + offset + COLS * 2, nre::Math::min<size_t>(size, SIZE - COLS * 2));
}
