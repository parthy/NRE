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

#include <kobj/Sm.h>
#include <mem/DataSpace.h>
#include <ipc/Consumer.h>
#include <ipc/Producer.h>
#include <ipc/PacketConsumer.h>
#include <ipc/PacketProducer.h>

#include "ProducerConsumer.h"

using namespace nre;
using namespace nre::test;

struct Item {
    explicit Item(int v) : value(v) {
    }

    int value;
    char dummy[60];
};

static void test_prodcons();
static void test_prodcons_simple();
static void test_prodcons_simple_specialcases();
static void test_prodcons_packet();
static void test_prodcons_packet_specialcases();

const TestCase prodcons = {
    "Producer-Consumer", test_prodcons
};

static void test_prodcons() {
    test_prodcons_simple();
    test_prodcons_simple_specialcases();
    test_prodcons_packet();
    test_prodcons_packet_specialcases();
}

static void test_prodcons_simple() {
    int i;
    DataSpace ds(ExecEnv::PAGE_SIZE, DataSpaceDesc::ANONYMOUS, DataSpaceDesc::RW);
    Sm sm(0);
    Producer<Item> prod(ds, sm, true);
    Consumer<Item> cons(ds, sm, false);

    WVPASS(!cons.has_data());
    i = 0;
    while(prod.produce(Item(i++)))
        ;
    WVPASS(cons.has_data());

    i = 0;
    for(; cons.has_data(); cons.next()) {
        Item *it = cons.get();
        WVPASSEQ(it->value, i++);
    }
    WVPASS(!cons.has_data());

    WVPASSEQ(i, static_cast<int>(cons.rblength() - 1));
}

static void test_prodcons_simple_specialcases() {
    int i;
    DataSpace ds(ExecEnv::PAGE_SIZE, DataSpaceDesc::ANONYMOUS, DataSpaceDesc::RW);
    Sm sm(0);
    Producer<Item> prod(ds, sm, true);
    Consumer<Item> cons(ds, sm, false);

    WVPASS(!cons.has_data());
    i = 0;
    while(prod.produce(Item(i++)))
        ;
    WVPASS(cons.has_data());

    for(i = 0; i < 32; ++i) {
        cons.get();
        cons.next();
        prod.produce(Item(i));
    }
}

static void test_prodcons_packet() {
    Item i(0);
    DataSpace ds(ExecEnv::PAGE_SIZE, DataSpaceDesc::ANONYMOUS, DataSpaceDesc::RW);
    Sm sm(0);
    PacketProducer prod(ds, sm, true);
    PacketConsumer cons(ds, sm, false);

    WVPASS(!cons.has_data());
    i.value = 0;
    while(prod.produce(&i, sizeof(Item)))
        i.value++;
    WVPASS(cons.has_data());

    i.value = 0;
    for(; cons.has_data(); cons.next()) {
        Item *ptr;
        size_t len = cons.get(ptr);
        WVPASSEQ(len, sizeof(Item));
        WVPASSEQ(ptr->value, i.value++);
    }
    WVPASS(!cons.has_data());
}

static void test_prodcons_packet_specialcases() {
    static char buffer[1024];
    int i;
    char *ptr;
    size_t len;

    DataSpace ds(ExecEnv::PAGE_SIZE, DataSpaceDesc::ANONYMOUS, DataSpaceDesc::RW);
    Sm sm(0);
    PacketProducer prod(ds, sm, true);
    PacketConsumer cons(ds, sm, false);

    // full
    WVPASS(!prod.produce(buffer, ExecEnv::PAGE_SIZE));
    WVPASS(!cons.has_data());

    // fill with different sized packets
    WVPASS(prod.produce(buffer, 512));
    WVPASS(prod.produce(buffer, 256));
    WVPASS(prod.produce(buffer, 1024));
    WVPASS(prod.produce(buffer, 1024));
    WVPASS(prod.produce(buffer, 1024));
    WVPASS(!prod.produce(buffer, 1024));
    WVPASS(cons.has_data());

    // read first
    len = cons.get(ptr);
    WVPASSEQ(len, static_cast<size_t>(512));
    cons.next();

    // fails because we only have 511 byte free at the beginning
    WVPASS(!prod.produce(buffer, 512));
    WVPASS(prod.produce(buffer, 128));

    // read all
    len = cons.get(ptr);
    WVPASSEQ(len, static_cast<size_t>(256));
    cons.next();
    len = cons.get(ptr);
    WVPASSEQ(len, static_cast<size_t>(1024));
    cons.next();
    len = cons.get(ptr);
    WVPASSEQ(len, static_cast<size_t>(1024));
    cons.next();
    len = cons.get(ptr);
    WVPASSEQ(len, static_cast<size_t>(1024));
    cons.next();
    len = cons.get(ptr);
    WVPASSEQ(len, static_cast<size_t>(128));
    cons.next();

    WVPASS(!cons.has_data());

    // write and read alternating
    for(i = 0; i < 32; ++i) {
        WVPASS(prod.produce(buffer, 512));
        len = cons.get(ptr);
        WVPASSEQ(len, static_cast<size_t>(512));
        cons.next();
    }
}
