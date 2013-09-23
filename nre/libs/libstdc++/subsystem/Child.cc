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
#include <kobj/Sc.h>
#include <CPU.h>

namespace nre {

Child::~Child() {
    delete[] _pts;
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

void Child::alloc_thread(uintptr_t *stack_addr, uintptr_t *utcb_addr) {
    ScopedLock<UserSm> childguard(&_sm);
    // TODO we might leak resources here if something fails
    if(stack_addr) {
        uint align = Math::next_pow2_shift(ExecEnv::STACK_SIZE);
        DataSpaceDesc desc(ExecEnv::STACK_SIZE, DataSpaceDesc::ANONYMOUS,
                           DataSpaceDesc::RW, 0, 0, align - ExecEnv::PAGE_SHIFT);
        const DataSpace &ds = _cm->_dsm.create(desc);
        *stack_addr = _regs.find_free(ds.size(), ExecEnv::STACK_SIZE);
        _regs.add(ds.desc(), *stack_addr, ds.flags() | ChildMemory::OWN, ds.unmapsel());
    }
    if(utcb_addr) {
        DataSpaceDesc desc(ExecEnv::PAGE_SIZE, DataSpaceDesc::VIRTUAL, DataSpaceDesc::RW);
        *utcb_addr = _regs.find_free(ExecEnv::PAGE_SIZE);
        _regs.add(desc, *utcb_addr, desc.flags());
    }
}

capsel_t Child::create_thread(capsel_t ec, const String &name, ulong id, cpu_t cpu, Qpd &qpd) {
    // TODO later one could add policy here and adjust the qpd accordingly
    capsel_t sc;
    {
        UtcbFrame puf;
        puf.accept_delegates(0);
        // we don't want to join this thread
        puf << Sc::CREATE << name << 0 << cpu << qpd;
        puf.delegate(ec);
        CPU::current().sc_pt().call(puf);
        puf.check_reply();
        sc = puf.get_delegated(0).offset();
        puf >> qpd;
    }

    ScopedLock<UserSm> guard(&_sm);
    _scs.append(new SchedEntity(id, name, cpu, sc));
    LOG(ADMISSION, "Child '" << cmdline() << "' created sc " << id << ":"
                             << name << " on cpu " << cpu << " (" << sc << ")\n");
    return sc;
}

Child::SchedEntity *Child::get_thread_by_id(ulong id) {
    for(auto it = _scs.begin(); it != _scs.end(); ++it) {
        if(it->id() == id)
            return &*it;
    }
    return nullptr;
}

Child::SchedEntity *Child::get_thread_by_cap(capsel_t cap) {
    for(auto it = _scs.begin(); it != _scs.end(); ++it) {
        if(it->cap() == cap)
            return &*it;
    }
    return nullptr;
}

void Child::join_thread(ulong id, capsel_t sm) {
    // ensure that the thread can't terminate between join() and creation of the JoinItem
    ScopedLock<UserSm> guard(&_sm);
    // if we've found it, enqueue the sm, so that we can up it on thread-termination
    // id == 0 means, wait until all other threads are dead (the main thread is not included here)
    if((id > 0 && get_thread_by_id(id)) || (id == 0 && _scs.length() > 0))
        _joins.append(new JoinItem(id, sm));
    // otherwise the thread is already dead, so just up the Sm to let the caller continue.
    else
        Sm(sm, true).up();
}

void Child::term_thread(ulong id, uintptr_t stack, uintptr_t utcb) {
    ScopedLock<UserSm> guard(&_sm);
    SchedEntity *se = get_thread_by_id(id);
    if(!se)
        return;

    // 0 indicates that this thread has used its own stack
    if(stack) {
        capsel_t sel;
        DataSpaceDesc desc = _regs.remove_by_addr(stack, &sel);
        _cm->_dsm.release(desc, sel);
    }
    // TODO if(utcb)
    //   c->reglist().remove_by_addr(utcb);

    for(auto it = _joins.begin(); it != _joins.end(); ) {
        auto old = it++;
        // waits for this thread or until all others are dead?
        if(old->id() == id || (old->id() == 0 && _scs.length() == 1)) {
            old->sm().up();
            _joins.remove(&*old);
            delete &*old;
        }
    }
    destroy_thread(se);
}

void Child::remove_thread(capsel_t cap) {
    ScopedLock<UserSm> guard(&_sm);
    Child::SchedEntity *se = get_thread_by_cap(cap);
    if(se)
        destroy_thread(se);
}

void Child::destroy_thread(SchedEntity *se) {
    {
        UtcbFrame puf;
        puf << Sc::DESTROY;
        puf.translate(se->cap());
        CPU::current().sc_pt().call(puf);
        puf.check_reply();
    }

    LOG(ADMISSION, "Child '" << cmdline() << "' destroyed sc " << se->id() << ":" << se->name() << "\n");
    _scs.remove(se);
    delete se;
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
        uf << Sc::DESTROY;
        uf.translate(it->cap());
        CPU::current().sc_pt().call(uf);
        uf.clear();
        auto old = it++;
        delete &*old;
    }
    for(auto it = _joins.begin(); it != _joins.end(); ) {
        auto old = it++;
        delete &*old;
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
