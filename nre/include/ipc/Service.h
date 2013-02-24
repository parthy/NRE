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

#include <kobj/LocalThread.h>
#include <kobj/GlobalThread.h>
#include <kobj/Pt.h>
#include <kobj/Sm.h>
#include <kobj/UserSm.h>
#include <ipc/ServiceCPUHandler.h>
#include <ipc/ServiceSession.h>
#include <mem/DataSpace.h>
#include <utcb/UtcbFrame.h>
#include <util/ScopedPtr.h>
#include <util/CPUSet.h>
#include <util/Math.h>
#include <bits/BitField.h>
#include <Exception.h>
#include <RCU.h>
#include <CPU.h>

namespace nre {

template<class T>
class SessionIterator;

/**
 * The exception that is used for services
 */
class ServiceException : public Exception {
public:
    explicit ServiceException(ErrorCode code = E_FAILURE, const String &msg = String()) throw()
        : Exception(code, msg) {
    }
};

/**
 * This class is used to provide a service for clients. If you create an instance of it, it is
 * registered at your parent with a specified name and services portals, that the client can call
 * to open and close sessions. Sessions are used to bind data to them. Note that sessions are
 * always used (also if no additional data is needed) to prevent a special case.
 * As soon as a client has a session, it can use the service that is provided. That is, it can call
 * the portals.
 */
class Service {
    friend class ServiceCPUHandler;
    template<class T>
    friend class SessionIterator;

public:
    static const uint MAX_SESSIONS_ORDER        =   6;
    static const size_t MAX_SESSIONS            =   1 << MAX_SESSIONS_ORDER;

    /**
     * The commands the parent provides for working with services
     */
    enum Command {
        REGISTER,
        OPEN_SESSION,
        CLOSE_SESSION,
        UNREGISTER,
    };

    /**
     * Constructor. Creates portals on the specified CPUs to accept client sessions and creates
     * Threads to handle <portal>. Note that you have to call reg() afterwards to finally register
     * the service.
     *
     * @param name the name of the service
     * @param cpus the CPUs on which you want to provide the service
     * @param portal the portal-function to provide
     */
    explicit Service(const char *name, const CPUSet &cpus, Pt::portal_func portal)
        : _regcaps(CapSelSpace::get().allocate(1 << CPU::order(), 1 << CPU::order())),
          _caps(CapSelSpace::get().allocate(MAX_SESSIONS << CPU::order(), MAX_SESSIONS << CPU::order())),
          _sm(), _cleanup_sm(0), _stop_sm(0), _stop(false), _cleaning(0), _name(name), _func(portal),
          _insts(new ServiceCPUHandler *[CPU::count()]), _reg_cpus(cpus.get()), _sessions() {
        for(size_t i = 0; i < CPU::count(); ++i) {
            if(_reg_cpus.is_set(i))
                _insts[i] = new ServiceCPUHandler(this, _regcaps + i, i);
            else
                _insts[i] = nullptr;
        }
    }
    /**
     * Destroys this service, i.e. destroys all sessions. You should have called unreg() before.
     */
    virtual ~Service() {
        unreg();
        for(size_t i = 0; i < MAX_SESSIONS; ++i) {
            ServiceSession *sess = rcu_dereference(_sessions[i]);
            if(sess)
                remove_session(sess);
        }
        for(size_t i = 0; i < CPU::count(); ++i)
            delete _insts[i];
        delete[] _insts;
        CapSelSpace::get().free(_caps, MAX_SESSIONS << CPU::order());
        CapSelSpace::get().free(_regcaps, 1 << CPU::order());
    }

    /**
     * Registers and starts service. This method blocks until stop() is called.
     */
    void start() {
        UtcbFrame uf;
        uf << REGISTER << String(_name) << _reg_cpus;
        // special case for root here because translate doesn't work inside one Pd.
        if(_startup_info.child)
            uf.delegate(CapRange(_regcaps, 1 << CPU::order(), Crd::OBJ_ALL));
        else
            uf << _regcaps;
        CPU::current().srv_pt().call(uf);
        uf.check_reply();
        _stop_sm.down();
    }

    /**
     * Stops the service, i.e. it unblocks the thread that called start().
     */
    void stop() {
        // don't unregister us here because we might be in a portal called by our parent and thus
        // we can't call our parent to unregister us
        _stop_sm.up();
    }

    /**
     * @return the service name
     */
    const char *name() const {
        return _name;
    }
    /**
     * @return the portal-function
     */
    Pt::portal_func portal() const {
        return _func;
    }
    /**
     * @return the capabilities used for all session-portals
     */
    capsel_t caps() const {
        return _caps;
    }
    /**
     * @return the bitmask that specified on which CPUs it is available
     */
    const BitField<Hip::MAX_CPUS> &available() const {
        return _reg_cpus;
    }

    /**
     * @return the iterator-beginning to walk over all sessions (note that you need to use an
     *  RCULock to prevent that sessions are destroyed while iterating over them)
     */
    template<class T>
    SessionIterator<T> sessions_begin();
    /**
     * @return the iterator-end
     */
    template<class T>
    SessionIterator<T> sessions_end();

