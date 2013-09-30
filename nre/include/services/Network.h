/*
 * Copyright (C) 2012, Nils Asmussen <nils@os.inf.tu-dresden.de>
 * Copyright (C) 2010, Julian Stecklina <jsteckli@os.inf.tu-dresden.de>
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

#include <arch/Types.h>
#include <ipc/PtClientSession.h>
#include <ipc/PacketConsumer.h>
#include <ipc/PacketProducer.h>
#include <utcb/UtcbFrame.h>

namespace nre {

/**
 * Types for the network service
 */
class Network {
public:
    static const size_t MAX_NICS            = 4;

    /**
     * The available commands
     */
    enum Command {
        INIT,
        GET_INFO,
    };

    /**
     * Represents an ethernet address
     */
    class EthernetAddr {
        enum {
            ETHERNET_ADDR_MASK = 0xFFFFFFFFFFFFULL,
        };

    public:
        explicit EthernetAddr() : _raw(0) {
        }
        explicit EthernetAddr(const uint8_t *mac)
            : EthernetAddr(mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]) {
        }
        explicit EthernetAddr(uint8_t b1, uint8_t b2, uint8_t b3, uint8_t b4, uint8_t b5, uint8_t b6) {
            _byte[0] = b1;
            _byte[1] = b2;
            _byte[2] = b3;
            _byte[3] = b4;
            _byte[4] = b5;
            _byte[5] = b6;
        }
        explicit EthernetAddr(uint64_t raw) : _raw(raw & ETHERNET_ADDR_MASK) {
        }

        uint64_t raw() const {
            return _raw;
        }
        bool is_local() const {
            return (_byte[0] & 2) != 0;
        }
        bool is_multicast() const {
            return (_byte[0] & 1) != 0;
        }
        bool is_broadcast() const {
            return (_raw & ETHERNET_ADDR_MASK) == ETHERNET_ADDR_MASK;
        }

        friend inline bool operator==(const EthernetAddr &a, const EthernetAddr &b) {
            return ((a._raw ^ b._raw) & ETHERNET_ADDR_MASK) == 0;
        }
        friend inline OStream &operator<<(OStream &os, const EthernetAddr &a) {
            os << fmt(a._byte[0], "0x", 2) << ":" << fmt(a._byte[1], "0x", 2) << ":"
               << fmt(a._byte[2], "0x", 2) << ":" << fmt(a._byte[3], "0x", 2) << ":"
               << fmt(a._byte[4], "0x", 2) << ":" << fmt(a._byte[5], "0x", 2);
            return os;
        }

    private:
        union {
            uint64_t _raw;
            uint8_t _byte[6];
        };
    };

    /**
     * Header of an Ethernet frame.
     */
    struct EthernetHeader {
        enum {
            PROTO_IP    = 0x0800,
            PROTO_ARP   = 0x0806
        };
        uint8_t mac_dst[6];
        uint8_t mac_src[6];
        uint16_t proto;
    } PACKED;

    /**
     * Describes a NIC (used for GET_INFO)
     */
    struct NIC {
        size_t id;
        EthernetAddr mac;
        char name[64];
    };

private:
    Network();
};

/**
 * Represents a session at the network service
 */
class NetworkSession : public PtClientSession {
public:
    /**
     * Creates a new session at given service
     *
     * @param service the service name
     * @param id the NIC id
     * @param inbuf the size of the input-buffer
     * @param outbuf the size of the output-buffer
     */
    explicit NetworkSession(const String &service, size_t id, size_t inbuf = 32 * 1024,
                            size_t outbuf = 32 * 1024)
        : PtClientSession(service, build_args(id)),
          _inds(inbuf, DataSpaceDesc::ANONYMOUS, DataSpaceDesc::RW), _insm(0),
          _outds(outbuf, DataSpaceDesc::ANONYMOUS, DataSpaceDesc::RW), _outsm(0),
          _cons(_inds, _insm, true), _prod(_outds, _outsm, true) {
        init();
    }

    /**
     * Retrieves information about this NIC.
     *
     * @return the info
     */
    Network::NIC get_info() {
        Network::NIC res;
        UtcbFrame uf;
        uf << Network::GET_INFO;
        pt().call(uf);
        uf.check_reply();
        uf >> res;
        return res;
    }

    /**
     * @return the dataspace for incoming packets
     */
    const DataSpace &inbuf() const {
        return _inds;
    }
    /**
     * @return the dataspace for outgoing packets
     */
    const DataSpace &outbuf() const {
        return _outds;
    }

    /**
     * @return the consumer to get incoming packets
     */
    PacketConsumer &consumer() {
        return _cons;
    }

    /**
     * Sends the given packet.
     *
     * @param buffer the packet
     * @param size the packet-size
     * @return true if successfull
     */
    bool send(const void *buffer, size_t size) {
        return _prod.produce(buffer, size);
    }

private:
    void init() {
        UtcbFrame uf;
        uf.delegate(_outds.sel(), 0);
        uf.delegate(_outsm.sel(), 1);
        uf.delegate(_inds.sel(), 2);
        uf.delegate(_insm.sel(), 3);
        uf << Network::INIT;
        pt().call(uf);
        uf.check_reply();
    }

    static String build_args(size_t id) {
        OStringStream os;
        os << id;
        return os.str();
    }

    DataSpace _inds;
    Sm _insm;
    DataSpace _outds;
    Sm _outsm;
    PacketConsumer _cons;
    PacketProducer _prod;
};

}
