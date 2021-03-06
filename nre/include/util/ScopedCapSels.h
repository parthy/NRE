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

#include <cap/CapSelSpace.h>

namespace nre {

/**
 * Allows RAII semantics for capability selectors. The construction of ScopedCapSels reserves the
 * specified amount of cap selectors and as soon as you don't explicitly release them (release()),
 * they are free'd in the destructor.
 */
class ScopedCapSels {
public:
    /**
     * Constructor. Allocates <count> capability selectors, aligned by <align>.
     *
     * @param count the number of cap selectors
     * @param align the alignment
     */
    explicit ScopedCapSels(uint count = 1, uint align = 1)
        : _cap(CapSelSpace::get().allocate(count, align)), _count(count), _owned(true) {
    }
    /**
     * Destructor. Free's the cap selectors, as soon as you haven't called release().
     */
    ~ScopedCapSels() {
        if(_owned)
            CapSelSpace::get().free(_cap, _count);
    }

    /**
     * @return the beginning of the allocated selector range
     */
    capsel_t get() const {
        return _cap;
    }

    /**
     * Specifies that the capability selectors are in use now and should not be free'd when
     * destructing this object
     *
     * @return the beginning of the allocated selector range
     */
    capsel_t release() {
        _owned = false;
        return _cap;
    }

private:
    ScopedCapSels(const ScopedCapSels&);
    ScopedCapSels& operator=(const ScopedCapSels&);

private:
    capsel_t _cap;
    uint _count;
    bool _owned;
};

}