    /**
     * @param pid the portal-selector
     * @return the session
     * @throws ServiceException if the session does not exist
     */
    template<class T>
    T *get_session(capsel_t pid) {
        return get_session_by_id<T>((pid - _caps) >> CPU::order());
    }
    /**
     * @param id the session-id
     * @return the session
     * @throws ServiceException if the session does not exist
     */
    template<class T>
    T *get_session_by_id(size_t id) {
        T *sess = static_cast<T*>(rcu_dereference(_sessions[id]));
        if(!sess)
            VTHROW(ServiceException, E_ARGS_INVALID, "Session " << id << " does not exist");
        return sess;
    }

    /**
     * @param cpu the cpu
     * @return the local thread for the given CPU to handle the provided portal
     */
    LocalThread *get_thread(cpu_t cpu) const {
        return _insts[cpu] != nullptr ? &_insts[cpu]->thread() : nullptr;
    }

protected:
    /**
     * Creates a new session in a free slot.
     *
     * @param args the arguments for the session
     * @return the created session
     * @throws ServiceException if there are no free slots anymore
     */
    ServiceSession *new_session(const String &args);

private:
    /**
     * May be overwritten to create an inherited class from ServiceSession.
     *
     * @param id the session-id
     * @param args the arguments for the session
     * @param pts the capabilities
     * @param func the portal-function
     * @return the session-object
     */
    virtual ServiceSession *create_session(size_t id, const String&, capsel_t pts, Pt::portal_func func) {
        return new ServiceSession(this, id, pts, func);
    }
    /**
     * Is called after a session has been created and put into the corresponding slot. May be
     * overwritten to perform some action.
     *
     * @param id the session-id
     */
    virtual void created_session(UNUSED size_t id) {
    }

    void unreg() {
        UtcbFrame uf;
        uf << UNREGISTER << String(_name);
        CPU::current().srv_pt().call(uf);
        uf.check_reply();
    }

    static void cleanup_thread(void*);
    void add_session(ServiceSession *sess);
    void remove_session(ServiceSession *sess);
    void do_remove_session(ServiceSession *sess);
    void destroy_session(capsel_t pid);

    Service(const Service&);
    Service& operator=(const Service&);

    capsel_t _regcaps;
    capsel_t _caps;
    UserSm _sm;
    Sm _cleanup_sm;
    Sm _stop_sm;
    bool _stop;
    word_t _cleaning;
    const char *_name;
    Pt::portal_func _func;
    ServiceCPUHandler **_insts;
    BitField<Hip::MAX_CPUS> _reg_cpus;
    ServiceSession *_sessions[MAX_SESSIONS];
};

/**
 * The iterator to walk forwards or backwards over all sessions. We need that, because we have to
 * skip unused slots. Note that the iterator assumes that no sessions are destroyed while being
 * used. Sessions may be added or removed in the meanwhile.
 */
template<class T>
class SessionIterator {
    friend class Service;

public:
    /**
     * Creates an iterator that starts at given position
     *
     * @param s the service
     * @param pos the start-position (index into session-array)
     */
    explicit SessionIterator(Service *s, ssize_t pos = 0) : _s(s), _pos(pos), _last(next()) {
    }

    T & operator*() const {
        return *_last;
    }
    T *operator->() const {
        return &operator*();
    }
    SessionIterator & operator++() {
        if(_pos < static_cast<ssize_t>(Service::MAX_SESSIONS) - 1) {
            _pos++;
            _last = next();
        }
        return *this;
    }
    SessionIterator operator++(int) {
        SessionIterator<T> tmp(*this);
        operator++();
        return tmp;
    }
    SessionIterator & operator--() {
        if(_pos > 0) {
            _pos--;
            _last = prev();
        }
        return *this;
    }
    SessionIterator operator--(int) {
        SessionIterator<T> tmp(*this);
        operator++();
        return tmp;
    }
    bool operator==(const SessionIterator<T>& rhs) const {
        return _pos == rhs._pos;
    }
    bool operator!=(const SessionIterator<T>& rhs) const {
        return _pos != rhs._pos;
    }

private:
    T *next() {
        while(_pos < static_cast<ssize_t>(Service::MAX_SESSIONS)) {
            T *t = static_cast<T*>(rcu_dereference(_s->_sessions[_pos]));
            if(t)
                return t;
            _pos++;
        }
        return nullptr;
    }
    T *prev() {
        while(_pos >= 0) {
            T *t = static_cast<T*>(rcu_dereference(_s->_sessions[_pos]));
            if(t)
                return t;
            _pos--;
        }
        return nullptr;
    }

    Service* _s;
    ssize_t _pos;
    T *_last;
};

template<class T>
SessionIterator<T> Service::sessions_begin() {
    return SessionIterator<T>(this);
}

template<class T>
SessionIterator<T> Service::sessions_end() {
    return SessionIterator<T>(this, MAX_SESSIONS);
}

}
