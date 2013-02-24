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
#include <ipc/PtClientSession.h>
#include <utcb/UtcbFrame.h>
#include <Exception.h>
#include <CPU.h>

namespace nre {

/**
 * Represents a session at the reboot service
 */
class RebootSession : public PtClientSession {
public:
    /**
     * Creates a new session at given service
     *
     * @param service the service name
     */
    explicit RebootSession(const String &service) : PtClientSession(service) {
    }

    /**
     * Tries to reboot the PC with various methods
     */
    void reboot() {
        UtcbFrame uf;
        pt().call(uf);
        uf.check_reply();
    }
};

}
