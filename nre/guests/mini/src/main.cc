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

#include "util.h"
#include "paging.h"
#include "idt.h"
#include "gdt.h"
#include "ports.h"
#include "pit.h"
#include "pic.h"
#include "keyb.h"
#include "stdout.h"

extern "C" int main();

static int counter = 0;

static void divbyzero() {
    Stdout::printf("Divide by zero\n");
}

static void gpf() {
    Stdout::printf("General protection fault\n");
}

static void timer() {
    if(counter++ % 100 == 0)
        Stdout::printf("Got timer irq %d\n", counter - 1);
    PIC::eoi(0x20);
}

static void keyboard() {
    Stdout::printf("Got keyboard irq: ");
    uint8_t sc;
    Video::set_color(Video::RED, Video::BLACK);
    while((sc = Keyb::read()))
        Stdout::printf("0x%x ", sc);
    Stdout::printf("\n");
    Video::set_color(Video::WHITE, Video::BLACK);
    PIC::eoi(0x21);
}

int main() {
    GDT::init();
    Paging::init();
    PIC::init();
    IDT::init();
    IDT::set(0x00, divbyzero);
    IDT::set(0x0D, gpf);
    IDT::set(0x20, timer);
    IDT::set(0x21, keyboard);
    PIT::init();
    Keyb::init();
    Stdout::init();
    Stdout::printf("\n");

    Paging::map(0x200000, 0x400000, Paging::PRESENT | Paging::WRITABLE);
    int *addr = reinterpret_cast<int*>(0x200000);
    *addr = 4;

    Util::enable_ints();
    while(1)
        ;
    return 0;
}
