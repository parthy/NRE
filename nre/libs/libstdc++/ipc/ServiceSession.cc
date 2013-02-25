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

#include <ipc/Service.h>
#include <ipc/ServiceSession.h>

namespace nre {

ServiceSession::ServiceSession(Service *s, size_t id, portal_func func)
    : SListTreapNode<size_t>(id), RefCounted(), _id(id),
      _caps(CapSelSpace::get().allocate(1 << CPU::order(), 1 << CPU::order())),
      _pts(new Pt *[CPU::count()]) {
    for(uint i = 0; i < CPU::count(); ++i) {
        _pts[i] = nullptr;
        if(s->available().is_set(i)) {
            LocalThread *ec = s->get_thread(i);
            assert(ec != nullptr);
            _pts[i] = new Pt(ec, _caps + i, func);
            _pts[i]->set_id(this);
        }
    }
}

}
