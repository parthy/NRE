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

#include <stream/IStream.h>
#include <String.h>

namespace nre {

/**
 * Inputstream that read from a string
 */
class IStringStream : public IStream {
public:
    /**
     * Reads a value of type <T> from the given string
     *
     * @param str the string
     * @return the read value
     */
    template<typename T>
    static T read_from(const String &str) {
        IStringStream is(str);
        T t;
        is >> t;
        return t;
    }

    /**
     * Constructor
     *
     * @param str the string (not copied)
     */
    explicit IStringStream(const String &str)
        : IStream(), _str(str), _pos() {
    }

private:
    virtual char read() {
        if(_pos < _str.length())
            return _str.str()[_pos++];
        return '\0';
    }

    const String &_str;
    size_t _pos;
};

}
