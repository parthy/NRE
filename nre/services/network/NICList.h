/*
 * Copyright (C) 2013, Nils Asmussen <nils@os.inf.tu-dresden.de>
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

#include <services/Network.h>

#include "NICDriver.h"

class NICList {
public:
    typedef NICDriver **iterator;

    explicit NICList() : _drivers(), _count() {
    }

    iterator begin() {
        return _drivers;
    }
    iterator end() {
        return _drivers + _count;
    }

    bool exists(size_t id) {
        return id < _count && _drivers[id] != nullptr;
    }
    NICDriver *get(size_t id) {
        assert(exists(id));
        return _drivers[id];
    }

    size_t reg(NICDriver *driver) {
        assert(_count < nre::Network::MAX_NICS - 1);
        _drivers[_count++] = driver;
        return _count - 1;
    }

private:
    NICDriver *_drivers[nre::Network::MAX_NICS];
    size_t _count;
};
