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

#pragma once

#include <arch/ExecEnv.h>
#include <kobj/Ports.h>
#include <mem/DataSpace.h>
#include <services/Console.h>

#include "ConsoleSessionData.h"
#include "Screen.h"
#include "VBE.h"

class VESAScreen : public Screen {
public:
    explicit VESAScreen(const VBE &vbe, uintptr_t phys, size_t size)
        : Screen(), _vbe(vbe), _ds(size, nre::DataSpaceDesc::ANONYMOUS, nre::DataSpaceDesc::RW, phys),
          _info(), _last() {
    }

    virtual nre::DataSpace &mem() {
        return _ds;
    }
    virtual void set_regs(const nre::Console::Register &, bool);
    virtual void write_tag(const char *tag, size_t len, uint8_t color);
    virtual void refresh(const char *src, size_t size);

private:
    void draw_char(unsigned x, unsigned y, char c, uint8_t color);
    void set_pixel(unsigned x, unsigned y, uint8_t r, uint8_t g, uint8_t b);

    const VBE &_vbe;
    nre::DataSpace _ds;
    nre::Console::ModeInfo _info;
    nre::Console::Register _last;
};
