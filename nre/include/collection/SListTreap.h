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

#include <collection/Treap.h>
#include <collection/SList.h>

namespace nre {

/**
 * A node in the slist-treap. You may create a subclass of this to add data to your nodes.
 */
template<typename KEY>
class SListTreapNode : public TreapNode<KEY> {
public:
    /**
     * Constructor
     *
     * @param key the key of the node
     */
    explicit SListTreapNode(typename TreapNode<KEY>::key_t key) : TreapNode<KEY>(key), _next(nullptr) {
    }

    SListTreapNode *next() {
        return _next;
    }
    void next(SListTreapNode *i) {
        _next = i;
    }

private:
    SListTreapNode *_next;
};

/**
 * A combination of a singly linked list and a treap, so that you can both iterate over all items
 * and find items by a key quickly. Note that the list does not maintain the order of the keys,
 * but has an arbitrary order.
 */
template<class T>
class SListTreap {
public:
    typedef typename SList<T>::iterator iterator;
    typedef typename SList<T>::const_iterator const_iterator;

    /**
     * Creates an empty slist-treap
     */
    explicit SListTreap() : _list(), _tree() {
    }

    /**
     * @return the number of items in the list
     */
    size_t length() const {
        return _list.length();
    }

    /**
     * @return beginning of list (you can change the list items)
     */
    iterator begin() {
        return _list.begin();
    }
    /**
     * @return end of list
     */
    iterator end() {
        return _list.end();
    }
    /**
     * @return tail of the list, i.e. the last valid item
     */
    iterator tail() {
        return _list.tail();
    }

    /**
     * @return beginning of list (you can NOT change the list items)
     */
    const_iterator cbegin() const {
        return _list.cbegin();
    }
    /**
     * @return end of list
     */
    const_iterator cend() const {
        return _list.cend();
    }
    /**
     * @return tail of the list, i.e. the last valid item (NOT changeable)
     */
    const_iterator ctail() const {
        return _list.ctail();
    }

    /**
     * Finds the node with given key in the tree
     *
     * @param key the key
     * @return the node or nullptr if not found
     */
    T *find(typename T::key_t key) const {
        return _tree.find(key);
    }

    /**
     * Inserts the given node in the tree. Note that it is expected, that the key of the node is
     * already set.
     *
     * @param node the node to insert
     */
    void insert(T *node) {
        _list.append(node);
        _tree.insert(node);
    }

    /**
     * Removes the given node from the tree.
     *
     * @param node the node to remove (doesn't have to be a valid pointer)
     * @return true if it has been removed
     */
    bool remove(T *node) {
        bool res = _list.remove(node);
        if(res)
            _tree.remove(node);
        return res;
    }

private:
    static bool is_less(const T &a, const T &b) {
        return a.key() < b.key();
    }

    SList<T> _list;
    Treap<T> _tree;
};

}
