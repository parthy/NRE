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

#include "printer.h"

const char *Printer::chars = "0123456789ABCDEF";

void Printer::vprintf(const char *fmt, va_list ap) {
    char c, b, *s;
    int n;
    uint u, base;
    while(1) {
        // wait for a '%'
        while((c = *fmt++) != '%') {
            _putc(c);
            // finished?
            if(c == '\0')
                return;
        }

        switch(c = *fmt++) {
            // signed integer
            case 'd':
            case 'i':
                n = va_arg(ap, int);
                putn(n);
                break;

            // pointer
            case 'p': {
                uintptr_t addr = va_arg(ap, uintptr_t);
                puts("0x");
                putu(addr, 16);
            }
            break;

            // unsigned integer
            case 'b':
            case 'u':
            case 'o':
            case 'x':
            case 'X':
                base = c == 'o' ? 8 : ((c == 'x' || c == 'X') ? 16 : (c == 'b' ? 2 : 10));
                u = va_arg(ap, uint);
                putu(u, base);
                break;

            // string
            case 's':
                s = va_arg(ap, char*);
                puts(s);
                break;

            // character
            case 'c':
                b = (char)va_arg(ap, uint);
                _putc(b);
                break;

            default:
                _putc(c);
                break;
        }
    }
}

void Printer::puts(const char *str) {
    while(*str)
        _putc(*str++);
}
