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

#include <kobj/GlobalThread.h>
#include <kobj/Sm.h>
#include <kobj/UserSm.h>
#include <collection/SList.h>
#include <stream/OStringStream.h>
#include <util/Atomic.h>
#include <util/ScopedLock.h>
#include <util/Sync.h>
#include <Logging.h>
#include <CPU.h>

namespace nre {

/**
 * Deletes objects after making sure that all CPUs have called a function. This is primarily
 * intended for deleting sessions and childs where portals have to be called to be sure that nobody
 * uses them anymore.
 *
 * It works like the following:
 * - we have a GlobalThread on each CPU, whereas CPU0 runs the "coordinator thread" and all others
 *   run a "helper thread".
 * - when calling del() the object is queued and the coordinator is waked up. At first, he
 *   invalidates the object (which should e.g. revoke the portals). Afterwards he notifies the other
 *   CPUs to call the function, does it as well and waits until they're finished.
 * - Finally, the coordinator deletes the object.
 */
template<class T>
class ThreadedDeleter {
public:
    /**
     * Creates a new threaded-deleter and uses <name> as prefix for the thread-names.
     *
     * @param name the prefix for the thread-names
     */
    explicit ThreadedDeleter(const char *name)
            : _sms(new Sm*[CPU::count()]), _tids(new ulong[CPU::count()]), _cpu_done(0), _done(0),
              _sm(), _objs(), _run(true) {
        OStringStream os;
        os << "cleanup-" << name;
        for(auto it = CPU::begin(); it != CPU::end(); ++it) {
            _sms[it->log_id()] = new Sm(0);
            GlobalThread *gt = GlobalThread::create(
                    it->log_id() == 0 ? cleanup_coordinator : cleanup_helper, it->log_id(), os.str());
            _tids[it->log_id()] = gt->id();
            gt->set_tls<ThreadedDeleter<T>*>(Thread::TLS_PARAM, this);
            gt->start();
        }
    }
    /**
     * Destructor
     */
    virtual ~ThreadedDeleter() {
        // wait until all threads are done
        _run = false;
        for(auto it = CPU::begin(); it != CPU::end(); ++it) {
            _sms[it->log_id()]->up();
            GlobalThread::join(_tids[it->log_id()]);
        }
        // now release the resources
        for(size_t i = 0; i < CPU::count(); ++i)
            delete _sms[i];
        delete[] _sms;
        delete[] _tids;
    }

    /**
     * Deletes the given object. It will wakeup the coordinator and force every CPU to do call().
     * Note that this method doesn't make sure that objects aren't deleted twice! So, the caller
     * is responsible for that.
     *
     * @param obj the object to delete
     */
    void del(T *obj) {
        {
            ScopedLock<UserSm> guard(&_sm);
            _objs.append(obj);
            LOG(THREADEDDEL, "del(" << obj << ")\n");
        }
        // notify the coordinator-thread
        // note that the one reason for doing it in another thread is that the childmanager can't
        // delete its childs in e.g. the pagefault-portal because the destructor destroys the
        // sessions, i.e. makes a call to the service. And the service might of cause trigger
        // pagefaults. So, to avoid deadlocks, we do it in a different thread.
        Sync::memory_fence();
        _sms[0]->up();
    }

    /**
     * Waits until all queued objects have been deleted. Note that you can't call wait()
     * from multiple threads concurrently!
     */
    void wait() {
        // we have to check whether it's destroyed here because _done is also up'ed if we haven't
        // waited for it.
        while(1) {
            _done.zero();
            ScopedLock<UserSm> guard(&_sm);
            if(_objs.length() == 0)
                break;
        }
    }

private:
    /**
     * Is called by all CPUs. Has to be overridden by the subclass.
     */
    virtual void call() = 0;
    /**
     * Invalidates the given object. This function is called before call() is called by every
     * CPU to e.g. make portals no longer callable by clients.
     *
     * @param obj the object
     */
    virtual void invalidate(T *obj) = 0;
    /**
     * Destroys the given object. By default, delete is used.
     *
     * @param obj the object
     */
    virtual void destroy(T *obj) {
        delete obj;
    }

    void remove(T *obj) {
        assert(CPU::current().log_id() == 0);
        LOG(THREADEDDEL, "Deleting " << obj << "\n");
        invalidate(obj);

        // let all helper threads do call()
        for(size_t i = 1; i < CPU::count(); ++i)
            _sms[i]->up();

        // we have to do that as well because the caller of del() might have been on e.g. CPU 1
        // this is safe because when doing del() in a portal, we don't wait anyway and if we wait
        // we don't do that in a portal.
        call();

        // wait for the others
        size_t n = CPU::count() - 1;
        while(n-- > 0)
            _cpu_done.down();

        // now it's safe to delete it
        {
            ScopedLock<UserSm> guard(&_sm);
            _objs.remove(obj);
        }
        destroy(obj);
        LOG(THREADEDDEL, "Deletion of " << obj << " completed\n");
    }

    static void cleanup_coordinator(void*) {
        ThreadedDeleter<T> *ct = Thread::current()->get_tls<ThreadedDeleter<T>*>(Thread::TLS_PARAM);
        Sm *sm = ct->_sms[CPU::current().log_id()];
        while(1) {
            sm->down();
            if(!ct->_run)
                break;

            while(1) {
                // get first object from list
                T *obj = nullptr;
                {
                    ScopedLock<UserSm> guard(&ct->_sm);
                    if(ct->_objs.length() > 0)
                        obj = &*ct->_objs.begin();
                }
                if(!obj)
                    break;

                // delete it
                ct->remove(obj);
                ct->_done.up();
            }
            LOG(THREADEDDEL, "No more objects to delete\n");
        }
    }

    static void cleanup_helper(void*) {
        ThreadedDeleter<T> *ct = Thread::current()->get_tls<ThreadedDeleter<T>*>(Thread::TLS_PARAM);
        Sm *sm = ct->_sms[CPU::current().log_id()];
        while(1) {
            sm->down();
            if(!ct->_run)
                break;
            ct->call();
            ct->_cpu_done.up();
        }
    }

    Sm **_sms;
    ulong *_tids;
    Sm _cpu_done;
    Sm _done;
    UserSm _sm;
    SList<T> _objs;
    volatile bool _run;
};

}
