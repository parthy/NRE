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

#include <util/Endian.h>

#include "NetworkService.h"

using namespace nre;

static void print_packet(const char *prefix, size_t len, const void *packet) {
    if(Logging::level & Logging::NET_DETAIL) {
        const char *type;
        const Network::EthernetHeader *header =
                reinterpret_cast<const Network::EthernetHeader*>(packet);
        switch(Endian::ntoh16(header->proto)) {
            case Network::EthernetHeader::PROTO_IP:
                type = "IP";
                break;
            case Network::EthernetHeader::PROTO_ARP:
                type = "ARP";
                break;
            default: {
                static char buffer[12];
                OStringStream os(buffer, sizeof(buffer));
                os << fmt(header->proto, "#0x", 4);
                type = buffer;
                break;
            }
        }
        LOG(NET_DETAIL, prefix << " " << type << " packet of " << len << "b from "
            << Network::EthernetAddr(header->mac_src) << " to "
            << Network::EthernetAddr(header->mac_dst) << "\n");
    }
}

void NetworkSessionData::init(DataSpace *inds, Sm *insm, DataSpace *outds, Sm *outsm) {
    if(_in.ds != nullptr)
        throw Exception(E_EXISTS, "Network session already initialized");
    _in.ds = inds;
    _in.sm = insm;
    _out.ds = outds;
    _out.sm = outsm;
    _cons = new PacketConsumer(*_in.ds, *_in.sm, false);
    _prod = new PacketProducer(*_out.ds, *_out.sm, false);
    _gt = GlobalThread::create(consumer_thread, CPU::current().log_id(),
                                    "network-consumer");
    _gt->set_tls(Thread::TLS_PARAM, this);
    _gt->start();
}

void NetworkSessionData::consumer_thread(void*) {
    NetworkSessionData *sess = Thread::current()->get_tls<NetworkSessionData*>(Thread::TLS_PARAM);
    while(1) {
        void *packet;
        size_t len = sess->_cons->get(packet);
        if(!len)
            break;

        print_packet("Sending", len, packet);
        sess->_driver->send(packet, len);
        sess->_cons->next();
    }
}

NetworkService::NetworkService(NICList &nics, const char *name)
    : Service(name, CPUSet(CPUSet::ALL), reinterpret_cast<portal_func>(portal)),
      _nics(nics) {
    // we want to accept two dataspaces and two sms
    for(auto it = CPU::begin(); it != CPU::end(); ++it) {
        LocalThread *ec = get_thread(it->log_id());
        UtcbFrameRef uf(ec->utcb());
        uf.accept_delegates(2);
    }
}

void NetworkService::broadcast(const void *packet, size_t len) {
    ScopedLock<Service> guard(this);
    print_packet("Received", len, packet);
    for(auto sess = sessions_begin(); sess != sessions_end(); ++sess) {
        if(!static_cast<NetworkSessionData*>(&*sess)->enqueue(packet, len))
            LOG(NET, "Client " << sess->id() << " lost packet of length " << len << "\n");
    }
}

ServiceSession *NetworkService::create_session(size_t id, const String &args, portal_func func) {
    IStringStream is(args);
    size_t nic;
    is >> nic;
    if(!_nics.exists(nic))
        VTHROW(Exception, E_ARGS_INVALID, "NIC (" << nic << ") does not exist");
    return new NetworkSessionData(this, id, func, nic, _nics.get(nic));
}

void NetworkService::portal(NetworkSessionData *sess) {
    UtcbFrameRef uf;
    try {
        Network::Command cmd;
        uf >> cmd;
        switch(cmd) {
            case Network::INIT: {
                capsel_t inds = uf.get_delegated(0).offset();
                capsel_t insm = uf.get_delegated(0).offset();
                capsel_t outds = uf.get_delegated(0).offset();
                capsel_t outsm = uf.get_delegated(0).offset();
                uf.finish_input();
                sess->init(new DataSpace(inds), new Sm(insm, false),
                        new DataSpace(outds), new Sm(outsm, false));
                uf.accept_delegates();
                uf << E_SUCCESS;
            }
            break;

            case Network::GET_INFO: {
                uf.finish_input();
                Network::NIC info;
                info.id = sess->nic();
                info.mac = sess->driver()->get_mac();
                assert(strlen(sess->driver()->name()) < sizeof(info.name));
                strcpy(info.name, sess->driver()->name());
                uf << E_SUCCESS << info;
            }
            break;
        }
    }
    catch(const Exception &e) {
        Syscalls::revoke(uf.delegation_window(), true);
        uf.clear();
        uf << e;
    }
}
