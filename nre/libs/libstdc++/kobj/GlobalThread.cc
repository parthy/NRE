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

#include <arch/Startup.h>
#include <kobj/GlobalThread.h>
#include <kobj/Sm.h>
#include <kobj/Sc.h>
#include <kobj/Pt.h>
#include <cap/CapSelSpace.h>
#include <utcb/UtcbFrame.h>

namespace nre {

ulong GlobalThread::_next_id = 1;
GlobalThread GlobalThread::_cur INIT_PRIO_GEC(_startup_info.utcb, CapSelSpace::INIT_EC,
                                              CapSelSpace::INIT_SC, _startup_info.cpu,
                                              &Pd::_cur, _startup_info.stack);

void GlobalThread::join(ulong id) {
    // delegate a semaphore to the parent which will up it as soon as the thread terminates. we
    // have to give him the Sm because the parent needs to release its resources and this should
    // of couse not revoke the Sm for us here.
    Sm sm(0);
    UtcbFrame uf;
    uf << Sc::JOIN << id;
    uf.delegate(sm.sel());
    CPU::current().sc_pt().call(uf);
    uf.check_reply();
    sm.down();
}

GlobalThread::GlobalThread(uintptr_t uaddr, capsel_t gt, capsel_t sc, cpu_t cpu, Pd *pd, uintptr_t stack)
    : Thread(Hip::get().cpu_phys_to_log(cpu), 0, gt, stack, uaddr), _id(0), _sc(new Sc(this, sc)),
      _name("main") {
    ExecEnv::set_current_thread(this);
    ExecEnv::set_current_pd(pd);
}

GlobalThread::~GlobalThread() {
    delete _sc;
}

void GlobalThread::start(Qpd qpd) {
    assert(_sc == nullptr);
    _sc = new Sc(this, qpd);
    _sc->start(_name, _id);
}

}
