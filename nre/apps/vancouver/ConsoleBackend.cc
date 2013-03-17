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

#include <services/Timer.h>
#include <services/Console.h>
#include <util/Clock.h>

#include "ConsoleBackend.h"
#include "Vancouver.h"

using namespace nre;

void ConsoleBackend::thread(void*) {
    ConsoleBackend *c = Thread::current()->get_tls<ConsoleBackend*>(Thread::TLS_PARAM);
    TimerSession &timer = c->_vc->timeouts().session();
    ConsoleSession &cons = c->_vc->console();
    nre::Clock clock(1000);
    while(1) {
        if(c->_current != MAX_VIEWS) {
            ConsoleView &view = c->_views[c->_current];
            nre::Console::Register regs;
            regs.mode = view.regs->mode;
            regs.cursor_pos = view.regs->cursor_pos;
            regs.cursor_style = view.regs->cursor_style;
            regs.offset = view.regs->offset;
            cons.set_regs(regs);

            memcpy(reinterpret_cast<char*>(cons.screen().virt()) + (view.regs->offset << 1),
                   view.ptr + (view.regs->offset << 1), 80 * 25 * 2);
        }
        timer.wait_until(clock.source_time(25, 1000));
    }
}
