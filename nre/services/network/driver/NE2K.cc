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

#include <services/PCIConfig.h>
#include <services/ACPI.h>
#include <util/Clock.h>
#include <util/PCI.h>

#include "NE2K.h"

using namespace nre;

void NE2K::detect(NetworkService &srv, NICList &list) {
    PCIConfigSession pcicfg("pcicfg");
    ACPISession acpi("acpi");
    PCI pci(pcicfg, &acpi);
    try {
        for(uint inst = 0; ; inst++) {
            BDF bdf = pcicfg.search_device(0x2, 0x0, inst);
            if(pcicfg.read(bdf, 0) == 0x802910ec) {
                PCIConfig::value_type port = pcicfg.read(bdf, PCI::BAR0 << 2);
                // must be an ioport
                if((port & 3) != 1 || (port >> 16))
                    continue;

                try {
                    port &= ~3;
                    Gsi *gsi = pci.get_gsi(bdf, 0);
                    NE2K *ne2k = new NE2K(srv, port, gsi);
                    size_t id = list.reg(ne2k);
                    LOG(NET, "Found NE2000 card with id=" << id << ", bdf=" << bdf
                        << ", gsi=" << gsi->gsi() << ", MAC=" << ne2k->get_mac() << "\n");
                }
                catch(const Exception &e) {
                    LOG(NET, "Instantiation of NE2000 driver failed: " << e.msg() << "\n");
                }
            }
        }
    }
    catch(...) {
    }
}

NE2K::NE2K(NetworkService &srv, Ports::port_t port, Gsi *gsi)
        : _sm(1), _srv(srv), _ports(port, 1 << 5), _gsi(gsi),
          _gt(GlobalThread::create(irq_thread, CPU::current().log_id(), "network-irq")) {
    reset();

    // get MAC
    uint16_t buffer[6];
    access_internal_ram(0, 3, buffer, true);
    _mac = Network::EthernetAddr(buffer[0], buffer[1], buffer[2], buffer[3], buffer[4], buffer[5]);

    // start irq-thread
    _gt->set_tls(Thread::TLS_PARAM, this);
    _gt->start();
}

bool NE2K::send(const void *packet, size_t size) {
    ScopedLock<UserSm> guard(&_sm);
    // is a transmit in progress or the packet to large?
    if((_ports.in<uint8_t>(REG_CR) & 4) || (size > (PG_START - PG_TX) * PAGE_SIZE))
        return false;

    // send the packet out
    access_internal_ram(PG_TX * PAGE_SIZE, (size + 3) / 4, const_cast<void *>(packet), false);
    _ports.out<uint8_t>((size & 0xFFU), REG_TBCR0);      // transmit count
    _ports.out<uint8_t>((size >> 8) & 0xFFU, REG_TBCR1); // transmit count
    _ports.out<uint8_t>(0x26, REG_CR);                   // page0, no-dma, transmit, STA

    uint8_t status;
    while((status = _ports.in<uint8_t>(REG_TSR)) == 0)
        Util::pause();
    LOG(NET_DETAIL, "Packet transmission: status=" << status
        << ", rx-status=" << _ports.in<uint8_t>(REG_TSR) << "\n");
    return true;
}

void NE2K::irq_thread(void*) {
    NE2K *ne2k = Thread::current()->get_tls<NE2K*>(Thread::TLS_PARAM);
    while(1) {
        ne2k->_gsi->down();
        LOG(NET_DETAIL, "Got IRQ\n");
        ne2k->handle_irq();
    }
}

/**
 * Read/Write to internal ram of the network card.
 */
void NE2K::access_internal_ram(uint16_t offset, uint16_t dwords, void *buffer, bool read) {
    _ports.out<uint8_t>(0x22, REG_CR);                 // page0 no-remote DMA, STA
    _ports.out<uint8_t>((offset & 0xFFU), REG_RSAR0);
    _ports.out<uint8_t>((offset >> 8) & 0xFFU, REG_RSAR1);
    _ports.out<uint8_t>((dwords * 4) & 0xFFU, REG_RBCR0);
    _ports.out<uint8_t>(((dwords * 4) >> 8) & 0xFFU, REG_RBCR1);
    _ports.out<uint8_t>(read ? 0xa : 0x12, REG_CR);    // read or write remote DMA, STA

    // we use 32bit IO accesses
    uint32_t *dwbuf = reinterpret_cast<uint32_t*>(buffer);
    if(read) {
        for(size_t i = 0; i < dwords; ++i)
            dwbuf[i] = _ports.in<uint32_t>(REG_DATA);
    }
    else {
        for(size_t i = 0; i < dwords; ++i)
            _ports.out<uint32_t>(dwbuf[i], REG_DATA);
    }
}

