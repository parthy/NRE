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
#include <utcb/UtcbFrame.h>
#include <Logging.h>

namespace nre {

ServiceCPUHandler::ServiceCPUHandler(Service* s, capsel_t pt, cpu_t cpu)
    : _s(s), _session_ec(LocalThread::create(cpu)), _service_ec(LocalThread::create(cpu)),
      _pt(_service_ec, pt, portal), _sm() {
    _service_ec->set_tls<Service*>(Thread::TLS_PARAM, s);
    UtcbFrameRef ecuf(_service_ec->utcb());
    ecuf.accept_translates();
}

void ServiceCPUHandler::portal(void*) {
    UtcbFrameRef uf;
    Service *s = Thread::current()->get_tls<Service*>(Thread::TLS_PARAM);
    try {
        Service::Command cmd;
        String name;
        uf >> cmd >> name;
        switch(cmd) {
            case Service::OPEN_SESSION: {
                String args;
                uf >> args;
                uf.finish_input();

                ServiceSession *sess = s->new_session(args);
                LOG(SERVICES, "Created session id=" << sess->id() << " args='" << args << "'\n");
                uf.delegate(CapRange(sess->portal_caps(), 1 << CPU::order(), Crd::OBJ_ALL));
                uf.accept_delegates();
                uf << E_SUCCESS << s->available();
            }
            break;

            case Service::CLOSE_SESSION: {
                capsel_t ident = uf.get_translated(0).offset();
                uf.finish_input();

                Reference<ServiceSession> sess = s->get_session_by_ident(ident - CPU::current().log_id());
                LOG(SERVICES, "Destroying session with id=" << sess->id() << "\n");
                s->remove_session(&*sess);
                uf << E_SUCCESS;
            }
            break;

            default:
                VTHROW(Exception, E_ARGS_INVALID, "Unsupported command: " << cmd);
                break;
        }
    }
    catch(const Exception& e) {
        uf.clear();
        uf << e;
    }
}

}
