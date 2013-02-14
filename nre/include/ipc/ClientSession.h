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

#include <utcb/UtcbFrame.h>
#include <ipc/Service.h>
#include <cap/CapSelSpace.h>
#include <kobj/Pt.h>
#include <CPU.h>

namespace nre {

/**
 * The client-part of a session. This way the service can manage per-session-data. That is,
 * it can distinguish between clients.
 */
class ClientSession {
public:
    /**
     * Opens the session at the service specified by the given name
     *
     * @param service the service name
     * @throws Exception if the session-creation failed
     */
    explicit ClientSession(const char *service)
        : _available(), _name(service), _pt(CPU::current().srv_pt()),
          _caps(open(Pd::current()->sel())) {
    }
    /**
     * Opens a session with portal <pt> at service with given name and delegates <cap>
     *
     * @param service the service name
     * @param cap the cap to delegate
     * @param pt the portal to call
     * @throws Exception if the session-creation failed
     */
    explicit ClientSession(const char *service, capsel_t cap, Pt &pt)
        : _available(), _name(service), _pt(pt), _caps(open(cap)) {
    }

    /**
     * Closes the session again
     */
    virtual ~ClientSession() {
        try {
            close();
            CapSelSpace::get().free(_caps, 1 << CPU::order());
        }
        catch(...) {
            // ignore exceptions in the destructor
        }
    }

    /**
     * @return the name of the service
     */
    const String &service() const {
        return _name;
    }
    /**
     * @return the bitmask that indicates on what CPUs the service is available
     */
    const BitField<Hip::MAX_CPUS> &available() const {
        return _available;
    }
    /**
     * @param log_id the logical CPU id
     * @return true if you can use the given CPU to talk to the service
     */
    bool available_on(cpu_t log_id) const {
        return _available.is_set(log_id);
    }
    /**
     * @return the base of the capabilities received to communicate with the service
     */
    capsel_t caps() const {
        return _caps;
    }

private:
    capsel_t open(capsel_t cap) {
        UtcbFrame uf;
        ScopedCapSels caps(1 << CPU::order(), 1 << CPU::order());
        uf.delegation_window(Crd(caps.get(), CPU::order(), Crd::OBJ_ALL));
        // we delegate a cap because it will be revoked if we get killed. at the moment, it has
        // to be a capability that is explicitly revoked by the parent. later, this doesn't matter
        // because NOVA will revoke all caps of this Pd.
        uf.delegate(cap);
        uf << Service::OPEN_SESSION << _name;
        _pt.call(uf);
        uf.check_reply();
        uf >> _available;
        return caps.release();
    }

    void close() {
        UtcbFrame uf;
        // + cpu-number because we can't be sure that portals on other CPUs exist
        uf.translate(_caps + CPU::current().log_id());
        uf << Service::CLOSE_SESSION << _name;
        _pt.call(uf);
    }

    ClientSession(const ClientSession&);
    ClientSession& operator=(const ClientSession&);

    BitField<Hip::MAX_CPUS> _available;
    String _name;
    Pt &_pt;
    capsel_t _caps;
};

}
