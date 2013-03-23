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

#include <stream/OStringStream.h>
#include <services/Timer.h>
#include <util/Clock.h>
#include <Logging.h>

#include "ViewSwitcher.h"
#include "ConsoleService.h"
#include "ConsoleSessionData.h"

using namespace nre;

char ViewSwitcher::_buffer[256];

ViewSwitcher::ViewSwitcher(ConsoleService *srv)
    : _usm(1), _ds(DS_SIZE, DataSpaceDesc::ANONYMOUS, DataSpaceDesc::RW), _sm(0),
      _prod(_ds, _sm, true), _cons(_ds, _sm, false),
      _ec(GlobalThread::create(switch_thread, CPU::current().log_id(), "console-vs")),
      _srv(srv) {
    _ec->set_tls<ViewSwitcher*>(Thread::TLS_PARAM, this);
}

void ViewSwitcher::switch_to(ConsoleSessionData *from, ConsoleSessionData *to) {
    SwitchCommand cmd;
    cmd.oldsessid = from ? from->id() : -1;
    cmd.sessid = to->id();
    LOG(CONSOLE, "Going to switch from " << cmd.oldsessid << " to " << cmd.sessid << "\n");
    // we can't access the producer concurrently
    ScopedLock<UserSm> guard(&_usm);
    _prod.produce(cmd);
}

void ViewSwitcher::switch_thread(void*) {
    ViewSwitcher *vs = Thread::current()->get_tls<ViewSwitcher*>(Thread::TLS_PARAM);
    nre::Clock clock(1000);
    TimerSession timer("timer");
    timevalue_t until = 0;
    size_t sessid = 0;
    while(1) {
        // are we finished?
        if(until && clock.source_time() >= until) {
            LOG(CONSOLE, "Giving " << sessid << " direct access\n");
            try {
                Reference<ConsoleSessionData> sess = vs->_srv->get_session<ConsoleSessionData>(sessid);
                // finally swap to that session. i.e. give him direct screen access
                ScopedLock<UserSm> guard(&sess->sm());
                sess->to_front();
            }
            catch(const Exception &e) {
                LOG(CONSOLE, e);
                // just ignore it
            }
            until = 0;
        }

        // either block until the next request, or - if we're switching - check for new requests
        if(until == 0 || vs->_cons.has_data()) {
            SwitchCommand *cmd = vs->_cons.get();
            LOG(CONSOLE, "Got switch " << cmd->oldsessid << " to " << cmd->sessid << "\n");
            try {
                // if there is an old one, make a backup and detach him from screen
                if(cmd->oldsessid == sessid && until == 0) {
                    Reference<ConsoleSessionData> old =
                            vs->_srv->get_session<ConsoleSessionData>(sessid);
                    ScopedLock<UserSm> guard(&old->sm());
                    old->to_back();
                }

                {
                    // set the video-mode for that session
                    Reference<ConsoleSessionData> sess =
                            vs->_srv->get_session<ConsoleSessionData>(cmd->sessid);
                    ScopedLock<UserSm> guard(&sess->sm());
                    sess->activate();
                }
            }
            catch(const Exception &e) {
                LOG(CONSOLE, e);
                // just ignore it
            }
            sessid = cmd->sessid;
            // show the tag for 1sec
            until = clock.source_time(SWITCH_TIME);
            vs->_cons.next();
        }

        try {
            Reference<ConsoleSessionData> sess = vs->_srv->get_session<ConsoleSessionData>(sessid);
            ScopedLock<UserSm> guard(&sess->sm());

            // repaint all lines from the buffer except the first
            Screen *screen = sess->screen();
            if(sess->out_ds())
                screen->refresh(reinterpret_cast<char*>(sess->out_ds()->virt()), sess->out_ds()->size());

            // write tag into buffer
            memset(_buffer, 0, sizeof(_buffer));
            OStringStream os(_buffer, sizeof(_buffer));
            os << "Console " << sess->console() << ": " << sess->title() << " (" <<
            sess->id() << ")";

            // write console tag
            screen->write_tag(_buffer, os.length(), COLOR);
        }
        catch(const Exception &e) {
            LOG(CONSOLE, e);
            // if the session is dead, stop switching to it
            until = 0;
            continue;
        }

        // wait 25ms
        LOG(CONSOLE, "Waiting until " << clock.source_time(REFRESH_DELAY) << "\n");
        timer.wait_until(clock.source_time(REFRESH_DELAY));
        LOG(CONSOLE, "Waiting done\n");
    }
}
