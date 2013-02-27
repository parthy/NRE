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

#include <kobj/Pt.h>
#include <kobj/LocalThread.h>
#include <ipc/Service.h>
#include <collection/SListTreap.h>
#include <subsystem/ServiceRegistry.h>
#include <subsystem/ChildConfig.h>
#include <subsystem/Child.h>
#include <mem/DataSpaceManager.h>
#include <util/Sync.h>
#include <Exception.h>

namespace nre {

/**
 * The exception used for errors during loading of the ELF file
 */
class ElfException : public Exception {
public:
    explicit ElfException(ErrorCode code = E_FAILURE, const String &msg = String()) throw()
        : Exception(code, msg) {
    }
};

/**
 * For all other exceptions regarding child handling
 */
class ChildException : public Exception {
public:
    explicit ChildException(ErrorCode code = E_FAILURE, const String &msg = String()) throw()
        : Exception(code, msg) {
    }
};

/**
 * This class is responsible for managing child tasks. That is, it provides portals for the child
 * tasks for various purposes (dataspaces, I/O ports, GSIs, services, ...). It will also handle
 * exceptions like pagefaults, division by zero and so on and react appropriately. It allows you
 * to load child tasks and get information about the running ones.
 */
class ChildManager {
    class Portals;
    friend class Portals;
    friend class Child;

    /**
     * Holds all portals for the child tasks
     */
    class Portals {
    public:
        static const size_t COUNT   = 9;

        PORTAL static void startup(Child *child);
        PORTAL static void init_caps(Child *child);
        PORTAL static void service(Child *child);
        PORTAL static void io(Child *child);
        PORTAL static void sc(Child *child);
        PORTAL static void gsi(Child *child);
        PORTAL static void dataspace(Child *child);
        PORTAL static void ex_de(Child *child);
        PORTAL static void ex_db(Child *child);
        PORTAL static void ex_bp(Child *child);
        PORTAL static void ex_of(Child *child);
        PORTAL static void ex_br(Child *child);
        PORTAL static void ex_ud(Child *child);
        PORTAL static void ex_nm(Child *child);
        PORTAL static void ex_df(Child *child);
        PORTAL static void ex_ts(Child *child);
        PORTAL static void ex_np(Child *child);
        PORTAL static void ex_ss(Child *child);
        PORTAL static void ex_gp(Child *child);
        PORTAL static void ex_pf(Child *child);
        PORTAL static void ex_mf(Child *child);
        PORTAL static void ex_ac(Child *child);
        PORTAL static void ex_mc(Child *child);
        PORTAL static void ex_xm(Child *child);
    };

    class ChildDeleter : public ThreadedDeleter<Child> {
    public:
        explicit ChildDeleter(ChildManager *cm)
            : ThreadedDeleter<Child>("child"), _cm(cm) {
        }

    private:
        virtual void call() {
            // call an empty portal with the child-Ecs
            UtcbFrame uf;
            Pt(_cm->_ecs[CPU::current().log_id()], cleanup_portal).call(uf);
            Pt(_cm->_srvecs[CPU::current().log_id()], cleanup_portal).call(uf);
        }

        virtual void invalidate(Child *obj) {
            obj->destroy();
        }
        virtual void destroy(Child *obj) {
            Child *o = nullptr;
            {
                ScopedLock<UserSm> guard(&_cm->_sm);
                if(obj->rem_ref())
                    o = obj;
            }
            // don't hold the lock during the delete (-> deadlock)
            if(o)
                delete o;
        }

        PORTAL static void cleanup_portal(void*) {
        }

        ChildManager *_cm;
    };

    /**
     * The different exit types
     */
    enum ExitType {
        THREAD_EXIT,
        PROC_EXIT,
        FAULT
    };

public:
    typedef typename SListTreap<Child>::const_iterator iterator;

    /**
     * Some settings
     */
    static const size_t MAX_CMDLINE_LEN     = 256;
    static const size_t MAX_MODAUX_LEN      = ExecEnv::PAGE_SIZE;

    /**
     * Creates a new child manager. It will already create all Ecs that are required
     */
    explicit ChildManager();
    /**
     * Deletes this child manager, i.e. it kills and deletes all childs and deletes all Ecs
     */
    ~ChildManager();

    /**
     * Loads a child task. That is, it treats <addr>...<addr>+<size> as an ELF file, creates a new
     * Pd, adds the correspondings segments to that Pd, creates a main thread and finally starts
     * the main thread. Afterwards, if the command line contains "provides=..." it waits until
     * the service with given name is registered.
     *
     * @param addr the address of the ELF file
     * @param size the size of the ELF file
     * @param config the config to use. this allows you to specify the access to the modules, the
     *  presented CPUs and other things
     * @return the id of the created child
     * @throws ELFException if the ELF is invalid
     * @throws Exception if something else failed
     */
    Child::id_type load(uintptr_t addr, size_t size, const ChildConfig &config);

