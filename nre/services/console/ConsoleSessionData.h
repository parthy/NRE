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

#include <kobj/UserSm.h>
#include <stream/VGAStream.h>
#include <services/Console.h>
#include <collection/DList.h>

#include "ConsoleService.h"

class ConsoleSessionData : public nre::ServiceSession, public nre::DListItem {
public:
    ConsoleSessionData(ConsoleService *srv, size_t id, portal_func func,
                       size_t con, size_t mode, const nre::String &title)
        : ServiceSession(srv, id, func), DListItem(), _has_screen(false), _console(con), _mode(mode),
          _screen(), _title(title), _sm(), _in_ds(), _out_ds(), _in_sm(), _prod(), _regs(), _srv(srv) {
        _regs.offset = nre::VGAStream::TEXT_OFF >> 1;
        _regs.mode = 0;
        _regs.cursor_pos = (nre::VGAStream::ROWS - 1) * nre::VGAStream::COLS + (nre::VGAStream::TEXT_OFF >> 1);
        _regs.cursor_style = 0x0d0e;
    }
    virtual ~ConsoleSessionData() {
        delete _prod;
        delete _in_ds;
        delete _in_sm;
        delete _out_ds;
        delete _screen;
    }

    virtual void invalidate() {
        if(_srv->is_active(this)) {
            // ensure that we don't have the screen; the session might be destroyed before the
            // viewswitcher can handle the switch and therefore, take away the screen, if necessary.
            to_back();
        }
        // remove us from service
        _srv->remove(this);
    }

    nre::UserSm &sm() {
        return _sm;
    }
    size_t mode() const {
        return _mode;
    }
    size_t console() const {
        return _console;
    }
    const nre::String &title() const {
        return _title;
    }
    size_t offset() const {
        return _regs.offset << 1;
    }
    nre::Producer<nre::Console::ReceivePacket> *prod() {
        return _prod;
    }
    nre::DataSpace *out_ds() {
        return _out_ds;
    }

    void create(nre::DataSpace *in_ds, nre::DataSpace *out_ds, nre::Sm *sm);
    void change_mode(nre::DataSpace *out_ds, size_t mode);

    void to_front() {
        if(!_has_screen) {
            swap();
            activate();
            _has_screen = true;
        }
    }
    void to_back() {
        if(_has_screen) {
            swap();
            _has_screen = false;
        }
    }
    void activate() {
        set_mode();
        set_regs(_regs, true);
    }

    Screen *screen() {
        return _screen;
    }
    void set_mode() {
         _srv->mode(mode());
    }
    void set_page(uint page) {
        _regs.offset = (nre::VGAStream::TEXT_OFF >> 1) + (page << 11);
    }
    const nre::Console::Register &regs() const {
        return _regs;
    }
    void set_regs(const nre::Console::Register &regs, bool force = false) {
        _regs = regs;
        _regs.mode = _mode;
        if(_srv->is_active(this))
            _screen->set_regs(_regs, force);
    }

    PORTAL static void portal(ConsoleSessionData *sess);

private:
    void swap() {
        _out_ds->switch_to(_screen->mem());
    }

    bool _has_screen;
    size_t _console;
    size_t _mode;
    Screen *_screen;
    nre::String _title;
    nre::UserSm _sm;
    nre::DataSpace *_in_ds;
    nre::DataSpace *_out_ds;
    nre::Sm *_in_sm;
    nre::Producer<nre::Console::ReceivePacket> *_prod;
    nre::Console::Register _regs;
    ConsoleService *_srv;
};
