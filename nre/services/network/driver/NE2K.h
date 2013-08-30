/*
 * Copyright (C) 2013, Nils Asmussen <nils@os.inf.tu-dresden.de>
 * Copyright (C) 2010, Bernhard Kauer <bk@vmmon.org>
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

#include <kobj/GlobalThread.h>
#include <kobj/Ports.h>
#include <kobj/Gsi.h>
#include <services/Network.h>

#include "../NICDriver.h"
#include "../NetworkService.h"

/**
 * A simple ne2k pci driver, mainly used on qemu devices.
 *
 * Features: reset, send, irq, receive, overflow-recover
 * Missing:  read counters, configuration of full-duplex modes
 * State: testing
 * Documentation: DP8390D, rtl8029, mx98905b
 */
class NE2K : public NICDriver {
    // page0 regs
    enum {
        REG_CR      = 0x0,  // read,write
        REG_PSTART  = 0x1,  // write
        REG_PSTOP   = 0x2,  // write
        REG_BNRY    = 0x3,  // read,write
        REG_TSR     = 0x4,  // read
        REG_TPSR    = 0x4,  // write
        REG_TBCR0   = 0x5,  // write
        REG_TBCR1   = 0x6,  // write
        REG_ISR     = 0x7,  // read,write
        REG_RSAR0   = 0x8,  // write
        REG_RSAR1   = 0x9,  // write
        REG_RBCR0   = 0xa,  // write
        REG_RBCR1   = 0xb,  // write
        REG_RSR     = 0xc,  // read
        REG_RCR     = 0xc,  // write
        REG_TCR     = 0xd,  // write
        REG_DCR     = 0xe,  // write
        REG_IMR     = 0xf,  // write
        REG_DATA    = 0x10  // write
    };
    // page1 regs
    enum {
        REG_PAR0    = 0x1,  // read,write
        REG_CURR    = 0x7,  // read,write
        REG_MAR0    = 0x8,  // read,write
        REG_MAR1    = 0x9,  // read,write
        REG_MAR2    = 0xa,  // read,write
        REG_MAR3    = 0xb,  // read,write
        REG_MAR4    = 0xc,  // read,write
        REG_MAR5    = 0xd,  // read,write
        REG_MAR6    = 0xe,  // read,write
        REG_MAR7    = 0xf,  // read,write
    };
    enum {
        PAGE_SIZE   = 256,
        PG_TX       = 0x40,
        PG_START    = PG_TX + 9216 / PAGE_SIZE, // we allow to send jumbo frames!
        PG_STOP     = 0xc0,
        BUFFER_SIZE = 32768, // our receive buffer
    };

public:
    static void detect(NetworkService &srv, NICList &list);

    explicit NE2K(NetworkService &srv, nre::Ports::port_t port, nre::Gsi *gsi);

    virtual const char *name() const {
        return "NE2K";
    }
    virtual nre::Network::EthernetAddr get_mac() {
        return _mac;
    }
    virtual bool send(const void *packet, size_t size);

private:
    static void irq_thread(void*);
    void access_internal_ram(uint16_t offset, uint16_t dwords, void *buffer, bool read);
    void handle_irq();
    void reset();

    nre::UserSm _sm;
    NetworkService &_srv;
    nre::Ports _ports;
    nre::Gsi *_gsi;
    nre::GlobalThread *_gt;
    uint8_t _next_packet;
    uint8_t _receive_buffer[BUFFER_SIZE];
    nre::Network::EthernetAddr _mac;
};
