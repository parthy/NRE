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

#include "util.h"
#include <stdarg.h>

class Printer {
public:
    typedef void (*putc_func)(char c);

    explicit Printer(putc_func putc) : _putc(putc) {
    }

    void puts(const char* str);
    template<typename T>
    void putn(T n) {
        if(n < 0) {
            _putc('-');
            n = -n;
        }
        if(n >= 10)
            putn<T>(n / 10);
        _putc('0' + (n % 10));
    }
    template<typename T>
    void putu(T u, uint base) {
        if(u >= base)
            putu<T>(u / base, base);
        _putc(chars[(u % base)]);
    }

    void printf(const char *fmt, ...) {
        va_list ap;
        va_start(ap, fmt);
        vprintf(fmt, ap);
        va_end(ap);
    }
    void vprintf(const char *fmt, va_list ap);

private:
    putc_func _putc;
    static const char *chars;
};
