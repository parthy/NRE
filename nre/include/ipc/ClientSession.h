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
#include <collection/SList.h>
#include <ipc/Service.h>
#include <cap/CapSelSpace.h>
#include <kobj/Pt.h>
#include <CPU.h>

namespace nre {

/**
 * The client-part of a session. This way the service can manage per-session-data. That is,
 * it can distinguish between clients.
 */
class ClientSession : public SListItem {
public:
    /**
     * Opens the session at the service specified by the given name with the parent-portal
     *
     * @param service the service name
     * @param args the arguments for the session
     * @throws Exception if the session-creation failed
     */
    explicit ClientSession(const String &service, const String &args = String())
        : SListItem(), _available(), _name(service), _pts(ObjCap::INVALID), _caps(open(args)) {
    }
    /**
     * Opens the session at the service specified by the given name with given portal.
     *
     * @param service the service name
     * @param args the arguments for the session
     * @param pts the portal selectors
     * @throws Exception if the session-creation failed
     */
    explicit ClientSession(const String &service, const String &args, capsel_t pts)
        : SListItem(), _available(), _name(service), _pts(pts), _caps(open(args)) {
    }

    /**
     * Closes the session again
     */
    virtual ~ClientSession() {
        close();
        CapSelSpace::get().free(_caps, 1 << CPU::order());
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

protected:
    capsel_t open(const String &args) {
        // grab session-portals from service
        ScopedCapSels ptcaps(1 << CPU::order(), 1 << CPU::order());
        UtcbFrame uf;
        uf.delegation_window(Crd(ptcaps.get(), CPU::order(), Crd::OBJ_ALL));
        uf << Service::OPEN_SESSION << _name << args;
        call(uf);
        uf.check_reply();
        uf >> _available;
        return ptcaps.release();
    }
    void close() {
        UtcbFrame uf;
        uf.translate(_caps + CPU::current().log_id());
        uf << Service::CLOSE_SESSION << _name;
        call(uf);
        uf.check_reply();
    }

    void call(UtcbFrame &uf) {
        if(_pts != ObjCap::INVALID)
            Pt(_pts + CPU::current().log_id()).call(uf);
        else
            CPU::current().srv_pt().call(uf);
    }

private:
    ClientSession(const ClientSession&);
    ClientSession& operator=(const ClientSession&);

    BitField<Hip::MAX_CPUS> _available;
    String _name;
    capsel_t _pts;
    capsel_t _caps;
};

}
