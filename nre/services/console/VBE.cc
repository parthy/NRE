/*
 * Copyright (C) 2013, Nils Asmussen <nils@os.inf.tu-dresden.de>
 * Copyright (C) 2009, Bernhard Kauer <bk@vmmon.org>
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

#include <kobj/Ports.h>
#include <Logging.h>
#include <Desc.h>
#include <Hip.h>

#include "VBE.h"

using namespace nre;

VBE::VBE() : _enabled(false), _clock(nre::Hip::get().freq_bus * 1000), _mb(&_clock, nullptr),
             _hostmb(&_clock, nullptr),
             _mem(1 << 20, DataSpaceDesc::LOCKED, DataSpaceDesc::RW, 1), _pcicfg("pcicfg"),
             _cpu(), _timeout(), _instructions(), _modecount(), _version(), _modes() {
    _mb.bus_hostop.  add(this, receive_static<MessageHostOp>);
    _mb.bus_timer.   add(this, receive_static<MessageTimer>);
    _mb.bus_hwioin.  add(this, receive_static<MessageHwIOIn>);
    _mb.bus_hwioout. add(this, receive_static<MessageHwIOOut>);
    _mb.bus_hwpcicfg.add(this, receive_static<MessageHwPciConfig>);

    const char *devs[] = {
        "mem", "pit:0x40,0", "scp:0x92,0x61", "pcihostbridge:0,0x100,0xcf8",
        "dpci:3,0,0,0,0,0", "dio:0x3c0+0x20", "dio:0x3b0+0x10", "vcpu", "halifax"
    };
    for(size_t i = 0; i < ARRAY_SIZE(devs); ++i)
        _mb.handle_arg(devs[i]);
    _hostmb.handle_arg("ioio");

    // initialize PIT0
    uint16 pic_init[][2] = {
        {0x43, 0x24}, // let counter0 count with minimal freq of 18.2hz
        {0x40, 0x00},
        {0x43, 0x56}, // let counter1 generate 15usec refresh cycles
        {0x41, 0x12}
    };
    for(size_t i = 0; i < ARRAY_SIZE(pic_init); ++i) {
        MessageIOOut m(MessageIOOut::TYPE_OUTB, pic_init[i][0], pic_init[i][1]);
        _mb.bus_ioout.send(m);
    }

    try {
        // check for VBE
        nre::Console::InfoBlock *p = reinterpret_cast<nre::Console::InfoBlock*>(mem() + (ES_SEG0 << 4));
        p->tag = nre::Console::TAG_VBE2;
        if(vbe_call(0x4f00, ES_SEG0))
            throw Exception(E_NOT_FOUND, "No VBE found");
        if(p->version < 0x200)
            VTHROW(Exception, E_NOT_FOUND, "VBE version " << p->version << " too old ( >= 2.0 required)");
        // we need only the version from the InfoBlock
        _version = p->version;

        // we need only the version from the InfoBlock
        LOG(VESA, "Found VBE:\n");
        LOG(VESA, "   Version: " << fmt(p->version, "#x") << "\n");
        LOG(VESA, "   Tag: " << fmt(p->tag, "#x") << "\n");
        LOG(VESA, "   Memory size: " << fmt(p->memory << 16, "#x") << "\n");
        LOG(VESA, "   OEM: " << vbe_to_ptr<char*>(p->oem_string) << "\n");
        LOG(VESA, "   Vendor: " << vbe_to_ptr<char*>(p->oem_vendor) << "\n");
        LOG(VESA, "   Product: " << vbe_to_ptr<char*>(p->oem_product) << "\n");
        LOG(VESA, "   Product revision: " << vbe_to_ptr<char*>(p->oem_product_rev) << "\n");

        // get modes
        size_t modecount = 0;
        unsigned short *video_mode_ptr = vbe_to_ptr<unsigned short*>(p->video_mode_ptr);
        while(modecount < 32768 && video_mode_ptr[modecount] != 0xffff)
            modecount++;
        _modes = new nre::Console::ModeInfo[modecount + 1];
        add_vga_mode();

        // add modes with linear framebuffer
        for(size_t i = 0; i < modecount; i++) {
            unsigned short mode = vbe_to_ptr<unsigned short*>(p->video_mode_ptr)[i];
            if(!vbe_call(0x4f01, ES_SEG1, mode))
                add_mode(mode, ES_SEG1, 0x81);
        }
        _enabled = true;
    }
    catch(const Exception &e) {
        LOG(VESA, "VESA initialization failed: " << e.msg() << "Disabling it.\n");
        add_vga_mode();
    }
}

void VBE::add_vga_mode() {
    if(!_modes)
        _modes = new nre::Console::ModeInfo[1];
    // add standard vga text mode #3
    _modes[_modecount]._vesa_mode = 3;
    _modes[_modecount].attr = 0x1;
    _modes[_modecount].resolution[0] = 80;
    _modes[_modecount].resolution[1] = 25;
    _modes[_modecount].bytes_per_scanline = 80 * 2;
    _modes[_modecount].bpp = 16;
    _modes[_modecount].phys_base = 0xb8000;
    _modecount++;
}

void VBE::set_mode(size_t index) {
    if(!_enabled)
        return;

    unsigned instructions = _instructions;
    timevalue start = _hostmb.clock()->clock(1000000);

    unsigned mode = _modes[index]._vesa_mode;
    if(_modes[index].attr & 0x80)
        mode |= 0xc000;
    if(vbe_call(0x4f02, ES_SEG0, 0, 0, mode))
        VTHROW(Exception, E_FAILURE, "Unable to switch to mode " << index << ":" << mode);

    timevalue end = _hostmb.clock()->clock(1000000);
    LOG(VESA_DETAIL, "Switch to " << fmt(mode, "#x") << " done (took " << (end - start)
                                  << "us, " << (_instructions - instructions) << " instr.)\n");
}

bool VBE::receive(MessageIOIn &msg) {
    LOG(VESA_DETAIL, "IOIn: port=" << fmt(msg.port, "#x")
                                   << " count=" << msg.count << " type=" << msg.type << "\n");
    return _hostmb.bus_hwioin.send(static_cast<MessageHwIOIn&>(msg), true);
}

bool VBE::receive(MessageIOOut &msg) {
    LOG(VESA_DETAIL, "IOOut: port=" << fmt(msg.port, "#x") << " val=" << msg.value
                                    << " count=" << msg.count << " type=" << msg.type << "\n");
    return _hostmb.bus_hwioout.send(static_cast<MessageHwIOOut&>(msg), true);
}

bool VBE::receive(MessageHwPciConfig &msg) {
    try {
        switch(msg.type) {
            case MessageHwPciConfig::TYPE_READ:
                msg.value = _pcicfg.read(BDF(msg.bdf), msg.dword << 2);
                return true;
            case MessageHwPciConfig::TYPE_WRITE:
                _pcicfg.write(BDF(msg.bdf), msg.dword << 2, msg.value);
                return true;
            case MessageHwPciConfig::TYPE_PTR:
                msg.value = _pcicfg.addr(BDF(msg.bdf), msg.dword << 2);
                return true;
        }
    }
    catch(...) {
        // this happens because pcidirect tries to read 1024 dwords from the PCI config space.
        // just ignore it
    }
    return false;
}

bool VBE::receive(MessageHostOp &msg) {
    switch(msg.type) {
        case MessageHostOp::OP_GUEST_MEM:
            if(msg.value < _mem.size()) {
                msg.ptr = mem() + msg.value;
                msg.len = _mem.size() - msg.value;
                return true;
            }
            return false;

        case MessageHostOp::OP_ALLOC_IOMEM: {
            DataSpace *ds = new DataSpace(msg.len, DataSpaceDesc::LOCKED, DataSpaceDesc::RW, msg.value);
            msg.ptr = reinterpret_cast<char*>(ds->virt());
            return true;
        }

        case MessageHostOp::OP_ALLOC_IOIO_REGION: {
            Ports *p = new Ports(msg.value >> 8, 1 << (msg.value & 0xff));
            LOG(VESA_DETAIL, "Allocated IO ports " << fmt(p->base(), "#x") << " .. "
                                                   << fmt(p->base() + p->count() - 1, "#x") << "\n");
            return true;
        }

        case MessageHostOp::OP_VCPU_BLOCK:
            // invalid value, to abort the loop
            _cpu.actv_state = 0x80000000;
            return true;

        case MessageHostOp::OP_VCPU_CREATE_BACKEND:
            return true;

        case MessageHostOp::OP_ASSIGN_PCI:
        case MessageHostOp::OP_ATTACH_IRQ:
        case MessageHostOp::OP_ATTACH_MSI:
        case MessageHostOp::OP_VCPU_RELEASE:
        case MessageHostOp::OP_NOTIFY_IRQ:
        case MessageHostOp::OP_GET_MODULE:
        case MessageHostOp::OP_GET_MAC:
        case MessageHostOp::OP_VIRT_TO_PHYS:
        case MessageHostOp::OP_ALLOC_FROM_GUEST:
        case MessageHostOp::OP_ALLOC_SEMAPHORE:
        case MessageHostOp::OP_ALLOC_SERVICE_THREAD:
        case MessageHostOp::OP_WAIT_CHILD:
        case MessageHostOp::OP_ALLOC_SERVICE_PORTAL:
        default:
            Util::panic("%s - unimplemented operation %x", __PRETTY_FUNCTION__, msg.type);
    }
}

bool VBE::receive(MessageTimer &msg) {
    switch(msg.type) {
        case MessageTimer::TIMER_NEW:
            msg.nr = TIMER_NR;
            return true;
        case MessageTimer::TIMER_REQUEST_TIMEOUT:
            assert(msg.nr == TIMER_NR);
            _timeout = msg.abstime;
            return true;
        default:
            return false;
    }
}

bool VBE::vbe_call(unsigned eax, unsigned short es_seg, unsigned ecx, unsigned edx, unsigned ebx) {
    memset(&_cpu, 0, sizeof(_cpu));
    _cpu.cr0 = 0x10;
    _cpu.cs.ar = 0x9b;
    _cpu.cs.limit = 0xffff;
    _cpu.ss.ar = 0x93;
    _cpu.ds.ar = _cpu.es.ar = _cpu.fs.ar = _cpu.gs.ar = _cpu.ss.ar;
    _cpu.ld.ar = 0x1000;
    _cpu.tr.ar = 0x8b;
    _cpu.ss.limit = _cpu.ds.limit = _cpu.es.limit = _cpu.fs.limit = _cpu.gs.limit = _cpu.cs.limit;
    _cpu.tr.limit = _cpu.ld.limit = _cpu.gd.limit = _cpu.id.limit = 0xffff;
    _cpu.mtd = Mtd::ALL;
    _cpu.dr7 = 0x400;

    // copy iret frame as well as HLT instruction to be able to come back
    _cpu.ss.sel = SS_SEG;
    _cpu.ss.base = SS_SEG << 4;
    _cpu.esp = 0xfff8;
    unsigned short iret_frame[] = {0xffff, SS_SEG, 0x2, 0xf4f4};
    memcpy(mem() + _cpu.ss.base + _cpu.esp, iret_frame, sizeof(iret_frame));

    // our buffer resides in the ES segment
    _cpu.es.sel = es_seg;
    _cpu.es.base = es_seg << 4;

    // start with int10 executed
    _cpu.eax = eax;
    _cpu.edx = edx;
    _cpu.ecx = ecx;
    _cpu.ebx = ebx;
    _cpu.eip = reinterpret_cast<unsigned short *>(mem())[0x10 * 2 + 0];
    _cpu.cs.sel = reinterpret_cast<unsigned short *>(mem())[0x10 * 2 + 1];
    _cpu.cs.base = _cpu.cs.sel << 4;

    CpuMessage msg0(CpuMessage::TYPE_SINGLE_STEP, &_cpu, MTD_ALL);
    CpuMessage msg1(CpuMessage::TYPE_CHECK_IRQ, &_cpu, MTD_ALL);
    while(!_cpu.actv_state) {
        _instructions++;
        /*Serial::get().writef("[%x] execute at %x:%x esp %x eax %x ecx %x esi %x ebp %x efl %x\n",
                             _instructions, _cpu.cs.sel, _cpu.eip, _cpu.esp, _cpu.eax, _cpu.ecx,
                             _cpu.esi, _cpu.ebp, _cpu.efl);*/
        if(!_mb.last_vcpu->executor.send(msg0)) {
            Util::panic("[%x] nobody to execute at %x:%x esp %x:%x\n",
                        _instructions, _cpu.cs.sel, _cpu.eip, _cpu.ss.sel, _cpu.esp);
        }
        if(!_mb.last_vcpu->executor.send(msg1)) {
            Util::panic("[%x] nobody to execute at %x:%x esp %x:%x\n",
                        _instructions, _cpu.cs.sel, _cpu.eip, _cpu.ss.sel, _cpu.esp);
        }

        if(_mb.clock()->time() > _timeout) {
            MessageTimeout msg2(TIMER_NR, _timeout);
            _timeout = ~0ull;
            _mb.bus_timeout.send(msg2);
        }
    }

    if((_cpu.eax & 0xffff) == 0x004f)
        return false;

    LOG(VESA_DETAIL, "VBE call(" << eax << ", " << ecx << ", " << edx << ", " << ebx << ")"
                     " = " << _cpu.eax << "\n");
    return true;
}

