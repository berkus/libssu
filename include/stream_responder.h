//
// Part of Metta OS. Check http://metta.exquance.com for latest version.
//
// Copyright 2007 - 2013, Stanislav Karchebnyy <berkus@exquance.com>
//
// Distributed under the Boost Software License, Version 1.0.
// (See file LICENSE_1_0.txt or a copy at http://www.boost.org/LICENSE_1_0.txt)
//
#pragma once

#include "host.h"
#include "key_responder.h"

namespace ssu {

namespace negotiation {

/**
 * @internal
 * Private helper class, to register with link layer to receive key exchange packets.
 * Only one instance ever created per host.
 */
class stream_responder : public key_responder, public stream_protocol
{
    stream_responder(shared_ptr<host>& host);
};

} // namespace negotiation

} // namespace ssu
