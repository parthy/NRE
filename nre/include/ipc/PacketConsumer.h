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

#include <ipc/Consumer.h>

namespace nre {


/**
 * Consumer-part for the packet-based producer-consumer-communication over a dataspace. Packet-based
 * means that the items in the ringbuffer are of variable size.
 *
 * Usage-example:
 * PacketConsumer cons(&ds, &sm);
 * void *buffer;
 * for(size_t len; (len = cons->get(buffer)) != 0; cons.next()) {
 *   // do something with buffer
 * }
 */
class PacketConsumer : public Consumer<size_t> {
    friend class PacketProducer;

public:
    /**
     * Creates a packet-consumer that uses the given dataspace for communication
     *
     * @param ds the dataspace
     * @param sm the semaphore to use for signaling (has to be shared with the producer of course)
     * @param init whether the consumer should init the state. this should only be done by one
     *  party and preferably by the first one. That is, if the client is the consumer it should
     *  init it (because it will create the dataspace and share it to the service).
     */
    explicit PacketConsumer(DataSpace &ds, Sm &sm, bool init = false)
        : Consumer<size_t>(ds, sm, init) {
        _max = (ds.size() - sizeof(Interface)) / sizeof(size_t);
    }

    /**
     * Retrieves the item at current position. If there is no item anymore, it blocks until the
     * producer notifies it, that there is data available. You might interrupt that by using stop().
     * Note that the method will only return 0 if it has been stopped *and* there is no data anymore.
     *
     * Important: You have to call next() to move to the next item.
     *
     * @param buffer will be set to the packet-data
     * @return the length of the packet
     */
    template<typename T>
    size_t get(T *&buffer) {
        size_t *len = Consumer<size_t>::get();
        if(len == nullptr)
            return 0;
        if(*len == static_cast<size_t>(-1)) {
            _if->rpos = 0;
            len = _if->buffer + _if->rpos;
        }
        buffer = (T*)(_if->buffer + _if->rpos + 1);
        return *len;
    }

    /**
     * Tells the producer that you're done working with the current item (i.e. the producer will
     * never touch the item while you're working with it)
     */
    void next() {
        size_t len = (_if->buffer[_if->rpos] + 2 * sizeof(size_t) - 1) / sizeof(size_t);
        _if->rpos = (_if->rpos + len) % _max;
    }
};

}
