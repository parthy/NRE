/*
 * Copyright (C) 2012, Nils Asmussen <nils@os.inf.tu-dresden.de>
 * Copyright (C) 2008-2009, Bernhard Kauer <bk@vmmon.org>
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

#include <ipc/PacketConsumer.h>
#include <ipc/Producer.h>
#include <Assert.h>
#include <cstdlib>

namespace nre {

/**
 * Producer-part for the packet-based producer-consumer-communication over a dataspace. Packet-based
 * means that the items in the ringbuffer are of variable size.
 */
class PacketProducer : private Producer<size_t> {
public:
    /**
     * Creates a packet-producer that uses the given dataspace for communication
     *
     * @param ds the dataspace
     * @param sm the semaphore to use for signaling (has to be shared with the consumer of course)
     * @param init whether the producer should init the state. this should only be done by one
     *  party and preferably by the first one. That is, if the client is the producer it should
     *  init it (because it will create the dataspace and share it to the service).
     */
    explicit PacketProducer(DataSpace &ds, Sm &sm, bool init = true)
        : Producer<size_t>(ds, sm, init) {
        _max = (ds.size() - sizeof(PacketConsumer::Interface)) / sizeof(size_t);
    }

    /**
     * Puts <len> bytes at <buffer> as a packet into the ringbuffer.
     *
     * @param buffer the data to produce
     * @param len the length of the data
     * @return true if the item has been written successfully
     */
    bool produce(const void *buffer, size_t len) {
        assert(buffer && len);
        // determine whether there is enough space
        size_t right = _max - _if->wpos;
        size_t left = _if->rpos;
        size_t needed = (len + 2 * sizeof(size_t) - 1) / sizeof(size_t);
        if(left > _if->wpos) {
            right = left - _if->wpos;
            left = 0;
        }
        // take care that we leave at least 1 byte free.
        if((needed >= right) && (needed >= left))
            return false;

        // determine position
        size_t ofs = _if->wpos;
        if(right < needed) {
            // tell consumer that we put the item at the front
            if(right != 0)
                _if->buffer[ofs] = -1;
            ofs = 0;
        }
        // store length and data
        _if->buffer[ofs] = len;
        assert(ofs + needed <= _max);
        memcpy(_if->buffer + ofs + 1, buffer, len);

        // move write position forward
        if(ofs + needed == _max)
            _if->wpos = 0;
        else
            _if->wpos = ofs + needed;
        Sync::memory_barrier();
        // notify consumer
        try {
            _sm.up();
        }
        catch(...) {
            // if the client closed the session, we might get here. so, just ignore it.
        }
        return true;
    }
};

}
