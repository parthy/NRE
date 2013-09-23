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

#include <stream/IStringStream.h>

#include "ConsoleService.h"
#include "ConsoleSessionData.h"
#include "VGAScreen.h"
#include "VESAScreen.h"

using namespace nre;

ConsoleService::ConsoleService(const char *name, uint modifier)
    : Service(name, CPUSet(CPUSet::ALL), reinterpret_cast<portal_func>(ConsoleSessionData::portal)),
      _vbe(), _reboot("reboot"), _console(), _mode(0), _cons(), _concyc(), _switcher(this),
      _modifier(modifier) {
    // we want to accept two dataspaces
    for(auto it = CPU::begin(); it != CPU::end(); ++it) {
        Reference<LocalThread> t = get_thread(it->log_id());
        UtcbFrameRef uf(t->utcb());
        uf.accept_delegates(2);
    }

    // add dummy session for boot screen and HV screen
    create_dummy(0, "Bootloader");
    create_dummy(1, "Hypervisor");
    _switcher.start();
}

Screen *ConsoleService::create_screen(size_t mode, size_t size) const {
    nre::Console::ModeInfo info;
    if(!_vbe.get_mode_info(mode, info))
        return nullptr;
    if(mode == 0)
        return new VGAScreen;
    return new VESAScreen(_vbe, info.phys_base, size);
}

void ConsoleService::create_dummy(uint page, const String &title) {
    OStringStream args;
    args << 0 << " " << 0 << " " << title;
    ConsoleSessionData *sess = static_cast<ConsoleSessionData*>(new_session(args.str()));
    DataSpace *ds = new DataSpace(ExecEnv::PAGE_SIZE * VGAScreen::PAGES, DataSpaceDesc::ANONYMOUS,
                                  DataSpaceDesc::RW);
    sess->create(nullptr, ds, 0);
    sess->set_page(page);
    memset(reinterpret_cast<void*>(ds->virt()), 0, ExecEnv::PAGE_SIZE * VGAScreen::PAGES);
    memcpy(reinterpret_cast<void*>(ds->virt() + sess->offset()),
           reinterpret_cast<void*>(sess->screen()->mem().virt() + sess->offset()),
           VGAScreen::SIZE);
}

void ConsoleService::up() {
    ScopedLock<UserSm> guard(&_sm);
    SessionReference *old = active();
    iterator it = _concyc[_console]->prev();
    _switcher.switch_to(old ? &*old->sess : nullptr, &*it->sess);
}

void ConsoleService::down() {
    ScopedLock<UserSm> guard(&_sm);
    SessionReference *old = active();
    iterator it = _concyc[_console]->next();
    _switcher.switch_to(old ? &*old->sess : nullptr, &*it->sess);
}

void ConsoleService::left() {
    ScopedLock<UserSm> guard(&_sm);
    left_unlocked();
}

void ConsoleService::left_unlocked() {
    SessionReference *old = active();
    do {
        _console = (_console - 1) % Console::SUBCONS;
    }
    while(_cons[_console] == nullptr);
    iterator it = _concyc[_console]->current();
    _switcher.switch_to(old ? &*old->sess : nullptr, &*it->sess);
}

void ConsoleService::right() {
    ScopedLock<UserSm> guard(&_sm);
    SessionReference *old = active();
    do {
        _console = (_console + 1) % Console::SUBCONS;
    }
    while(_cons[_console] == nullptr);
    iterator it = _concyc[_console]->current();
    _switcher.switch_to(old ? &*old->sess : nullptr, &*it->sess);
}

void ConsoleService::switch_to(size_t console) {
    ScopedLock<UserSm> guard(&_sm);
    if(_cons[console]) {
        SessionReference *old = active();
        _console = console;
        iterator it = _concyc[_console]->current();
        _switcher.switch_to(old ? &*old->sess : nullptr, &*it->sess);
    }
}

ServiceSession *ConsoleService::create_session(size_t id, const String &args, portal_func func) {
    size_t con, mode;
    String title;
    IStringStream is(args);
    is >> con >> mode >> title;
    if(con >= Console::SUBCONS)
        VTHROW(Exception, E_ARGS_INVALID, "Subconsole " << con << " does not exist");
    if(!is_valid_mode(mode))
        VTHROW(Exception, E_ARGS_INVALID, "Mode " << mode << " does not exist");
    return new ConsoleSessionData(this, id, func, con, mode, title);
}

void ConsoleService::remove(ConsoleSessionData *sess) {
    ScopedLock<UserSm> guard(&_sm);
    size_t con = sess->console();
    if(!_cons[con])
        return;

    for(auto it = _cons[con]->begin(); it != _cons[con]->end(); ++it) {
        if(&*it->sess == sess) {
            _cons[con]->remove(&*it);
            break;
        }
    }
    if(_cons[con]->length() == 0) {
        delete _cons[con];
        delete _concyc[con];
        _cons[con] = nullptr;
        _concyc[con] = nullptr;
        if(_console == con)
            left_unlocked();
    }
    else {
        iterator it = _cons[con]->begin();
        _concyc[con]->reset(it, it, _cons[con]->end());
        if(_console == con)
            _switcher.switch_to(nullptr, &*it->sess);
    }
}

void ConsoleService::session_ready(ConsoleSessionData *sess) {
    ScopedLock<UserSm> guard(&_sm);
    SessionReference *old = active();
    SessionReference *newref = new SessionReference;
    newref->sess = Reference<ConsoleSessionData>(sess);
    _console = sess->console();
    if(_cons[_console] == nullptr) {
        _cons[_console] = new nre::DList<SessionReference>();
        _cons[_console]->append(newref);
        _concyc[_console] = new Cycler<iterator>(
            _cons[_console]->begin(), _cons[_console]->end());
    }
    else {
        iterator it = _cons[_console]->append(newref);
        _concyc[_console]->reset(
            _cons[_console]->begin(), it, _cons[_console]->end());
    }
    _switcher.switch_to(old ? &*old->sess : nullptr, sess);
}

bool ConsoleService::handle_keyevent(const Keyboard::Packet &pk) {
    if(~pk.flags & _modifier)
        return false;
    switch(pk.keycode) {
        case Keyboard::VK_1 ... Keyboard::VK_9:
            if(~pk.flags & Keyboard::RELEASE)
                switch_to(1 + pk.keycode - Keyboard::VK_1);
            return true;

        case Keyboard::VK_0:
        case Keyboard::VK_ESC:
            if(~pk.flags & Keyboard::RELEASE)
                switch_to(0);
            return true;

        case Keyboard::VK_END:
            if(~pk.flags & Keyboard::RELEASE)
                _reboot.reboot();
            return true;

        case Keyboard::VK_LEFT:
            if(~pk.flags & Keyboard::RELEASE)
                left();
            return true;

        case Keyboard::VK_RIGHT:
            if(~pk.flags & Keyboard::RELEASE)
                right();
            return true;

        case Keyboard::VK_UP:
            if(~pk.flags & Keyboard::RELEASE)
                up();
            return true;

        case Keyboard::VK_DOWN:
            if(~pk.flags & Keyboard::RELEASE)
                down();
            return true;
    }
    return false;
}
