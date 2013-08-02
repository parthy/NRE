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

#include <ipc/Service.h>
#include <services/Keyboard.h>
#include <services/Reboot.h>
#include <collection/Cycler.h>
#include <collection/DList.h>

#include "VBE.h"
#include "Screen.h"
#include "ViewSwitcher.h"

class ConsoleSessionData;

class ConsoleService : public nre::Service {
public:
    struct SessionReference : public nre::DListItem {
        nre::Reference<ConsoleSessionData> sess;
    };

    typedef nre::DList<SessionReference>::iterator iterator;

    ConsoleService(const char *name, uint modifier);

    SessionReference *active() {
        if(_concyc[_console] == nullptr)
            return nullptr;
        iterator it = _concyc[_console]->current();
        return it != _cons[_console]->end() ? &*it : nullptr;
    }
    bool is_active(ConsoleSessionData *sess) {
        SessionReference *ref = active();
        return ref && &*ref->sess == sess;
    }

    void remove(ConsoleSessionData *sess);
    void up();
    void down();
    void left();
    void left_unlocked();
    void right();

    bool get_mode_info(size_t idx, nre::Console::ModeInfo &info) {
        return _vbe.get_mode_info(idx, info);
    }
    bool is_valid_mode(size_t idx) const {
        return idx < static_cast<size_t>(_vbe.end() - _vbe.begin());
    }
    size_t idx_from_mode(uint16_t mode) {
        size_t i = 0;
        for(auto it = _vbe.begin(); it != _vbe.end(); ++it, ++i) {
            if(it->_vesa_mode == mode)
                return i;
        }
        VTHROW(Exception, E_NOT_FOUND, "Mode " << mode << " not found");
        return 0;
    }
    size_t mode() const {
        return _mode;
    }
    void mode(size_t mode) {
        if(_mode != mode) {
            _vbe.set_mode(mode);
            _mode = mode;
        }
    }
    Screen *create_screen(size_t mode, size_t size) const;

    ViewSwitcher &switcher() {
        return _switcher;
    }
    void session_ready(ConsoleSessionData *sess);
    bool handle_keyevent(const nre::Keyboard::Packet &pk);

private:
    virtual nre::ServiceSession *create_session(size_t id, const nre::String &args, portal_func func);
    void create_dummy(uint page, const nre::String &title);
    void switch_to(size_t console);

    VBE _vbe;
    nre::RebootSession _reboot;
    size_t _console;
    size_t _mode;
    nre::DList<SessionReference> *_cons[nre::Console::SUBCONS];
    nre::Cycler<iterator> *_concyc[nre::Console::SUBCONS];
    nre::UserSm _sm;
    ViewSwitcher _switcher;
    uint _modifier;
};
