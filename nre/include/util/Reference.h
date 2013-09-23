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

#include <util/Atomic.h>

namespace nre {

class RefCounted {
public:
    explicit RefCounted() : _refs(1) {
    }

    ulong refcount() const {
        return _refs;
    }
    void add_ref() const {
        Atomic::add(&_refs, +1);
    }
    bool rem_ref() const {
        return Atomic::add(&_refs, -1) == 1;
    }

private:
    mutable ulong _refs;
};

template<class T>
class Reference {
public:
    explicit Reference() : _obj(nullptr) {
    }
    explicit Reference(T *obj) : _obj(obj) {
        attach();
    }
    Reference(const Reference<T> &r) : _obj(r._obj) {
        attach();
    }
    Reference<T> &operator=(const Reference<T> &r) {
        if(&r != this) {
            detach();
            _obj = r._obj;
            attach();
        }
        return *this;
    }
    Reference(Reference<T> &&r) : _obj(r._obj) {
        r._obj = nullptr;
    }
    ~Reference() {
        detach();
    }

    bool valid() const {
        return _obj != nullptr;
    }
    T *operator->() {
        return _obj;
    }
    const T *operator->() const {
        return _obj;
    }
    T &operator*() {
        return *_obj;
    }
    const T &operator*() const {
        return *_obj;
    }

    void unref() {
        if(_obj && _obj->rem_ref())
            delete _obj;
    }

private:
    void attach() {
        if(_obj)
            _obj->add_ref();
    }
    void detach() {
        unref();
        _obj = nullptr;
    }

    T *_obj;
};

}
