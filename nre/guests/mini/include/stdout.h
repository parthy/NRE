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

#include "serial.h"
#include "video.h"
#include "printer.h"
#include <stdarg.h>

class Stdout {
public:
    static void init() {
        Serial::init();
        Video::clear();
    }

    static void printf(const char *fmt, ...) {
        {
            va_list ap;
            va_start(ap, fmt);
            Printer(Serial::putc).vprintf(fmt, ap);
            va_end(ap);
        }
        {
            va_list ap;
            va_start(ap, fmt);
            Printer(Video::putc).vprintf(fmt, ap);
            va_end(ap);
        }
    }
};
