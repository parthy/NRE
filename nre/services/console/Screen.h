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

#include <arch/Types.h>
#include <arch/ExecEnv.h>
#include <services/Console.h>

class Screen {
public:
    explicit Screen() {
    }
    virtual ~Screen() {
    }

    virtual nre::DataSpace &mem() = 0;
    virtual void set_regs(const nre::Console::Register &regs, bool force) = 0;
    virtual void refresh(const char *src, size_t size) = 0;
    virtual void write_tag(const char *tag, size_t len, uint8_t color) = 0;
};
