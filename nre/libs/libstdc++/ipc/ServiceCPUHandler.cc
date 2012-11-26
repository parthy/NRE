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

#include <ipc/ServiceCPUHandler.h>
#include <ipc/Service.h>
#include <ipc/ClientSession.h>
#include <utcb/UtcbFrame.h>
#include <stream/Serial.h>
#include <util/ScopedLock.h>
#include <util/ScopedPtr.h>

namespace nre {

ServiceCPUHandler::ServiceCPUHandler(Service* s, capsel_t pt, cpu_t cpu)
    : _s(s), _session_ec(LocalThread::create(cpu)), _service_ec(LocalThread::create(cpu)),
      _pt(_service_ec, pt, portal), _sm() {
    _service_ec->set_tls<Service*>(Thread::TLS_PARAM, s);
    UtcbFrameRef ecuf(_service_ec->utcb());
    // for session-identification
    ecuf.accept_translates(s->_caps, Service::MAX_SESSIONS_ORDER + CPU::order());
    ecuf.accept_delegates(0);
}

void ServiceCPUHandler::portal(capsel_t) {
    UtcbFrameRef uf;
    Service *s = Thread::current()->get_tls<Service*>(Thread::TLS_PARAM);
    try {
        Service::Command cmd;
        String name;
        uf >> cmd >> name;
        switch(cmd) {
            case Service::OPEN_SESSION: {
                capsel_t cap = uf.get_delegated(0).offset();
                uf.finish_input();

                ServiceSession *sess = s->new_session(cap);
                uf.delegate(CapRange(sess->portal_caps(), 1 << CPU::order(), Crd::OBJ_ALL));
                uf.accept_delegates();
                uf << E_SUCCESS << s->available();
            }
            break;

            case Service::CLOSE_SESSION: {
                capsel_t sid = uf.get_translated().offset();
                uf.finish_input();

                s->destroy_session(sid);
                uf << E_SUCCESS;
            }
            break;

            default:
                VTHROW(Exception, E_ARGS_INVALID, "Unsupported command: " << cmd);
                break;
        }
    }
    catch(const Exception& e) {
        Syscalls::revoke(uf.delegation_window(), true);
        uf.clear();
        uf << e;
    }
}

}