void NE2K::handle_irq() {
    ScopedLock<UserSm> guard(&_sm);
    // ack them
    uint8_t isr = _ports.in<uint8_t>(REG_ISR);
    _ports.out<uint8_t>(isr, REG_ISR);

    // packet received
    if(isr & 1) {
        // get current page pointer
        _ports.out<uint8_t>(0x62, REG_CR);
        uint8_t current_page = _ports.in<uint8_t>(7);
        _ports.out<uint8_t>(0x22, REG_CR);

        if(current_page != _next_packet) {
            // read all packets from the ring buffer
            size_t pages;
            if(current_page >= _next_packet)
                pages = current_page - _next_packet;
            else
                pages = PG_STOP - _next_packet;
            access_internal_ram(_next_packet * PAGE_SIZE, pages * PAGE_SIZE / 4,
                                _receive_buffer, true);

            // ring buffer wrap around?
            if(current_page < _next_packet) {
                access_internal_ram(PG_START * PAGE_SIZE, (current_page - PG_START) * PAGE_SIZE / 4,
                        _receive_buffer + pages * PAGE_SIZE, true);
                pages += (current_page - PG_START);
            }

            // prog new boundary
            _next_packet = current_page;
            _ports.out<uint8_t>((_next_packet > PG_START) ? (_next_packet - 1) : (PG_STOP - 1), REG_BNRY);

            // now parse the packets and send them upstream
            size_t packet_len = 0;
            for(size_t index = 0; index < pages; index += (4 + packet_len + PAGE_SIZE - 1) / PAGE_SIZE) {
                size_t offset = index * PAGE_SIZE;

                // Please note that we receive only good packages, thus the status bits are not valid!
                packet_len = _receive_buffer[offset + 2] + (_receive_buffer[offset + 3] << 8);
                assert(packet_len + offset < BUFFER_SIZE);

                _srv.broadcast(_receive_buffer + offset + 4, packet_len - 4);
            }
        }
    }

    // overflow -> we simply reset the card
    if(isr & 0x10)
        reset();
}

void NE2K::reset() {
    // reset the card
    _ports.out<uint8_t>(_ports.in<uint8_t>(0x1f), 0x1f);

    // wait up to 1ms for the reset-completed bit in the isr
    nre::Clock clock(1000);
    timevalue_t timeout = 1 + clock.dest_time();
    while((~_ports.in<uint8_t>(REG_ISR) & 0x80) && clock.dest_time() < timeout)
        nre::Util::pause();

    // intialize the card
    unsigned char reset_prog[] = {
        REG_CR,     0x21,      // page0, abort remote DMA, STOP
        REG_DCR,    0x49,      // DCR in dword mode, no loopback and 4byte FIFO
        REG_RBCR0,  0x00,      // zero remote byte count
        REG_RBCR1,  0x00,      // zero remote byte count
        REG_TCR,    0x02,      // transmit: loopback mode
        REG_RSR,    0x20,      // receive:  monitor mode
        REG_TPSR,   PG_TX,     // transmit start
        REG_PSTART, PG_START,  // startpg
        REG_PSTOP,  PG_STOP,   // stoppg
        REG_BNRY,   PG_START,  // boundary
        REG_ISR,    0xff,      // clear isr
        REG_IMR,    0x00,      // set imr

        REG_CR,     0x61,      // page1, abort remote DMA, STOP
        // we do not initialize phys reg, as we use promiscuous mode later on
        REG_CURR,   PG_START + 1, // CURPAGE
        REG_MAR0,   0xff,      // multicast
        REG_MAR1,   0xff,      // multicast
        REG_MAR2,   0xff,      // multicast
        REG_MAR3,   0xff,      // multicast
        REG_MAR4,   0xff,      // multicast
        REG_MAR5,   0xff,      // multicast
        REG_MAR6,   0xff,      // multicast
        REG_MAR7,   0xff,      // multicast

        REG_CR,     0x22,      // page0, START
        REG_ISR,    0xff,      // clear isr
        REG_IMR,    0x11,      // set imr to get RX and overflow IRQ
        REG_TCR,    0x00,      // transmit: normal mode
        REG_RCR,    0x1e,      // receive: small packets, broadcast, multicast and promiscuous
    };
    for(size_t i = 0; i < sizeof(reset_prog) / 2; i++)
        _ports.out<uint8_t>(reset_prog[i * 2 + 1], reset_prog[i * 2]);
    _next_packet = PG_START + 1;
}
