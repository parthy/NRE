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

#include <ipc/Service.h>

namespace nre {

ServiceSession *Service::new_session(const String &args) {
    ScopedLock<UserSm> guard(&_sm);
    ServiceSession *sess = create_session(_next_id++, args, _func);
    _sessions.insert(sess);
    return sess;
}

void Service::remove_session(ServiceSession *sess) {
    // take care that we don't delete a session twice.
    bool del = false;
    {
        ScopedLock<UserSm> guard(&_sm);
        del = _sessions.remove(sess);
    }
    if(del)
        _deleter.del(sess);
}

}
