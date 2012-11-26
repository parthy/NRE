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

#include <subsystem/ServiceRegistry.h>
#include <subsystem/Child.h>

namespace nre {

const ServiceRegistry::Service* ServiceRegistry::reg(Child *child, const String &name, capsel_t pts,
                                                     size_t count,
                                                     const BitField<Hip::MAX_CPUS> &available) {
    if(search(name))
        VTHROW(ServiceRegistryException, E_EXISTS, "Service '" << name << "' does already exist");
    Service *s = new Service(child, name, pts, count, available);
    _srvs.append(s);
    return s;
}

void ServiceRegistry::unreg(Child *child, const String &name) {
    Service *s = search(name);
    if(!s)
        VTHROW(ServiceRegistryException, E_NOT_FOUND, "Service '" << name << "' does not exist");
    if(s->child() != child) {
        VTHROW(ServiceRegistryException, E_NOT_FOUND,
               "Child '" << child->cmdline() << "' does not own service '" << name << "'");
    }
    _srvs.remove(s);
    delete s;
}

}
