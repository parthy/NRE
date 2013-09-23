/*
 * Copyright (C) 2013, Nils Asmussen <nils@os.inf.tu-dresden.de>
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

#include <mem/DataSpace.h>
#include <kobj/Sm.h>
#include <ipc/ServiceSession.h>
#include <ipc/Service.h>
#include <ipc/PacketProducer.h>
#include <ipc/PacketConsumer.h>
#include <stream/IStringStream.h>

#include "NICList.h"

class NetworkSessionData : public nre::ServiceSession {
    struct Channel {
        Channel() : ds(), sm() {
        }
        ~Channel() {
            delete ds;
            delete sm;
        }
        nre::DataSpace *ds;
        nre::Sm *sm;
    };

public:
    explicit NetworkSessionData(nre::Service *s, size_t id, portal_func func, size_t nic,
                                NICDriver *driver)
        : ServiceSession(s, id, func), _in(), _out(), _cons(), _prod(), _gt(),
          _nic(nic), _driver(driver) {
    }
    virtual ~NetworkSessionData() {
        delete _prod;
        delete _cons;
    }

    virtual void invalidate() {
        if(_cons)
            _cons->stop();
    }

    size_t nic() const {
        return _nic;
    }
    NICDriver *driver() {
        return _driver;
    }
    bool enqueue(const void *packet, size_t len) {
        return _prod->produce(packet, len);
    }

    void init(nre::DataSpace *inds, nre::Sm *insm, nre::DataSpace *outds, nre::Sm *outsm);

private:
    static void consumer_thread(void*);

    Channel _in;
    Channel _out;
    nre::PacketConsumer *_cons;
    nre::PacketProducer *_prod;
    nre::Reference<nre::GlobalThread> _gt;
    size_t _nic;
    NICDriver *_driver;
};

class NetworkService : public nre::Service {
public:
    explicit NetworkService(NICList &nics, const char *name);

    void broadcast(const void *packet, size_t len);

private:
    virtual nre::ServiceSession *create_session(size_t id, const nre::String &args, portal_func func);

    PORTAL static void portal(NetworkSessionData *sess);

private:
    NICList &_nics;
};
