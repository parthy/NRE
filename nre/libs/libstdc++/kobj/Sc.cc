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

#include <kobj/Sc.h>
#include <kobj/Pt.h>
#include <utcb/UtcbFrame.h>
#include <util/ScopedCapSels.h>

namespace nre {

void Sc::start(const String &name, void *ptr) {
    UtcbFrame uf;
    ScopedCapSels sc;
    uf.delegation_window(Crd(sc.get(), 0, Crd::OBJ_ALL));
    uf << Sc::CREATE << name << ptr << _ec->cpu() << _qpd;
    uf.delegate(_ec->sel());
    sel(sc.get());
    CPU::current().sc_pt().call(uf);
    uf.check_reply();
    uf >> _qpd;
}

}
