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

#include <collection/SListTreap.h>
#include <util/Profiler.h>
#include <util/Random.h>

#include "SListTreapTest.h"

#define TEST_NODE_COUNT     10
#define PERF_NODE_COUNT     5000

using namespace nre;
using namespace nre::test;

static void test_in_order();
static void test_rev_order();
static void test_rand_order();
static void test_perf();
static void test_add_and_rem(int *vals);
static void print_perf(const char *name, AvgProfiler &prof);

const TestCase slisttreaptest_inorder = {
    "SListTreap - add and remove nodes with increasing values", test_in_order,
};
const TestCase slisttreaptest_revorder = {
    "SListTreap - add and remove nodes with decreasing values", test_rev_order,
};
const TestCase slisttreaptest_randorder = {
    "SListTreap - add and remove regions with addresses in rand order", test_rand_order
};
const TestCase slisttreaptest_perf = {
    "SListTreap - performance", test_perf
};

struct MyNode : public SListTreapNode<int> {
    MyNode(int key, int _data) : SListTreapNode<int>(key), data(_data) {
    }
    int data;
};

static void test_in_order() {
    static int vals[TEST_NODE_COUNT];
    for(size_t i = 0; i < TEST_NODE_COUNT; i++)
        vals[i] = i;
    test_add_and_rem(vals);
}

static void test_rev_order() {
    static int vals[TEST_NODE_COUNT];
    for(size_t i = 0; i < TEST_NODE_COUNT; i++)
        vals[i] = TEST_NODE_COUNT - i;
    test_add_and_rem(vals);
}

static void test_rand_order() {
    static int vals[TEST_NODE_COUNT];
    for(size_t i = 0; i < TEST_NODE_COUNT; i++)
        vals[i] = i;
    Random::init(0x12345);
    for(size_t i = 0; i < 10000; i++) {
        size_t j = Random::get() % TEST_NODE_COUNT;
        size_t k = Random::get() % TEST_NODE_COUNT;
        uintptr_t t = vals[j];
        vals[j] = vals[k];
        vals[k] = t;
    }
    test_add_and_rem(vals);
}

static void test_perf() {
    SListTreap<MyNode> tree;
    MyNode **nodes = new MyNode *[PERF_NODE_COUNT];

    // create
    {
        AvgProfiler prof(PERF_NODE_COUNT);
        for(size_t i = 0; i < PERF_NODE_COUNT; i++) {
            nodes[i] = new MyNode(i, i);

            prof.start();
            tree.insert(nodes[i]);
            prof.stop();
        }
        print_perf("Node insertion:", prof);
    }

    // find all
    {
        AvgProfiler prof(PERF_NODE_COUNT);
        for(size_t i = 0; i < PERF_NODE_COUNT; i++) {
            prof.start();
            tree.find(i);
            prof.stop();
        }
        print_perf("Node searching:", prof);
    }

    // remove
    {
        AvgProfiler prof(PERF_NODE_COUNT);
        for(size_t i = 0; i < PERF_NODE_COUNT; i++) {
            prof.start();
            tree.remove(nodes[i]);
            prof.stop();
        }
        print_perf("Node removal:", prof);
    }

    for(size_t i = 0; i < PERF_NODE_COUNT; i++)
        delete nodes[i];
    delete[] nodes;
}

static void test_add_and_rem(int *vals) {
    static MyNode *nodes[TEST_NODE_COUNT];
    SListTreap<MyNode> tree;
    MyNode *node;

    // create
    for(size_t i = 0; i < TEST_NODE_COUNT; i++) {
        nodes[i] = new MyNode(vals[i], i);
        tree.insert(nodes[i]);
    }

    // find all
    for(size_t i = 0; i < TEST_NODE_COUNT; i++) {
        node = tree.find(vals[i]);
        WVPASSEQPTR(node, nodes[i]);
    }

    // iterate
    size_t c = 0;
    for(SListTreap<MyNode>::iterator it = tree.begin(); it != tree.end(); ++it)
        c++;
    WVPASSEQ(c, tree.length());

    // remove
    for(size_t i = 0; i < TEST_NODE_COUNT; i++) {
        tree.remove(nodes[i]);
        node = tree.find(vals[i]);
        WVPASSEQPTR(node, static_cast<MyNode*>(nullptr));
        delete nodes[i];

        for(size_t j = i + 1; j < TEST_NODE_COUNT; j++) {
            node = tree.find(vals[j]);
            WVPASSEQPTR(node, nodes[j]);
        }
    }
}

static void print_perf(const char *name, AvgProfiler &prof) {
    WVPRINT(name);
    WVPERF(prof.avg(), "cycles");
    WVPRINT("min: " << prof.min());
    WVPRINT("max: " << prof.max());
}
