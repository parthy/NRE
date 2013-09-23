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

#include <kobj/GlobalThread.h>
#include <kobj/Sm.h>
#include <Test.h>
#include <Logging.h>
#include <CPU.h>
#include <cstdlib>

#include "ThreadRefs.h"

using namespace nre;
using namespace nre::test;

static void test_threadrefs();

const TestCase threadrefs = {
    "Reference counting of threads", test_threadrefs
};

static Sm sm(0);

static void mythread(void*) {
    sm.down();
}

static void test_threadrefs() {
    struct mallinfo minfo_before = dlmallinfo();

    {
        Reference<GlobalThread> gtcpy;
        {
            Reference<GlobalThread> gt = GlobalThread::create(mythread, CPU::current().log_id(), "mythread");
            WVPASSEQ(gt->refcount(), 2UL);
            gt->start();
            gtcpy = gt;
            WVPASSEQ(gt->refcount(), 3UL);
        }
        WVPASSEQ(gtcpy->refcount(), 2UL);

        sm.up();
        gtcpy->join();
        WVPASSEQ(gtcpy->refcount(), 1UL);
    }

    struct mallinfo minfo_after = dlmallinfo();
    WVPASSEQ(minfo_after.fordblks, minfo_before.fordblks);
    WVPASSEQ(minfo_after.uordblks, minfo_before.uordblks);
}
