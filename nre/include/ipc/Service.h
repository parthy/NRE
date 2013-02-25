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
#include <util/ThreadedDeleter.h>
#include <util/ScopedPtr.h>
#include <util/CPUSet.h>
#include <util/Math.h>
#include <bits/BitField.h>
#include <Exception.h>
#include <RCU.h>
#include <CPU.h>

namespace nre {

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

    class ServiceSessionDeleter : public ThreadedDeleter<ServiceSession> {
    public:
        explicit ServiceSessionDeleter(Service *s)
            : ThreadedDeleter<ServiceSession>("session"), _s(s) {
        }

    private:
        virtual void call() {
            // call an empty portal with the session-Ec
            UtcbFrame uf;
            Pt(_s->get_thread(CPU::current().log_id()), cleanup_portal).call(uf);
        }

        virtual void invalidate(ServiceSession *obj) {
            obj->destroy();
        }
        virtual void destroy(ServiceSession *obj) {
            ScopedLock<UserSm> guard(&_s->_sm);
            if(obj->rem_ref())
                delete obj;
        }

        PORTAL static void cleanup_portal(void*) {
        }

        Service *_s;
    };

public:
    typedef ServiceSession::portal_func portal_func;
    typedef typename SListTreap<ServiceSession>::iterator iterator;

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
    explicit Service(const char *name, const CPUSet &cpus, portal_func portal)
        : _next_id(0), _regcaps(CapSelSpace::get().allocate(1 << CPU::order(), 1 << CPU::order())),
          _sm(), _stop_sm(0), _stop(false), _name(name), _func(portal), _deleter(this),
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
        {
            Reference<ServiceSession> sess;
            while((sess = get_first()).valid())
                remove_session(&*sess, true);
        }
        for(size_t i = 0; i < CPU::count(); ++i)
            delete _insts[i];
        delete[] _insts;
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
    portal_func portal() const {
        return _func;
    }
    /**
     * @return the bitmask that specified on which CPUs it is available
     */
    const BitField<Hip::MAX_CPUS> &available() const {
        return _reg_cpus;
    }

    /**
     * The up-/down-implementation to allow ScopedLock<Service>. This is required if you want to
     * iterate over all sessions to prevent that the list is manipulated during that time.
     */
    void up() {
        _sm.up();
    }
    void down() {
        _sm.down();
    }

    /**
     * @return the iterator-beginning to walk over all sessions (note that you need to use an
     *  ScopedLock<Service> to prevent that sessions are destroyed while iterating over them)
     */
    iterator sessions_begin() {
        return _sessions.begin();
    }
    /**
     * @return the iterator-end
     */
    iterator sessions_end() {
        return _sessions.end();
    }

    /**
     * Returns a reference to the session with given id. As long as you hold the reference, the
     * session won't be destroyed.
     *
     * @param id the session-id
     * @return a reference to the session
     * @throws ServiceException if the session does not exist
     */
    template<class T>
    Reference<T> get_session(size_t id) {
        ScopedLock<UserSm> guard(&_sm);
        T *sess = static_cast<T*>(_sessions.find(id));
        if(!sess)
            VTHROW(ServiceException, E_ARGS_INVALID, "Session " << id << " doesn't exist");
        return Reference<T>(sess);
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
    Reference<ServiceSession> get_first() {
        ScopedLock<UserSm> guard(&_sm);
        if(_sessions.length() > 0)
            return Reference<ServiceSession>(&*_sessions.begin());
        return Reference<ServiceSession>();
    }
    Reference<ServiceSession> get_session_by_ident(capsel_t ident) {
        ScopedLock<UserSm> guard(&_sm);
        for(auto it = _sessions.begin(); it != _sessions.end(); ++it) {
            if(it->portal_caps() == ident)
                return Reference<ServiceSession>(&*it);
        }
        VTHROW(ServiceException, E_ARGS_INVALID, "Session with ident " << ident << " doesn't exist");
    }

    /**
     * May be overwritten to create an inherited class from ServiceSession.
     *
     * @param id the session-id
     * @param args the arguments for the session
     * @param func the portal-function
     * @return the session-object
     */
    virtual ServiceSession *create_session(size_t id, const String&, portal_func func) {
        return new ServiceSession(this, id, func);
    }

    void remove_session(ServiceSession *sess, bool wait = false);

    void unreg() {
        UtcbFrame uf;
        uf << UNREGISTER << String(_name);
        CPU::current().srv_pt().call(uf);
        uf.check_reply();
    }

    Service(const Service&);
    Service& operator=(const Service&);

    size_t _next_id;
    capsel_t _regcaps;
    UserSm _sm;
    Sm _stop_sm;
    bool _stop;
    const char *_name;
    portal_func _func;
    ServiceSessionDeleter _deleter;
    ServiceCPUHandler **_insts;
    BitField<Hip::MAX_CPUS> _reg_cpus;
    SListTreap<ServiceSession> _sessions;
};

}
