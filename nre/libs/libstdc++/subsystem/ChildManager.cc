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

#include <subsystem/ChildManager.h>
#include <subsystem/ChildHip.h>
#include <stream/Serial.h>
#include <ipc/Service.h>
#include <kobj/Gsi.h>
#include <kobj/Sc.h>
#include <kobj/Ports.h>
#include <arch/Elf.h>
#include <util/Math.h>
#include <Logging.h>
#include <new>

namespace nre {

ChildManager::ChildManager()
    : _next_id(0), _child_count(0), _childs(), _deleter(this), _dsm(), _registry(), _sm(),
      _switchsm(), _slotsm(), _regsm(0), _diesm(0), _ecs(), _srvecs() {
    _ecs = new Reference<LocalThread>[CPU::count()];
    _srvecs = new Reference<LocalThread>[CPU::count()];
    for(auto it = CPU::begin(); it != CPU::end(); ++it) {
        _ecs[it->log_id()] = LocalThread::create(it->log_id());
        _ecs[it->log_id()]->set_tls(Thread::TLS_PARAM, this);

        _srvecs[it->log_id()] = LocalThread::create(it->log_id());
        _srvecs[it->log_id()]->set_tls(Thread::TLS_PARAM, this);

        UtcbFrameRef defuf(_ecs[it->log_id()]->utcb());
        defuf.accept_translates();
        defuf.accept_delegates(0);

        UtcbFrameRef srvuf(_srvecs[it->log_id()]->utcb());
        srvuf.accept_translates();
        srvuf.accept_delegates(Math::next_pow2_shift<size_t>(CPU::count()));
    }
}

ChildManager::~ChildManager() {
    {
        Reference<Child> child;
        while((child = get_first()).valid())
            destroy_child(&*child);
        _deleter.wait();
    }
    delete[] _ecs;
    delete[] _srvecs;
}

void ChildManager::prepare_stack(Child *c, uintptr_t &sp, uintptr_t csp) {
    /*
     * Initial stack:
     * +------------------+  <- top
     * |     arguments    |
     * |        ...       |
     * +------------------+
     * |         0        |
     * +------------------+
     * |   argv[argc-1]   |
     * +------------------+
     * |       ...        |
     * +------------------+
     * |     argv[0]      | <--\
     * +------------------+    |
     * |       argv       | ---/
     * +------------------+
     * |       argc       |
     * +------------------+
     */

    // first, simply copy the command-line to the stack
    const String &cmdline = c->cmdline();
    size_t len = Math::min<size_t>(MAX_CMDLINE_LEN, cmdline.length());
    char *bottom = reinterpret_cast<char*>(sp - Math::round_up<size_t>(len + 1, sizeof(word_t)));
    memcpy(bottom, cmdline.str(), len + 1);

    // count number of arguments
    size_t i = 0, argc = 0;
    char *str = bottom;
    while(*str) {
        if(*str == ' ' && i > 0)
            argc++;
        else if(*str != ' ')
            i++;
        str++;
    }
    if(i > 0)
        argc++;

    word_t *ptrs = reinterpret_cast<word_t*>(bottom - sizeof(word_t) * (argc + 1));
    // ensure that its aligned to 16-byte for SSE; this time not +8 because we'll call main
    ptrs = reinterpret_cast<word_t*>(reinterpret_cast<uintptr_t>(ptrs) & ~0xFUL);
    // store argv and argc
    *(ptrs - 1) = csp + (reinterpret_cast<word_t>(ptrs) & (ExecEnv::STACK_SIZE - 1));
    *(ptrs - 2) = argc;
    // store stackpointer for user
    sp = csp + (reinterpret_cast<uintptr_t>(ptrs - 2) & (ExecEnv::STACK_SIZE - 1));

    // now, walk through it, replace ' ' by '\0' and store pointers to the individual arguments
    str = bottom;
    i = 0;
    char *begin = bottom;
    while(*str) {
        if(*str == ' ' && i > 0) {
            *ptrs++ = csp + (reinterpret_cast<word_t>(begin) & (ExecEnv::STACK_SIZE - 1));
            *str = '\0';
            i = 0;
        }
        else if(*str != ' ') {
            if(i == 0)
                begin = str;
            i++;
        }
        str++;
    }
    if(i > 0)
        *ptrs++ = csp + (reinterpret_cast<word_t>(begin) & (ExecEnv::STACK_SIZE - 1));
    // terminate
    *ptrs++ = 0;
}

void ChildManager::build_hip(Child *c, const ChildConfig &config) {
    // create ds for cmdlines in Hip mem-items
    uintptr_t cmdlinesaddr = c->reglist().find_free(MAX_MODAUX_LEN);
    const DataSpace &auxds = _dsm.create(
        DataSpaceDesc(MAX_MODAUX_LEN, DataSpaceDesc::ANONYMOUS, DataSpaceDesc::RWX));
    char *cmdlines = reinterpret_cast<char*>(auxds.virt());
    char *cmdlinesend = cmdlines + MAX_MODAUX_LEN;
    c->reglist().add(auxds.desc(), cmdlinesaddr, ChildMemory::R | ChildMemory::OWN, auxds.unmapsel());

    // create ds for hip
    const DataSpace &ds = _dsm.create(
        DataSpaceDesc(ExecEnv::PAGE_SIZE, DataSpaceDesc::ANONYMOUS, DataSpaceDesc::RW));
    c->_hip = c->reglist().find_free(ExecEnv::PAGE_SIZE);
    ChildHip *hip = reinterpret_cast<ChildHip*>(ds.virt());

    // setup Hip
    new (hip)ChildHip(config.cpus());
    HipMem mem;
    for(size_t memidx = 0; config.get_module(memidx, mem); ++memidx) {
        uintptr_t auxaddr = 0;
        if(mem.aux) {
            size_t len = strlen(mem.cmdline()) + 1;
            if(cmdlines + len <= cmdlinesend) {
                memcpy(cmdlines, mem.cmdline(), len);
                auxaddr = cmdlinesaddr;
            }
            cmdlines += len;
            cmdlinesaddr += len;
        }
        hip->add_mem(mem.addr, mem.size, auxaddr, mem.type);
    }
    hip->finalize();

    // add to region list
    c->reglist().add(ds.desc(), c->_hip, ChildMemory::R | ChildMemory::OWN, ds.unmapsel());
}

Child::id_type ChildManager::load(uintptr_t addr, size_t size, const ChildConfig &config) {
    ElfEh *elf = reinterpret_cast<ElfEh*>(addr);

    // check ELF
    if(size < sizeof(ElfEh) || sizeof(ElfPh) > elf->e_phentsize ||
       size < elf->e_phoff + elf->e_phentsize * elf->e_phnum)
        throw ElfException(E_ELF_INVALID, "Size of ELF file invalid");
    if(!(elf->e_ident[0] == 0x7f && elf->e_ident[1] == 'E' &&
         elf->e_ident[2] == 'L' && elf->e_ident[3] == 'F'))
        throw ElfException(E_ELF_SIG, "No ELF signature");

    static struct {
        int no;
        PORTAL void (*portal)(Child*);
    } exc[] = {
        {CapSelSpace::EV_DIVIDE,        Portals::ex_de},
        {CapSelSpace::EV_DEBUG,         Portals::ex_db},
        {CapSelSpace::EV_BREAKPOINT,    Portals::ex_bp},
        {CapSelSpace::EV_OVERFLOW,      Portals::ex_of},
        {CapSelSpace::EV_BOUNDRANGE,    Portals::ex_br},
        {CapSelSpace::EV_UNDEFOP,       Portals::ex_ud},
        {CapSelSpace::EV_NOMATHPROC,    Portals::ex_nm},
        {CapSelSpace::EV_DBLFAULT,      Portals::ex_df},
        {CapSelSpace::EV_TSS,           Portals::ex_ts},
        {CapSelSpace::EV_INVSEG,        Portals::ex_np},
        {CapSelSpace::EV_STACK,         Portals::ex_ss},
        {CapSelSpace::EV_GENPROT,       Portals::ex_gp},
        {CapSelSpace::EV_PAGEFAULT,     Portals::ex_pf},
        {CapSelSpace::EV_MATHFAULT,     Portals::ex_mf},
        {CapSelSpace::EV_ALIGNCHK,      Portals::ex_ac},
        {CapSelSpace::EV_MACHCHK,       Portals::ex_mc},
        {CapSelSpace::EV_SIMD,          Portals::ex_xm},
    };

    // create child
    capsel_t pts = CapSelSpace::get().allocate(per_child_caps(), per_child_caps());
    Child *c = new Child(this, _next_id++, config.cmdline());
    try {
        // we have to create the portals first to be able to delegate them to the new Pd
        c->_ptcount = CPU::count() * (ARRAY_SIZE(exc) + Portals::COUNT - 1);
        c->_pts = new Pt *[c->_ptcount];
        memset(c->_pts, 0, c->_ptcount * sizeof(Pt*));
        for(cpu_t cpu = 0; cpu < CPU::count(); ++cpu) {
            size_t idx = cpu * (ARRAY_SIZE(exc) + Portals::COUNT - 1);
            size_t off = cpu * Hip::get().service_caps();
            size_t i = 0;
            for(; i < ARRAY_SIZE(exc); ++i) {
                c->_pts[idx + i] = new Pt(_ecs[cpu], pts + off + exc[i].no,
                                          reinterpret_cast<Pt::portal_func>(exc[i].portal),
                                          Mtd(Mtd::GPR_ACDB | Mtd::GPR_BSD | Mtd::RSP | Mtd::RFLAGS |
                                              Mtd::QUAL | Mtd::RIP_LEN));
            }
            c->_pts[idx + i++] = new Pt(_ecs[cpu], pts + off + CapSelSpace::EV_STARTUP,
                                        reinterpret_cast<Pt::portal_func>(Portals::startup),
                                        Mtd(Mtd::RSP));
            c->_pts[idx + i++] = new Pt(_ecs[cpu], pts + off + CapSelSpace::SRV_INIT,
                                        reinterpret_cast<Pt::portal_func>(Portals::init_caps),
                                        Mtd(0));
            c->_pts[idx + i++] = new Pt(_srvecs[cpu], pts + off + CapSelSpace::SRV_SERVICE,
                                        reinterpret_cast<Pt::portal_func>(Portals::service),
                                        Mtd(0));
            c->_pts[idx + i++] = new Pt(_ecs[cpu], pts + off + CapSelSpace::SRV_IO,
                                        reinterpret_cast<Pt::portal_func>(Portals::io),
                                        Mtd(0));
            c->_pts[idx + i++] = new Pt(_ecs[cpu], pts + off + CapSelSpace::SRV_SC,
                                        reinterpret_cast<Pt::portal_func>(Portals::sc),
                                        Mtd(0));
            c->_pts[idx + i++] = new Pt(_ecs[cpu], pts + off + CapSelSpace::SRV_GSI,
                                        reinterpret_cast<Pt::portal_func>(Portals::gsi),
                                        Mtd(0));
            c->_pts[idx + i++] = new Pt(_ecs[cpu], pts + off + CapSelSpace::SRV_DS,
                                        reinterpret_cast<Pt::portal_func>(Portals::dataspace),
                                        Mtd(0));
            while(i-- > 0)
                c->_pts[idx + i]->set_id(reinterpret_cast<word_t>(c));
        }
        // now create Pd and pass portals
        c->_pd = new Pd(Crd(pts, Math::next_pow2_shift(per_child_caps()), Crd::OBJ_ALL));
        c->_pd->set_name(config.cmdline().str());
        c->_entry = elf->e_entry;
        c->_main = config.entry();

        // check load segments and add them to regions
        for(size_t i = 0; i < elf->e_phnum; i++) {
            ElfPh *ph = reinterpret_cast<ElfPh*>(addr + elf->e_phoff + i * elf->e_phentsize);
            if(reinterpret_cast<uintptr_t>(ph) + sizeof(ElfPh) > addr + size)
                throw ElfException(E_ELF_INVALID, "Program header outside binary");
            if(ph->p_type != 1)
                continue;
            if(size < ph->p_offset + ph->p_filesz)
                throw ElfException(E_ELF_INVALID, "LOAD segment outside binary");

            uint perms = ChildMemory::OWN;
            if(ph->p_flags & PF_R)
                perms |= ChildMemory::R;
            if(ph->p_flags & PF_W)
                perms |= ChildMemory::W;
            if(ph->p_flags & PF_X)
                perms |= ChildMemory::X;

            size_t dssize = Math::round_up<size_t>(ph->p_memsz, ExecEnv::PAGE_SIZE);
            // TODO leak, if reglist().add throws
            const DataSpace &ds = _dsm.create(
                DataSpaceDesc(dssize, DataSpaceDesc::ANONYMOUS, DataSpaceDesc::RWX));
            // TODO actually it would be better to do that later
            memcpy(reinterpret_cast<void*>(ds.virt()),
                   reinterpret_cast<void*>(addr + ph->p_offset), ph->p_filesz);
            memset(reinterpret_cast<void*>(ds.virt() + ph->p_filesz), 0, ph->p_memsz - ph->p_filesz);
            c->reglist().add(ds.desc(), ph->p_vaddr, perms, ds.unmapsel());
        }

        // utcb
        c->_utcb = c->reglist().find_free(Utcb::SIZE);
        // just reserve the virtual memory with no permissions; it will not be requested
        c->reglist().add(DataSpaceDesc(Utcb::SIZE, DataSpaceDesc::VIRTUAL, 0), c->_utcb, 0);
        c->_ec = GlobalThread::create_for(c->_pd,
            reinterpret_cast<GlobalThread::startup_func>(elf->e_entry), config.cpu(),
            c->cmdline(), c->_utcb);

        // he needs a stack
        uint align = Math::next_pow2_shift(ExecEnv::STACK_SIZE);
        c->_stack = c->reglist().find_free(ExecEnv::STACK_SIZE, ExecEnv::STACK_SIZE);
        c->reglist().add(DataSpaceDesc(ExecEnv::STACK_SIZE, DataSpaceDesc::ANONYMOUS, 0, 0,
                                       c->_ec->stack(), align - ExecEnv::PAGE_SHIFT),
                         c->stack(), ChildMemory::RW | ChildMemory::OWN);

        // and a Hip
        build_hip(c, config);

        LOG(CHILD_CREATE, "Starting child '" << c->cmdline() << "'...\n");
        LOG(CHILD_CREATE, *c << "\n");

        // start child
        c->_ec->start();
    }
    catch(...) {
        delete c;
        throw;
    }

    {
        ScopedLock<UserSm> guard(&_sm);
        _childs.insert(c);
    }
    Atomic::add(&_child_count, +1);

    // wait until all services are registered
    if(config.waits() > 0) {
        size_t services_present;
        do {
            _regsm.down();
            services_present = 0;
            for(size_t i = 0; i < config.waits(); ++i) {
                if(_registry.find(config.wait(i)))
                    services_present++;
            }
        }
        while(services_present < config.waits());
    }
    return c->id();
}

void ChildManager::Portals::startup(Child *c) {
    UtcbExcFrameRef uf;
    try {
        if(c->_started) {
            uintptr_t stack = uf->rsp & ~(ExecEnv::PAGE_SIZE - 1);
            ChildMemory::DS *ds = c->reglist().find_by_addr(stack);
            if(!ds) {
                VTHROW(ChildMemoryException, E_NOT_FOUND,
                       "Dataspace not found for stack @ " << fmt(stack, "p"));
            }
            uf->rip = *reinterpret_cast<word_t*>(
                ds->origin(stack) + (uf->rsp & (ExecEnv::PAGE_SIZE - 1)) + sizeof(word_t));
            uf->mtd = Mtd::RIP_LEN;
        }
        else {
            uf->rip = *reinterpret_cast<word_t*>(uf->rsp + sizeof(word_t));
            prepare_stack(c, uf->rsp, c->stack());
            // the bit indicates that its not the root-task
            // TODO not nice
    #ifdef __i386__
            uf->rax = (1 << 31) | c->_ec->cpu();
    #else
            uf->rdi = (1 << 31) | c->_ec->cpu();
    #endif
            uf->rsi = c->_main;
            uf->rcx = c->hip();
            uf->rdx = c->utcb();
            uf->mtd = Mtd::RIP_LEN | Mtd::RSP | Mtd::GPR_ACDB | Mtd::GPR_BSD;
            c->_started = true;
        }
    }
    catch(...) {
        // let the kernel kill the Thread
        uf->rip = ExecEnv::KERNEL_START;
        uf->mtd = Mtd::RIP_LEN;
    }
}

void ChildManager::Portals::init_caps(Child *c) {
    UtcbFrameRef uf;
    try {
        // we can't give the child the cap for e.g. the Pd when creating the Pd. therefore the child
        // grabs them afterwards with this portal
        uf.finish_input();
        // don't allow them to create Sc's
        uf.delegate(c->_pd->sel(), 0, UtcbFrame::NONE, Crd::OBJ | Crd::PD_EC |
                    Crd::PD_PD | Crd::PD_PT | Crd::PD_SM);
        uf.delegate(c->_ec->sel(), 1);
        uf.delegate(c->_ec->sc()->sel(), 2);
        uf << E_SUCCESS;
    }
    catch(const Exception& e) {
        Syscalls::revoke(uf.delegation_window(), true);
        uf.clear();
        uf << e;
    }
}

void ChildManager::Portals::service(Child *c) {
    ChildManager *cm = Thread::current()->get_tls<ChildManager*>(Thread::TLS_PARAM);
    UtcbFrameRef uf;
    try {
        String name;
        Service::Command cmd;
        uf >> cmd >> name;
        switch(cmd) {
            case Service::REGISTER: {
                BitField<Hip::MAX_CPUS> available;
                capsel_t cap = uf.get_delegated(uf.delegation_window().order()).offset();
                uf >> available;
                uf.finish_input();

                LOG(SERVICES, "Child '" << c->cmdline() << "' regs " << name << "\n");
                capsel_t sm = cm->reg_service(c, cap, name, available);
                uf.accept_delegates();
                uf.delegate(sm);
                uf << E_SUCCESS;
            }
            break;

            case Service::UNREGISTER: {
                uf.finish_input();

                LOG(SERVICES, "Child '" << c->cmdline() << "' unregs " << name << "\n");
                cm->unreg_service(c, name);
                uf << E_SUCCESS;
            }
            break;

            case Service::OPEN_SESSION: {
                String args;
                uf >> args;
                uf.finish_input();

                LOG(SERVICES, "Child '" << c->cmdline() << "' opens session at "
                                        << name << " (" << args << ")\n");
                const ClientSession *sess = c->open_session(name, args, cm->registry().find(name));

                uf.delegate(CapRange(sess->caps(), 1 << CPU::order(), Crd::OBJ_ALL));
                uf << E_SUCCESS << sess->available();
            }
            break;

            case Service::CLOSE_SESSION: {
                capsel_t ident = uf.get_translated(0).offset();
                uf.finish_input();

                LOG(SERVICES, "Child '" << c->cmdline() << "' closes session at " << name << "\n");
                c->close_session(ident);
                uf << E_SUCCESS;
            }
            break;
        }
    }
    catch(const Exception& e) {
        Syscalls::revoke(uf.delegation_window(), true);
        uf.clear();
        uf << e;
    }
}

void ChildManager::Portals::gsi(Child *c) {
    UtcbFrameRef uf;
    try {
        uint gsi;
        void *pcicfg = nullptr;
        Gsi::Op op;
        uf >> op >> gsi;
        if(op == Gsi::ALLOC)
            uf >> pcicfg;
        uf.finish_input();

        capsel_t cap = 0;
        {
            ScopedLock<UserSm> guard(&c->_sm);
            if(op == Gsi::ALLOC) {
                LOG(RESOURCES, "Child '" << c->cmdline() << "' allocates GSI "
                                         << gsi << " (PCI " << pcicfg << ")\n");
            }
            else {
                LOG(RESOURCES, "Child '" << c->cmdline() << "' releases GSI " << gsi << "\n");
            }

            if(gsi >= Hip::MAX_GSIS)
                VTHROW(Exception, E_ARGS_INVALID, "Invalid GSI " << gsi);
            // make sure that just the owner can release it
            if(op == Gsi::RELEASE && !c->gsis().is_set(gsi))
                VTHROW(Exception, E_ARGS_INVALID, "Can't release GSI " << gsi << ". Not owner");
            if(c->_gsi_next == Hip::MAX_GSIS)
                throw Exception(E_CAPACITY, "No free GSI slots");

            {
                UtcbFrame puf;
                puf << op << gsi;
                if(op == Gsi::ALLOC) {
                    puf << pcicfg;
                    cap = c->_gsi_next++;
                    puf.delegation_window(Crd(c->_gsi_caps + cap, 0, Crd::OBJ_ALL));
                }
                CPU::current().gsi_pt().call(puf);
                puf.check_reply();
                if(op == Gsi::ALLOC)
                    puf >> gsi;
                c->gsis().set(gsi, op == Gsi::ALLOC);
            }
        }

        uf << E_SUCCESS;
        if(op == Gsi::ALLOC) {
            uf << gsi;
            uf.delegate(c->_gsi_caps + cap);
        }
    }
    catch(const Exception& e) {
        Syscalls::revoke(uf.delegation_window(), true);
        uf.clear();
        uf << e;
    }
}

void ChildManager::Portals::io(Child *c) {
    UtcbFrameRef uf;
    try {
        Ports::port_t base;
        uint count;
        Ports::Op op;
        uf >> op >> base >> count;
        uf.finish_input();

        {
            ScopedLock<UserSm> guard(&c->_sm);
            if(op == Ports::ALLOC) {
                LOG(RESOURCES, "Child '" << c->cmdline() << "' allocates ports "
                                         << fmt(base, "#x") << ".." << fmt(base + count - 1, "#x")
                                         << "\n");
            }
            else {
                LOG(RESOURCES, "Child '" << c->cmdline() << "' releases ports "
                                         << fmt(base, "#x") << ".." << fmt(base + count - 1, "#x")
                                         << "\n");
            }

            // alloc() makes sure that nobody can free something from other childs.
            if(op == Ports::RELEASE)
                c->io().remove(base, count);

            {
                UtcbFrame puf;
                if(op == Ports::ALLOC)
                    puf.delegation_window(Crd(0, 31, Crd::IO_ALL));
                puf << op << base << count;
                CPU::current().io_pt().call(puf);
                puf.check_reply();
            }

            if(op == Ports::ALLOC) {
                c->io().add(base, count);
                uf.delegate(CapRange(base, count, Crd::IO_ALL));
            }
        }
        uf << E_SUCCESS;
    }
    catch(const Exception& e) {
        Syscalls::revoke(uf.delegation_window(), true);
        uf.clear();
        uf << e;
    }
}

void ChildManager::Portals::sc(Child *c) {
    UtcbFrameRef uf;
    try {
        Sc::Command cmd;
        uf >> cmd;

        switch(cmd) {
            case Sc::ALLOC: {
                uintptr_t stackaddr = 0, utcbaddr = 0;
                bool stack, utcb;
                uf >> stack >> utcb;
                uf.finish_input();

                c->alloc_thread(stack ? &stackaddr : nullptr, utcb ? &utcbaddr : nullptr);
                uf << E_SUCCESS;
                if(stack)
                    uf << stackaddr;
                if(utcb)
                    uf << utcbaddr;
            }
            break;

            case Sc::CREATE: {
                void *ptr;
                String name;
                Qpd qpd;
                cpu_t cpu;
                capsel_t ec = uf.get_delegated(0).offset();
                uf >> name >> ptr >> cpu >> qpd;
                uf.finish_input();

                capsel_t sc = c->create_thread(ec, name, ptr, cpu, qpd);

                uf.accept_delegates();
                uf.delegate(sc);
                uf << E_SUCCESS << qpd;
            }
            break;

            case Sc::JOIN: {
                void *ptr;
                capsel_t sm = uf.get_delegated(0).offset();
                uf >> ptr;
                uf.finish_input();

                c->join_thread(ptr, sm);

                uf.accept_delegates();
                uf << E_SUCCESS;
            }
            break;

            case Sc::DESTROY: {
                capsel_t sc = uf.get_translated(0).offset();
                uf.finish_input();

                c->remove_thread(sc);

                uf << E_SUCCESS;
            }
            break;
        }
    }
    catch(const Exception& e) {
        Syscalls::revoke(uf.delegation_window(), true);
        uf.clear();
        uf << e;
    }
}

void ChildManager::map(UtcbFrameRef &uf, Child *c, DataSpace::RequestType type) {
    Crd crd(0);
    DataSpaceDesc desc;
    if(type == DataSpace::JOIN)
        crd = uf.get_translated(0);
    else
        uf >> desc;
    uf.finish_input();

    ScopedLock<UserSm> guard(&c->_sm);
    uintptr_t addr = 0;
    if(type != DataSpace::JOIN && desc.type() == DataSpaceDesc::VIRTUAL) {
        addr = c->reglist().find_free(desc.size());
        desc = DataSpaceDesc(desc.size(), desc.type(), desc.flags(), 0, 0, desc.align());
        c->reglist().add(desc, addr, desc.flags());
        desc.virt(addr);
        LOG(DATASPACES, "Child '" << c->cmdline() << "' allocated virtual ds:\n\t" << desc << "\n");
        uf << E_SUCCESS << desc;
    }
    else {
        // create it or attach to the existing dataspace
        const DataSpace &ds = type == DataSpace::JOIN ? _dsm.join(crd.offset()) : _dsm.create(desc);

        // add it to the regions of the child
        uint flags = ds.flags();
        try {
            // only create creations and non-device-memory
            if(type != DataSpace::JOIN && desc.phys() == 0)
                flags |= ChildMemory::OWN;
            // restrict permissions based on semaphore permission bits
            else if(type == DataSpace::JOIN) {
                if(!(crd.attr() & Crd::SM_UP))
                    flags &= ~ChildMemory::W;
                if(!(crd.attr() & Crd::SM_DN))
                    flags &= ~ChildMemory::X;
            }
            size_t align = 1 << (ds.desc().align() + ExecEnv::PAGE_SHIFT);
            addr = c->reglist().find_free(ds.size(), align);
            c->reglist().add(ds.desc(), addr, flags, ds.unmapsel());
        }
        catch(...) {
            _dsm.release(desc, ds.unmapsel());
            throw;
        }

        // build answer
        DataSpaceDesc childdesc(ds.size(), ds.type(), ds.flags() & flags, ds.phys(), addr,
                                ds.virt(), ds.desc().align());
        if(type == DataSpace::CREATE) {
            LOG(DATASPACES, "Child '" << c->cmdline() << "' created:\n\t"
                                      << "[sel=" << fmt(ds.sel(), "#x")
                                      << ", umsel=" << fmt(ds.unmapsel(), "#x") << "] "
                                      << childdesc << "\n");
            uf.delegate(ds.sel(), 0);
            uf.delegate(ds.unmapsel(), 1);
        }
        else {
            LOG(DATASPACES, "Child '" << c->cmdline() << "' joined:\n\t"
                    << "[sel=" << fmt(ds.sel(), "#x")
                    << ", umsel=" << fmt(ds.unmapsel(), "#x") << "] "
                    << childdesc << "\n");
            uf.accept_delegates();
            uf.delegate(ds.unmapsel());
        }
        uf << E_SUCCESS << childdesc;
    }
}

void ChildManager::switch_to(UtcbFrameRef &uf, Child *c) {
    capsel_t srcsel = uf.get_translated(0).offset();
    capsel_t dstsel = uf.get_translated(0).offset();
    uf.finish_input();

    {
        // note that we need another lock here since it may also involve childs of c (c may have
        // delegated it and if they cause a pagefault during this operation, we might get mixed
        // results)
        ScopedLock<UserSm> guard_switch(&_switchsm);

        uintptr_t srcorg, dstorg;
        {
            // first do the stuff for the child that requested the switch
            ScopedLock<UserSm> guard_regs(&c->_sm);
            ChildMemory::DS *src, *dst;
            src = c->reglist().find(srcsel);
            dst = c->reglist().find(dstsel);
            LOG(DATASPACES, "Child '" << c->cmdline() << "' switches:\n\t" << src->desc()
                                      << "\n\t" << dst->desc() << "\n");
            if(!src || !dst) {
                VTHROW(Exception, E_ARGS_INVALID,
                       "Unable to switch. DS " << srcsel << " or " << dstsel << " not found");
            }
            if(src->desc().size() != dst->desc().size()) {
                VTHROW(Exception, E_ARGS_INVALID,
                       "Unable to switch non-equal-sized dataspaces (" << src->desc().size() << ","
                                                                       << dst->desc().size() << ")");
            }

            // first revoke the memory to prevent further accesses
            CapRange(src->desc().origin() >> ExecEnv::PAGE_SHIFT,
                     src->desc().size() >> ExecEnv::PAGE_SHIFT, Crd::MEM_ALL).revoke(false);
            CapRange(dst->desc().origin() >> ExecEnv::PAGE_SHIFT,
                     dst->desc().size() >> ExecEnv::PAGE_SHIFT, Crd::MEM_ALL).revoke(false);
            // now copy the content
            memcpy(reinterpret_cast<char*>(dst->desc().origin()),
                   reinterpret_cast<char*>(src->desc().origin()),
                   src->desc().size());
            // now swap the two dataspaces
            srcorg = src->desc().origin();
            dstorg = dst->desc().origin();
            src->swap_backend(dst);
        }

        // now change the mapping for all other childs that have one of these dataspaces
        {
            ScopedLock<UserSm> guard_childs(&_sm);
            for(auto it = _childs.begin(); it != _childs.end(); ++it) {
                if(&*it == c)
                    continue;

                ScopedLock<UserSm> guard_regs(&it->_sm);
                DataSpaceDesc dummy;
                ChildMemory::DS *src, *dst;
                src = it->reglist().find(srcsel);
                dst = it->reglist().find(dstsel);
                if(!src && !dst)
                    continue;

                if(src)
                    src->switch_to(dstorg);
                if(dst)
                    dst->switch_to(srcorg);
            }
        }

        // now swap the origins also in the dataspace-manager (otherwise clients that join
        // afterwards will receive the wrong location)
        _dsm.swap(srcsel, dstsel);
    }

    uf << E_SUCCESS;
}

void ChildManager::unmap(UtcbFrameRef &uf, Child *c) {
    capsel_t sel = 0;
    DataSpaceDesc desc;
    uf >> desc;
    if(desc.type() != DataSpaceDesc::VIRTUAL)
        sel = uf.get_translated(0).offset();
    uf.finish_input();

    ScopedLock<UserSm> guard(&c->_sm);
    if(desc.type() == DataSpaceDesc::VIRTUAL) {
        LOG(DATASPACES, "Child '" << c->cmdline() << "' destroys virtual ds " << desc << "\n");
        c->reglist().remove_by_addr(desc.virt());
    }
    else {
        LOG(DATASPACES, "Child '" << c->cmdline() << "' destroys "
                                  << fmt(sel, "#x") << ": " << desc << "\n");
        // destroy (decrease refs) the ds
        _dsm.release(desc, sel);
        c->reglist().remove(sel);
    }
    uf << E_SUCCESS;
}

void ChildManager::Portals::dataspace(Child *c) {
    ChildManager *cm = Thread::current()->get_tls<ChildManager*>(Thread::TLS_PARAM);
    UtcbFrameRef uf;
    try {
        DataSpace::RequestType type;
        uf >> type;

        switch(type) {
            case DataSpace::CREATE:
            case DataSpace::JOIN:
                cm->map(uf, c, type);
                break;

            case DataSpace::SWITCH_TO:
                cm->switch_to(uf, c);
                break;

            case DataSpace::DESTROY:
                cm->unmap(uf, c);
                break;
        }
    }
    catch(const Exception& e) {
        Syscalls::revoke(uf.delegation_window(), true);
        uf.clear();
        uf << e;
    }
}

void ChildManager::Portals::ex_pf(Child *c) {
    ChildManager *cm = Thread::current()->get_tls<ChildManager*>(Thread::TLS_PARAM);
    UtcbExcFrameRef uf;
    cpu_t cpu = CPU::current().log_id();
    cpu_t pcpu = CPU::get(cpu).phys_id();

    bool kill = false;
    uintptr_t pfaddr = uf->qual[1];
    unsigned error = uf->qual[0];
    uintptr_t eip = uf->rip;

    // voluntary exit?
    if(pfaddr == eip && pfaddr >= ExecEnv::EXIT_START && pfaddr <= ExecEnv::THREAD_EXIT) {
        cm->term_child(c, 0, uf);
        return;
    }

    try {
        ScopedLock<UserSm> guard_switch(&cm->_switchsm);
        ScopedLock<UserSm> guard_regs(&c->_sm);

        LOG(PFS, "Child '" << c->cmdline() << "': Pagefault for " << fmt(pfaddr, "p")
                           << " @ " << fmt(eip, "p") << " on cpu " << pcpu << ", error="
                           << fmt(error, "#x") << "\n");

        // TODO different handlers (cow, ...)
        uintptr_t pfpage = pfaddr & ~(ExecEnv::PAGE_SIZE - 1);
        bool remap = false;
        ChildMemory::DS *ds = c->reglist().find_by_addr(pfaddr);
        uint perms = 0;
        uint flags = 0;
        kill = !ds || !ds->desc().flags();
        if(!kill) {
            flags = ds->page_perms(pfaddr);
            perms = ds->desc().flags() & ChildMemory::RWX;
        }
        // check if the access rights are violated
        if(flags) {
            if((error & 0x2) && !(perms & ChildMemory::W))
                kill = true;
            if((error & 0x4) && !(perms & ChildMemory::R))
                kill = true;
        }

        // is the page already mapped (may be ok if two cpus accessed the page at the same time)
        if(!kill && flags) {
            // first check if our parent has unmapped the memory
            Crd res = Syscalls::lookup(Crd(ds->origin(pfaddr) >> ExecEnv::PAGE_SHIFT, 0, Crd::MEM));
            // if so, remap it
            if(res.is_null()) {
                // reset all permissions since we want to remap it completely.
                // note that this assumes that we're revoking always complete dataspaces.
                ds->all_perms(0);
                remap = true;
            }
            else {
                LOG(PFS, "Child '" << c->cmdline() << "': Pagefault for " << fmt(pfaddr, "p")
                                   << " @ " << fmt(eip, "p") << " on cpu " << pcpu << ", error="
                                   << fmt(error, "#x") << " (page already mapped)\n");
                LOG(PFS_DETAIL, "See regionlist:\n" << c->reglist());
            }
        }

        if(!kill && (remap || !flags)) {
            // try to map the next few pages
            size_t pages = 32;
            if(ds->desc().flags() & DataSpaceDesc::BIGPAGES) {
                // try to map the whole pagetable at once
                pages = ExecEnv::PT_ENTRY_COUNT;
                // take care that we start at the beginning (note that this assumes that it is
                // properly aligned, which is made sure by root. otherwise we might leave the ds
                pfpage &= ~(ExecEnv::BIG_PAGE_SIZE - 1);
            }

            // build CapRange
            uintptr_t src = ds->origin(pfpage);
            CapRange cr(src >> ExecEnv::PAGE_SHIFT, pages, Crd::MEM | (perms << 2),
                        pfpage >> ExecEnv::PAGE_SHIFT);
            // ensure that it fits into the utcb
            cr.limit_to(uf.free_typed());
            cr.count(ds->page_perms(pfpage, cr.count(), perms));
            uf.delegate(cr);

            // ensure that we have the memory (if we're a subsystem this might not be true)
            // TODO this is not sufficient, in general
            // TODO perhaps we could find the dataspace, that belongs to this address and use this
            // one to notify the parent that he should map it?
            UNUSED volatile int x = *reinterpret_cast<int*>(src);
        }
    }
    catch(...) {
        kill = true;
    }

    // we can't release the lock after having killed the child. thus, we do out here (it's save
    // because there can't be any running Ecs anyway since we only destroy it when there are no
    // other Ecs left)
    if(kill) {
        try {
            LOG(CHILD_KILL, "Child '" << c->cmdline() << "': Unresolvable pagefault for "
                                      << fmt(pfaddr, "p") << " @ " << fmt(uf->rip, "p") << " on cpu "
                                      << pcpu << ", error=" << fmt(error, "#x") << "\n");
            cm->kill_child(c, CapSelSpace::EV_PAGEFAULT, uf, FAULT, 1);
        }
        catch(...) {
            // just let the kernel kill the Ec here
            uf->mtd = Mtd::RIP_LEN;
            uf->rip = ExecEnv::KERNEL_START;
        }
    }
}

void ChildManager::exception_kill(Child *c, int vector) {
    ChildManager *cm = Thread::current()->get_tls<ChildManager*>(Thread::TLS_PARAM);
    UtcbExcFrameRef uf;
    cm->kill_child(c, vector, uf, FAULT, 1);
}

void ChildManager::Portals::ex_de(Child *c) {
    ChildManager *cm = Thread::current()->get_tls<ChildManager*>(Thread::TLS_PARAM);
    cm->exception_kill(c, CapSelSpace::EV_DIVIDE);
}
void ChildManager::Portals::ex_db(Child *c) {
    ChildManager *cm = Thread::current()->get_tls<ChildManager*>(Thread::TLS_PARAM);
    cm->exception_kill(c, CapSelSpace::EV_DEBUG);
}
void ChildManager::Portals::ex_bp(Child *c) {
    ChildManager *cm = Thread::current()->get_tls<ChildManager*>(Thread::TLS_PARAM);
    cm->exception_kill(c, CapSelSpace::EV_BREAKPOINT);
}
void ChildManager::Portals::ex_of(Child *c) {
    ChildManager *cm = Thread::current()->get_tls<ChildManager*>(Thread::TLS_PARAM);
    cm->exception_kill(c, CapSelSpace::EV_OVERFLOW);
}
void ChildManager::Portals::ex_br(Child *c) {
    ChildManager *cm = Thread::current()->get_tls<ChildManager*>(Thread::TLS_PARAM);
    cm->exception_kill(c, CapSelSpace::EV_BOUNDRANGE);
}
void ChildManager::Portals::ex_ud(Child *c) {
    ChildManager *cm = Thread::current()->get_tls<ChildManager*>(Thread::TLS_PARAM);
    cm->exception_kill(c, CapSelSpace::EV_UNDEFOP);
}
void ChildManager::Portals::ex_nm(Child *c) {
    ChildManager *cm = Thread::current()->get_tls<ChildManager*>(Thread::TLS_PARAM);
    cm->exception_kill(c, CapSelSpace::EV_NOMATHPROC);
}
void ChildManager::Portals::ex_df(Child *c) {
    ChildManager *cm = Thread::current()->get_tls<ChildManager*>(Thread::TLS_PARAM);
    cm->exception_kill(c, CapSelSpace::EV_DBLFAULT);
}
void ChildManager::Portals::ex_ts(Child *c) {
    ChildManager *cm = Thread::current()->get_tls<ChildManager*>(Thread::TLS_PARAM);
    cm->exception_kill(c, CapSelSpace::EV_TSS);
}
void ChildManager::Portals::ex_np(Child *c) {
    ChildManager *cm = Thread::current()->get_tls<ChildManager*>(Thread::TLS_PARAM);
    cm->exception_kill(c, CapSelSpace::EV_INVSEG);
}
void ChildManager::Portals::ex_ss(Child *c) {
    ChildManager *cm = Thread::current()->get_tls<ChildManager*>(Thread::TLS_PARAM);
    cm->exception_kill(c, CapSelSpace::EV_STACK);
}
void ChildManager::Portals::ex_gp(Child *c) {
    ChildManager *cm = Thread::current()->get_tls<ChildManager*>(Thread::TLS_PARAM);
    cm->exception_kill(c, CapSelSpace::EV_GENPROT);
}
void ChildManager::Portals::ex_mf(Child *c) {
    ChildManager *cm = Thread::current()->get_tls<ChildManager*>(Thread::TLS_PARAM);
    cm->exception_kill(c, CapSelSpace::EV_MATHFAULT);
}
void ChildManager::Portals::ex_ac(Child *c) {
    ChildManager *cm = Thread::current()->get_tls<ChildManager*>(Thread::TLS_PARAM);
    cm->exception_kill(c, CapSelSpace::EV_ALIGNCHK);
}
void ChildManager::Portals::ex_mc(Child *c) {
    ChildManager *cm = Thread::current()->get_tls<ChildManager*>(Thread::TLS_PARAM);
    cm->exception_kill(c, CapSelSpace::EV_MACHCHK);
}
void ChildManager::Portals::ex_xm(Child *c) {
    ChildManager *cm = Thread::current()->get_tls<ChildManager*>(Thread::TLS_PARAM);
    cm->exception_kill(c, CapSelSpace::EV_SIMD);
}

void ChildManager::term_child(Child *c, int vector, UtcbExcFrameRef &uf) {
    try {
        int exitcode;
        bool pd = uf->eip != ExecEnv::THREAD_EXIT;
        // lol: using the condition operator instead of if-else leads to
        // "undefined reference to `nre::ExecEnv::THREAD_EXIT'"
        exitcode = uf->eip;
        if(pd)
            exitcode -= ExecEnv::EXIT_START;
        else
            exitcode -= ExecEnv::THREAD_EXIT;
        if(pd || exitcode != 0) {
            LOG(CHILD_KILL, "Child '" << c->cmdline() << "': " << (pd ? "Pd" : "Thread")
                                      << " terminated with exit code " << exitcode << " on cpu "
                                      << CPU::current().phys_id() << "\n");
        }

        kill_child(c, vector, uf, pd ? PROC_EXIT : THREAD_EXIT, exitcode);
    }
    catch(...) {
        // just let the kernel kill the Ec here
        uf->mtd = Mtd::RIP_LEN;
        uf->rip = ExecEnv::KERNEL_START;
    }
}

void ChildManager::kill_child(Child *c, int vector, UtcbExcFrameRef &uf, ExitType type, int exitcode) {
    bool dead = false;
    try {
        uintptr_t *addr, addrs[32];
        if(type == FAULT) {
            LOG(CHILD_KILL, "Child '" << c->cmdline() << "': caused exception "
                                      << vector << " @ " << fmt(uf->rip, "p") << " on cpu "
                                      << CPU::current().phys_id() << "\n");
            LOG(CHILD_KILL, "\tRegisters:\n");
            LOG(CHILD_KILL, "\trax=" << fmt(uf->rax, "#0x", 16)
                         << ", rbx=" << fmt(uf->rbx, "#0x", 16)
                         << ", rcx=" << fmt(uf->rcx, "#0x", 16) << "\n");
            LOG(CHILD_KILL, "\trdx=" << fmt(uf->rdx, "#0x", 16)
                         << ", rsi=" << fmt(uf->rsi, "#0x", 16)
                         << ", rdi=" << fmt(uf->rdi, "#0x", 16) << "\n");
            LOG(CHILD_KILL, "\trsp=" << fmt(uf->rsp, "#0x", 16)
                         << ", rbp=" << fmt(uf->rbp, "#0x", 16)
                         << ", rfl=" << fmt(uf->rfl, "#0x", 16) << "\n");
            LOG(CHILD_KILL, c->reglist());
            LOG(CHILD_KILL, "Unable to resolve fault; killing child\n");
        }
        // if its a thread exit, free stack and utcb
        else if(type == THREAD_EXIT)
            c->term_thread(reinterpret_cast<void*>(uf->rdx), uf->rsi, uf->rdi);

        if(exitcode != 0) {
            ExecEnv::collect_backtrace(c->_ec->stack(), uf->rbp, addrs, 32);
            LOG(CHILD_KILL, "Backtrace:\n");
            addr = addrs;
            while(*addr != 0) {
                LOG(CHILD_KILL, "\t" << fmt(*addr, "p") << "\n");
                addr++;
            }
        }
    }
    catch(const ChildMemoryException &e) {
        // this happens if removing the stack or utcb fails. we consider this as a protocol violation
        // of the child and thus kill it.
        LOG(CHILD_KILL, "Child thread violated exit protocol (" << e.msg() << "); killing it\n");
        type = FAULT;
    }
    catch(...) {
        // ignore the exception here. this may happen if the child is already gone
        dead = true;
    }

    // let the kernel kill the Thread by causing it a pagefault in kernel-area
    uf->mtd = Mtd::RIP_LEN;
    uf->rip = ExecEnv::KERNEL_START;
    if(!dead && type != THREAD_EXIT)
        destroy_child(c);
}

void ChildManager::destroy_child(Child *c) {
    // take care that we don't delete childs twice.
    bool del = false;
    {
        ScopedLock<UserSm> guard(&_sm);
        if(_childs.remove(c)) {
            del = true;
            _registry.remove(c);
        }
    }
    if(del)
        _deleter.del(c);
}

}
