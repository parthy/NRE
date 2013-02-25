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

#include <subsystem/Child.h>
#include <subsystem/ChildManager.h>
#include <stream/OStream.h>
#include <utcb/UtcbFrame.h>
#include <CPU.h>

namespace nre {

Child::~Child() {
    delete[] _pts;
    delete _ec;
    delete _pd;
    release_gsis();
    release_ports();
    release_scs();
    release_regs();
    release_sessions();
    CapSelSpace::get().free(_gsi_caps, Hip::MAX_GSIS);
    Atomic::add(&_cm->_child_count, -1);
    Sync::memory_fence();
    _cm->_diesm.up();
}

const ClientSession *Child::open_session(const String &name, const String &args,
                                         const ServiceRegistry::Service *s) {
    ScopedLock<UserSm> guard(&_sm);
    // atm, we simply accept all sessions here. later we might restrict the number of sessions
    // per service and client
    ClientSession *sess;
    if(s)
        sess = new ClientSession(name, args, s->pts());
    else
        sess = new ClientSession(name, args);
    _sessions.append(sess);
    return sess;
}

void Child::close_session(capsel_t handle) {
    ScopedLock<UserSm> guard(&_sm);
    for(auto it = _sessions.begin(); it != _sessions.end(); ++it) {
        if(it->caps() + CPU::current().log_id() == handle) {
            _sessions.remove(&*it);
            delete &*it;
            return;
        }
    }
    VTHROW(Exception, E_NOT_FOUND, "Session with handle " << handle << " not found");
}

void Child::release_gsis() {
    UtcbFrame uf;
    for(uint i = 0; i < Hip::MAX_GSIS; ++i) {
        if(_gsis.is_set(i)) {
            uf << Gsi::RELEASE << i;
            CPU::current().gsi_pt().call(uf);
            uf.clear();
        }
    }
}

void Child::release_ports() {
    UtcbFrame uf;
    for(auto it = _io.begin(); it != _io.end(); ++it) {
        if(it->size) {
            uf << Ports::RELEASE << it->addr << it->size;
            CPU::current().io_pt().call(uf);
            uf.clear();
        }
    }
}

void Child::release_scs() {
    UtcbFrame uf;
    for(auto it = _scs.begin(); it != _scs.end(); ) {
        uf << Sc::STOP;
        uf.translate(it->cap());
        CPU::current().sc_pt().call(uf);
        uf.clear();
        auto cur = it++;
        delete &*cur;
    }
}

void Child::release_regs() {
    ScopedLock<UserSm> guard(&_cm->_sm);
    for(auto it = _regs.begin(); it != _regs.end(); ++it) {
        DataSpaceDesc desc = it->desc();
        if(it->cap() != ObjCap::INVALID && desc.type() != DataSpaceDesc::VIRTUAL)
            _cm->_dsm.release(desc, it->cap());
    }
}

void Child::release_sessions() {
    for(auto it = _sessions.begin(); it != _sessions.end(); ) {
        auto old = it++;
        delete &*old;
    }
}

OStream & operator<<(OStream &os, const Child &c) {
    os << "Child[cmdline='" << c.cmdline() << "' cpu=" << c._ec->cpu();
    os << " entry=" << fmt(reinterpret_cast<void*>(c.entry())) << "]:\n";
    os << "\tScs:\n";
    for(auto it = c.scs().cbegin(); it != c.scs().cend(); ++it)
        os << "\t\t" << it->name() << " on CPU " << CPU::get(it->cpu()).phys_id() << "\n";
    os << "\tGSIs: " << c.gsis() << "\n";
    os << "\tPorts:\n" << c.io();
    os << c.reglist();
    os << "\n";
    return os;
}

}
