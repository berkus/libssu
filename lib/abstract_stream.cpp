//
// Part of Metta OS. Check http://atta-metta.net for latest version.
//
// Copyright 2007 - 2015, Stanislav Karchebnyy <berkus@atta-metta.net>
//
// Distributed under the Boost Software License, Version 1.0.
// (See file LICENSE_1_0.txt or a copy at http://www.boost.org/LICENSE_1_0.txt)
//
#include "sss/streams/abstract_stream.h"
#include "sss/host.h"
#include "sss/forward_ptrs.h"

namespace sss {

abstract_stream::abstract_stream(host_ptr h)
    : host_(h)
{
}

void
abstract_stream::set_priority(priority_t priority)
{
    priority_ = priority;
}

uia::peer_identity
abstract_stream::local_host_id() const
{
    return host_->host_identity().id();
}

uia::peer_identity
abstract_stream::remote_host_id() const
{
    return peer_id_;
}

} // sss namespace
