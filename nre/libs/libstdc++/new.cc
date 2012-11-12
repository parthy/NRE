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

#include <new>
#include <cstdlib>

// clang generates calls to operator new(unsigned int), because it expects size_t = unsigned int
// but gcc uses size_t = unsigned long and thus generates an operator new(unsigned long) into the
// libsupc++. its a bit funny that this only happens on x86_32 where unsigned int is the same as
// unsigned long anyway :)
// thus, we use this workaround here

#ifdef __clang__
#   ifdef __i386__

namespace std {
class bad_alloc {
};
}

void *operator new(uint size) throw(std::bad_alloc) {
    return malloc(size);
}

void *operator new[](uint size) throw(std::bad_alloc) {
    return malloc(size);
}

#   endif
#endif