void VBE::add_mode(unsigned short mode, unsigned seg, unsigned min_attributes) {
    nre::Console::ModeInfo *modeinfo = reinterpret_cast<nre::Console::ModeInfo*>(mem() + (seg << 4));
    if((modeinfo->attr & min_attributes) == min_attributes) {
        // keep vesa modenumber for further reference
        modeinfo->_vesa_mode = mode;

        // validate bytes_per_scanline
        if(_version < 0x300 || !modeinfo->bytes_per_scanline)
            modeinfo->bytes_per_scanline = (modeinfo->resolution[0] * modeinfo->bpp / 8);

        memcpy(_modes + _modecount, modeinfo, sizeof(*_modes));
        LOG(VESA, "Mode" << fmt(_modecount, 2) << ": " << fmt(mode, "#x", 3)
                         << " " << ((modeinfo->attr & 0x80) ? "linear" : "window")
                         << " " << fmt(modeinfo->resolution[0], 4) << "x"
                                << fmt(modeinfo->resolution[1], 4) << "x"
                                << fmt(modeinfo->bpp, 2)
                         << " phys " << fmt(modeinfo->phys_base, "#x", 8)
                         << " attr " << fmt(modeinfo->attr, "#x")
                         << " bps " << fmt(modeinfo->bytes_per_scanline, "#0x", 6)
                         << " planes " << modeinfo->planes
                         << " memmodel " << modeinfo->memory_model << "\n");
        _modecount++;
    }
}