    /**
     * @return the number of childs
     */
    size_t count() const {
        return _child_count;
    }
    /**
     * @return a semaphore that is up'ed as soon as a child has been killed
     */
    Sm &dead_sm() {
        return _diesm;
    }

    /**
     * The up-/down-implementation to allow ScopedLock<ChildManager>. This is required if you want
     * to iterate over all childs to prevent that the list is manipulated during that time.
     */
    void up() {
        _sm.up();
    }
    void down() {
        _sm.down();
    }

    /**
     * Returns a reference to the child with given id. As long as you hold the reference, the
     * object won't be destroyed.
     *
     * @param id the child id
     * @return the child with given id (reference might be invalid)
     */
    Reference<const Child> get(Child::id_type id) const {
        ScopedLock<UserSm> guard(&_sm);
        return Reference<const Child>(_childs.find(id));
    }

    /**
     * @return the iterator-beginning to walk over all sessions (note that you need to use an
     *  ScopedLock<ChildManager> to prevent that sessions are destroyed while iterating over them)
     */
    iterator begin() const {
        return _childs.cbegin();
    }
    /**
     * @return the iterator-end
     */
    iterator end() const {
        return _childs.cend();
    }

    /**
     * Kills the child with given id
     *
     * @param id the child id
     */
    void kill(Child::id_type id) {
        Reference<const Child> child = get(id);
        destroy_child(const_cast<Child*>(&*child));
    }

    /**
     * @return the service registry
     */
    const ServiceRegistry &registry() const {
        return _registry;
    }
    /**
     * Registers the given service. This is used to let the task that hosts the childmanager
     * register services as well (by default, only its child tasks do so).
     *
     * @param cap the portal capabilities
     * @param name the service name
     * @param available the CPUs it is available on
     * @return a semaphore cap that is used to notify the service about potentially destroyed sessions
     */
    capsel_t reg_service(capsel_t cap, const String& name, const BitField<Hip::MAX_CPUS> &available) {
        return reg_service(nullptr, cap, name, available);
    }
    /**
     * Unregisters the service with given name
     *
     * @param name the service name
     */
    void unreg_service(const String& name) {
        unreg_service(nullptr, name);
    }

private:
    static size_t per_child_caps() {
        return Math::next_pow2(Hip::get().service_caps() * CPU::count());
    }

    Reference<Child> get_first() {
        ScopedLock<UserSm> guard(&_sm);
        if(_childs.length() > 0)
            return Reference<Child>(&*_childs.begin());
        return Reference<Child>();
    }

    const ServiceRegistry::Service *get_service(const String &name) {
        ScopedLock<UserSm> guard(&_sm);
        const ServiceRegistry::Service* s = registry().find(name);
        if(!s && !_startup_info.child)
            VTHROW(ChildException, E_NOT_FOUND, "Unable to find service '" << name << "'");
        return s;
    }
    capsel_t reg_service(Child *c, capsel_t pts, const String& name,
                         const BitField<Hip::MAX_CPUS> &available) {
        ScopedLock<UserSm> guard(&_sm);
        const ServiceRegistry::Service *srv = _registry.reg(c, name, pts, 1 << CPU::order(), available);
        _regsm.up();
        return srv->sm().sel();
    }
    void unreg_service(Child *c, const String& name) {
        ScopedLock<UserSm> guard(&_sm);
        _registry.unreg(c, name);
    }

    void exception_kill(Child *c, int vector);
    void term_child(Child *c, int vector, UtcbExcFrameRef &uf);
    void kill_child(Child *c, int vector, UtcbExcFrameRef &uf, ExitType type, int exitcode);
    void destroy_child(Child *c);

    static void prepare_stack(Child *c, uintptr_t &sp, uintptr_t csp);
    void build_hip(Child *c, const ChildConfig &config);

    void map(UtcbFrameRef &uf, Child *c, DataSpace::RequestType type);
    void switch_to(UtcbFrameRef &uf, Child *c);
    void unmap(UtcbFrameRef &uf, Child *c);

    ChildManager(const ChildManager&);
    ChildManager& operator=(const ChildManager&);

    size_t _next_id;
    size_t _child_count;
    SListTreap<Child> _childs;
    ChildDeleter _deleter;
    DataSpaceManager<DataSpace> _dsm;
    ServiceRegistry _registry;
    mutable UserSm _sm;
    UserSm _switchsm;
    mutable UserSm _slotsm;
    Sm _regsm;
    Sm _diesm;
    LocalThread **_ecs;
    LocalThread **_srvecs;
};

}
