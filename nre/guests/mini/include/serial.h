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

#include "ports.h"

class Serial {
    enum {
        COM1    = 0x3F8,
        COM2    = 0x2E8,
        COM3    = 0x2F8,
        COM4    = 0x3E8
    };
    enum {
        DLR_LO  = 0,
        DLR_HI  = 1,
        IER     = 1,    // interrupt enable register
        FCR     = 2,    // FIFO control register
        LCR     = 3,    // line control register
        MCR     = 4,    // modem control register
    };

public:
    static void init() {
        Ports::out<uint8_t>(COM1 + LCR, 0x80);          // Enable DLAB (set baud rate divisor)
        Ports::out<uint8_t>(COM1 + DLR_LO, 0x01);       // Set divisor to 1 (lo byte) 115200 baud
        Ports::out<uint8_t>(COM1 + DLR_HI, 0x00);       //                  (hi byte)
        Ports::out<uint8_t>(COM1 + LCR, 0x03);          // 8 bits, no parity, one stop bit
        Ports::out<uint8_t>(COM1 + IER, 0);             // disable interrupts
        Ports::out<uint8_t>(COM1 + FCR, 7);
        Ports::out<uint8_t>(COM1 + MCR, 3);
    }

    static void putc(char c) {
        if(c == '\0')
            return;

        if(c == '\n')
            putc('\r');
        while((Ports::in<uint8_t>(COM1 + 5) & 0x20) == 0)
            ;
        Ports::out<uint8_t>(COM1, c);
    }
};
