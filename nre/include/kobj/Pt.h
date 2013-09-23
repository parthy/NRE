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

#include <arch/ExecEnv.h>
#include <kobj/ObjCap.h>
#include <kobj/LocalThread.h>
#include <utcb/UtcbFrame.h>
#include <util/ScopedCapSels.h>
#include <Syscalls.h>

namespace nre {

/**
 * Represents a portal. A portal is always bound to a LocalThread.
 */
class Pt : public ObjCap {
public:
    typedef PORTAL void (*portal_func)(void*);

    /**
     * Attaches a portal object to the given portal-capability-selector. The destructor will neither
     * free the selector nor the capability.
     *
     * @param pt the capability-selector for the portal
     */
    explicit Pt(capsel_t pt) : ObjCap(pt, KEEP_CAP_BIT | KEEP_SEL_BIT) {
    }

    /**
     * Creates a portal for <func> at selector <pt> that is bound to the given Thread. The destructor
     * will not free the selector, but only the capability.
     *
     * @param ec the LocalThread to bind the portal to
     * @param pt the capability selector to use
     * @param func the portal function
     * @param mtd the message-transfer descriptor to describe what information should the
     *  kernel pass to the portal
     */
    explicit Pt(const Reference<LocalThread> &ec, capsel_t pt, portal_func func, Mtd mtd = Mtd())
        : ObjCap(pt, KEEP_SEL_BIT) {
        create(ec, pt, reinterpret_cast<uintptr_t>(func), mtd);
    }

    /**
     * Creates a portal for <func> that is bound to the given Thread.
     *
     * @param ec the LocalThread to bind the portal to
     * @param func the portal function
     * @param mtd the message-transfer descriptor to describe what information should the
     *  kernel pass to the portal
     */
    explicit Pt(const Reference<LocalThread> &ec, portal_func func, Mtd mtd = Mtd()) : ObjCap() {
        ScopedCapSels pt;
        create(ec, pt.get(), reinterpret_cast<uintptr_t>(func), mtd);
        sel(pt.release());
    }

    /**
     * Calls this portal with given UtcbFrame. The state of the UtcbFrame is reset afterwards, so
     * that you can iterate over the typed and untyped item again from the beginning.
     * Note: although you can specify the UtcbFrame, you can't really choose it. That is, the kernel
     * will always use the top-most UtcbFrame of the Utcb that belongs to your Thread. The parameter
     * is rather passed symbolically to make clear that the UtcbFrame you're working with is
     * changed by the call.
     *
     * @param uf the UtcbFrame
     */
    void call(UtcbFrame &uf) {
        Syscalls::call(sel());
        uf._upos = 0;
        uf._tpos = 0;
    }

    /**
     * Sets the id of this portal which you'll receive as first parameter on every subsequent
     * portal-call.
     *
     * @param id the id
     */
    void set_id(word_t id) {
        Syscalls::pt_ctrl(sel(), id);
    }

private:
    void create(const Reference<LocalThread> &ec, capsel_t pt, uintptr_t func, Mtd mtd) {
        Syscalls::create_pt(pt, ec->sel(), func, mtd, Pd::current()->sel());
    }
};

}
