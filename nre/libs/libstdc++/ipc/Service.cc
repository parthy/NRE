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
#include <Logging.h>

namespace nre {

ServiceSession *Service::new_session(const String &args) {
    ScopedLock<UserSm> guard(&_sm);
    for(size_t i = 0; i < MAX_SESSIONS; ++i) {
        if(_sessions[i] == nullptr) {
            LOG(SERVICES, "Creating session " << i << " (caps=" << _caps + (i << CPU::order()) << ")\n");
            add_session(create_session(i, args, _caps + (i << CPU::order()), _func));
            return _sessions[i];
        }
    }
    throw ServiceException(E_CAPACITY, "No free sessions");
}

PORTAL static void cleanup_portal(capsel_t) {
}

void Service::cleanup_thread(void*) {
    Service *s = Thread::current()->get_tls<Service*>(Thread::TLS_PARAM);
    UtcbFrame uf;
    // call an empty portal with the session-Ec
    Pt pt(s->get_thread(CPU::current().log_id()), cleanup_portal);
    pt.call(uf);
    s->_cleanup_sm.up();
}

void Service::add_session(ServiceSession *sess) {
    rcu_assign_pointer(_sessions[sess->id()], sess);
    created_session(sess->id());
}

void Service::do_remove_session(ServiceSession *sess) {
    rcu_assign_pointer(_sessions[sess->id()], nullptr);
    // ensure that nobody can call the portals before we do the following
    sess->destroy();

    // force the Ecs on all CPUs, except ours, to call a portal to ensure that no client is still
    // in a portal and uses the session.
    size_t count = 0;
    for(auto cpu = CPU::begin(); cpu != CPU::end(); ++cpu) {
        if(_reg_cpus.is_set(cpu->log_id()) && cpu->log_id() != CPU::current().log_id()) {
            GlobalThread *gt = GlobalThread::create(cleanup_thread, cpu->log_id(), "sess-cleanup");
            gt->set_tls(GlobalThread::TLS_PARAM, this);
            gt->start();
            count++;
        }
    }
    // wait until it's finished
    while(count-- > 0)
        _cleanup_sm.down();

    RCU::invalidate(sess);
    RCU::gc(true);
}

static UserSm dead_sm;
static size_t dead_count = 0;
static ServiceSession *dead_sessions[Service::MAX_SESSIONS];

void Service::remove_session(ServiceSession *sess) {
    {
        ScopedLock<UserSm> guard(&dead_sm);
        dead_sessions[sess->id()] = sess;
        dead_count++;
    }

    if(Atomic::cmpnswap(&_cleaning, 0, 1)) {
        while(1) {
            size_t i;
            for(i = 0; i < Service::MAX_SESSIONS; ++i) {
                if(dead_sessions[i])
                    break;
            }

            do_remove_session(dead_sessions[i]);

            {
                ScopedLock<UserSm> guard(&dead_sm);
                dead_sessions[i] = nullptr;
                if(--dead_count == 0) {
                    _cleaning = 0;
                    break;
                }
            }
        }
    }
}

void Service::destroy_session(capsel_t pid) {
    ScopedLock<UserSm> guard(&_sm);
    size_t i = (pid - _caps) >> CPU::order();
    LOG(SERVICES, "Destroying session " << i << "\n");
    ServiceSession *sess = rcu_dereference(_sessions[i]);
    if(sess)
        remove_session(sess);
}

}
