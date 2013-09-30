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

#pragma once

#include <arch/Types.h>
#include <ipc/ClientSession.h>
#include <ipc/Consumer.h>
#include <mem/DataSpace.h>
#include <util/ScopedPtr.h>

namespace nre {

/**
 * Types for the console service
 */
class Console {
public:
    /**
     * Basic attributes
     */
    static const size_t SUBCONS         = 32;
    static const uint32_t TAG_VBE2      = 0x32454256;

    /**
     * The available commands
     */
    enum Command {
        CREATE,
        GET_REGS,
        SET_REGS,
        GET_MODEINFO,
        SET_MODE
    };

    /**
     * VBE info
     */
    struct InfoBlock {
        uint32_t tag;
        uint16_t version;
        uint32_t oem_string;
        uint32_t caps;
        uint32_t video_mode_ptr;
        uint16_t memory;
        uint16_t oem_revision;
        uint32_t oem_vendor;
        uint32_t oem_product;
        uint32_t oem_product_rev;
    } PACKED;

    /**
     * Used for GET_MODEINFO
     */
    struct ModeInfo {
        uint16_t attr;
        uint16_t win[7];
        uint16_t bytes_scanline;
        uint16_t resolution[2];
        uint8_t char_size[2];
        uint8_t planes;
        uint8_t bpp;
        uint8_t banks;
        uint8_t memory_model;
        uint8_t bank_size;
        uint8_t number_images;
        uint8_t : 8;
        uint8_t red_mask_size;       // Size of direct color red mask
        uint8_t red_field_pos;       // Bit posn of lsb of red mask
        uint8_t green_mask_size;     // Size of direct color green mask
        uint8_t green_field_pos;     // Bit posn of lsb of green mask
        uint8_t blue_mask_size;      // Size of direct color blue mask
        uint8_t blue_field_pos;      // Bit posn of lsb of blue mask
        uint8_t rsvd_mask_size;      // Size of direct color res mask
        uint8_t rsvd_field_pos;      // Bit posn of lsb of res mask
        uint8_t colormode;
        // vbe2
        uint32_t phys_base;
        uint16_t res1[3];
        // vbe3
        uint16_t bytes_per_scanline;
        uint8_t number_images_bnk;
        uint8_t number_images_lin;
        uint8_t vbe3[12];
        // own extensions (needs to be compatible with seouls Vbe::ModeInfoBlock)
        uint8_t : 8;
        uint32_t : 32;
        uint16_t _vesa_mode; // vesa mode number
    } PACKED;

    /**
     * Specifies attributes for the console
     */
    struct Register {
        uint16_t mode;
        uint16_t cursor_style;
        uint32_t cursor_pos;
        size_t offset;
    };

    /**
     * A packet that we receive from the console
     */
    struct ReceivePacket {
        uint flags;
        uint8_t scancode;
        uint8_t keycode;
        char character;
    };
};

/**
 * Represents a session at the console service
 */
class ConsoleSession : public ClientSession {
    static const size_t IN_DS_SIZE      = ExecEnv::PAGE_SIZE;

public:
    /**
     * Creates a new session at given service. That is, it creates a new subconsole attached
     * to the given console.
     *
     * @param service the service name
     * @param console the console to attach to
     * @param title the subconsole title
     * @param mode the mode index to use
     * @param size the size of the framebuffer
     */
    explicit ConsoleSession(const String &service, size_t console, const String &title,
                            size_t mode = 0, size_t size = ExecEnv::PAGE_SIZE * 32)
        : ClientSession(service, build_args(console, mode, title)),
          _in_ds(IN_DS_SIZE, DataSpaceDesc::ANONYMOUS, DataSpaceDesc::RW),
          _out_ds(new DataSpace(size, DataSpaceDesc::ANONYMOUS, DataSpaceDesc::RW)), _sm(0),
          _consumer(_in_ds, _sm, true) {
        create();
    }
    virtual ~ConsoleSession() {
        delete _out_ds;
    }

    /**
     * Sets the mode with index <mode> and allocates and delegates a new dataspace with <size>
     * bytes to the console service.
     *
     * @param mode the mode
     * @param size the size of the dataspace to use a buffer
     */
    void set_mode(size_t mode, size_t size) {
        ScopedPtr<DataSpace> out_ds(new DataSpace(size, DataSpaceDesc::ANONYMOUS, DataSpaceDesc::RW));
        UtcbFrame uf;
        uf << Console::SET_MODE << mode;
        uf.delegate(out_ds->sel());
        Pt(caps() + CPU::current().log_id()).call(uf);
        uf.check_reply();
        delete _out_ds;
        _out_ds = out_ds.release();
    }

    /**
     * @return the screen memory (might be directly mapped or buffered)
     */
    const DataSpace &screen() const {
        return *_out_ds;
    }

    /**
     * Requests the modeinfo for mode at index <idx>.
     *
     * @param idx the mode index
     * @return true if the mode exists
     */
    bool get_mode_info(size_t idx, Console::ModeInfo &info) const {
        UtcbFrame uf;
        uf << Console::GET_MODEINFO << idx;
        Pt(caps() + CPU::current().log_id()).call(uf);
        uf.check_reply();
        bool res;
        uf >> res;
        if(res)
            uf >> info;
        return res;
    }

    /**
     * Requests the current registers
     *
     * @return the registers
     */
    Console::Register get_regs() {
        UtcbFrame uf;
        uf << Console::GET_REGS;
        Pt(caps() + CPU::current().log_id()).call(uf);
        uf.check_reply();
        Console::Register regs;
        uf >> regs;
        return regs;
    }

    /**
     * Sets the given registers
     *
     * @param regs the registers
     */
    void set_regs(const Console::Register &regs) {
        UtcbFrame uf;
        uf << Console::SET_REGS << regs;
        Pt(caps() + CPU::current().log_id()).call(uf);
        uf.check_reply();
    }

    /**
     * @return the consumer to receive packets from the console
     */
    Consumer<Console::ReceivePacket> &consumer() {
        return _consumer;
    }

    /**
     * Receives the next packet from the console. I.e. it waits until the next packet arrives.
     *
     * @return the received packet
     * @throws Exception if it failed
     */
    Console::ReceivePacket receive() {
        Console::ReceivePacket *pk = _consumer.get();
        if(!pk)
            throw Exception(E_ABORT, "Unable to receive console packet");
        Console::ReceivePacket res = *pk;
        _consumer.next();
        return res;
    }

private:
    void create() {
        UtcbFrame uf;
        uf << Console::CREATE;
        uf.delegate(_in_ds.sel(), 0);
        uf.delegate(_out_ds->sel(), 1);
        uf.delegate(_sm.sel(), 2);
        Pt(caps() + CPU::current().log_id()).call(uf);
        uf.check_reply();
    }

    static String build_args(size_t console, size_t mode, const String &title) {
        OStringStream os;
        os << console << " " << mode << " " << title;
        return os.str();
    }

    DataSpace _in_ds;
    DataSpace *_out_ds;
    Sm _sm;
    Consumer<Console::ReceivePacket> _consumer;
};

}
