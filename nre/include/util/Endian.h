/*
 * Copyright (C) 2013, Nils Asmussen <nils@os.inf.tu-dresden.de>
 * Copyright (C) 2010, Julian Stecklina <jsteckli@os.inf.tu-dresden.de>
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

namespace nre {

class Endian {
public:
    static uint16_t hton16(uint16_t value) {
        asm volatile ("xchg %b0, %h0" : "+Q"(value));
        return value;
    }
    static uint16_t ntoh16(uint16_t value) {
        asm volatile ("xchg %b0, %h0" : "+Q"(value));
        return value;
    }
    static uint32_t hton32(uint32_t value) {
        asm volatile ("bswap %0" : "+r"(value));
        return value;
    }
    static uint32_t ntoh32(uint32_t value) {
        asm volatile ("bswap %0" : "+r"(value));
        return value;
    }
    static uint64_t hton64(uint64_t value) {
#ifdef __i386__
        return static_cast<uint64_t>(hton32(value)) << 32 | hton32(value >> 32);
#else
        asm volatile ("bswap %0" : "+r"(value));
        return value;
#endif
    }

private:
    Endian();
};

}
