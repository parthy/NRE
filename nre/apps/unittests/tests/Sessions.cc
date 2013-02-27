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
#include <ipc/PtClientSession.h>
#include <subsystem/ChildManager.h>
#include <kobj/Pt.h>
#include <utcb/UtcbFrame.h>
#include <util/Profiler.h>
#include <CPU.h>

#include "Sessions.h"

using namespace nre;
using namespace nre::test;

class MyService;
static void test_sessions();

const TestCase sessions = {
    "Sessions", test_sessions
};

typedef void (*client_func)(AvgProfiler &prof, Pt &pt, UtcbFrame &uf, uint &sum);

static const uint TEST_COUNT = 100;
static MyService *srv;

class MySession : public ServiceSession {
public:
    explicit MySession(Service *s, size_t id, portal_func func) : ServiceSession(s, id, func) {
    }
    virtual ~MySession();
};

class MyService : public Service {
public:
    explicit MyService(portal_func func)
        : Service("myservice", CPUSet(CPUSet::ALL), func), last_seen() {
    }

    virtual ServiceSession *create_session(size_t id, const String &, portal_func func) {
        return new MySession(this, id, func);
    }

    size_t last_seen;
};

MySession::~MySession() {
    if(srv->last_seen == CPU::count())
        srv->stop();
}

PORTAL static void portal_empty(void*) {
    UtcbFrameRef uf;
    bool last;
    uf >> last;
    if(last)
        Atomic::add(&srv->last_seen, +1);
}

static int sessions_server(int, char *[]) {
    srv = new MyService(portal_empty);
    srv->start();
    delete srv;
    return 0;
}

static void client_thread(void*) {
    for(size_t i = 0; i < TEST_COUNT; ++i) {
        PtClientSession sess("myservice");
        UtcbFrame uf;
        uf << (i == TEST_COUNT - 1);
        sess.pt(CPU::current().log_id()).call(uf);
    }
}

static int sessions_client(int, char *[]) {
    ulong ids[CPU::count()];
    for(CPU::iterator cpu = CPU::begin(); cpu != CPU::end(); ++cpu) {
        GlobalThread *gt = GlobalThread::create(client_thread, cpu->log_id(), "mythread");
        ids[cpu->log_id()] = gt->id();
        gt->start();
    }
    for(size_t i = 0; i < ARRAY_SIZE(ids); ++i)
        GlobalThread::join(ids[i]);
    return 0;
}

static void test_sessions() {
    ChildManager *mng = new ChildManager();
    Hip::mem_iterator self = Hip::get().mem_begin();
    // map the memory of the module
    DataSpace ds(self->size, DataSpaceDesc::ANONYMOUS, DataSpaceDesc::R, self->addr);
    {
        ChildConfig cfg(0, "sessions-service provides=myservice");
        cfg.entry(reinterpret_cast<uintptr_t>(sessions_server));
        mng->load(ds.virt(), self->size, cfg);
    }
    {
        ChildConfig cfg(0, "sessions-client");
        cfg.entry(reinterpret_cast<uintptr_t>(sessions_client));
        mng->load(ds.virt(), self->size, cfg);
    }
    while(mng->count() > 0)
        mng->dead_sm().down();
    delete mng;
}
