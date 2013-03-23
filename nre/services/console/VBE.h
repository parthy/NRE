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

#include <nul/motherboard.h>
#include <nul/vcpu.h>
#include <executor/cpustate.h>
#include <mem/DataSpace.h>
#include <services/PCIConfig.h>
#include <services/Console.h>

class VBE : public StaticReceiver<VBE> {
    enum {
      SS_SEG   = 0x1000,
      ES_SEG0  = 0x2000,
      ES_SEG1  = 0x3000,
      TIMER_NR = 2
    };

public:
    typedef const nre::Console::ModeInfo *iterator;

    explicit VBE();

    iterator begin() const {
        return _modes;
    }
    iterator end() const {
        return _modes + _modecount;
    }

    bool get_mode_info(size_t index, nre::Console::ModeInfo &info) const {
        if(index >= _modecount)
            return false;
        info = _modes[index];
        return true;
    }
    void set_mode(size_t index);

    bool receive(MessageIOIn &msg);
    bool receive(MessageIOOut &msg);
    bool receive(MessageHwPciConfig &msg);
    bool receive(MessageHostOp &msg);
    bool receive(MessageTimer &msg);

private:
    bool vbe_call(unsigned eax, unsigned short es_seg, unsigned ecx = 0, unsigned edx = 0,
                  unsigned ebx = 0);

    void add_mode(unsigned short mode, unsigned seg, unsigned min_attributes);
    char *mem() const {
        return reinterpret_cast<char*>(_mem.virt());
    }
    template<typename T>
    T vbe_to_ptr(unsigned ptr) const {
        return reinterpret_cast<T>(mem() + (ptr & 0xffff) + ((ptr >> 12) & 0xffff0));
    }

    ::Clock _clock;
    Motherboard _mb;
    Motherboard _hostmb;
    nre::DataSpace _mem;
    nre::PCIConfigSession _pcicfg;
    CpuState _cpu;
    timevalue_t _timeout;
    unsigned _instructions;
    size_t _modecount;
    unsigned short _version;
    nre::Console::ModeInfo *_modes;
};
