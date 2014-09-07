//
// Part of Metta OS. Check http://atta-metta.net for latest version.
//
// Copyright 2007 - 2014, Stanislav Karchebnyy <berkus@atta-metta.net>
//
// Distributed under the Boost Software License, Version 1.0.
// (See file LICENSE_1_0.txt or a copy at http://www.boost.org/LICENSE_1_0.txt)
//
#include "arsenal/logging.h"
#include "ssu/identity.h"
#include "ssu/host.h"
#include "ssu/negotiation/kex_responder.h"
#include "routing/routing_client.h"

using namespace std;
namespace ur = uia::routing;

namespace ssu {

//=================================================================================================
// stream_responder
//=================================================================================================

/**
 * Private helper class, registers with socket layer to receive key exchange packets.
 * Only one instance ever created per host.
 */
class stream_responder : public negotiation::kex_responder, public stream_protocol
{
    /** @name Routing protocol */
    /**@{*/
    /// Set of routing clients we've connected to so far.
    unordered_set<ur::client*> connected_clients_;
    void connect_routing_client(ur::client* client);
    // Handlers:
    void created_client(ur::client *rc);
    void client_ready();
    void lookup_notify(ssu::peer_id const& target_peer,
        uia::comm::endpoint const& peer_ep,
        uia::routing::client_profile const& peer_profile);
    /**@}*/

    /** @name Key exchange protocol */
    /**@{*/
    unique_ptr<channel> create_channel(uia::comm::socket_endpoint const& initiator_ep,
            byte_array const& initiator_eid,
            byte_array const& user_data_in, byte_array& user_data_out) override;
    /**@}*/

public:
    stream_responder(std::shared_ptr<host> host);
};

stream_responder::stream_responder(shared_ptr<host> host)
    : kex_responder(host, stream_protocol::magic_id)
{
    // Get us connected to all currently extant routing clients
    for (ur::client *c : host->coordinator->routing_clients()) {
        connect_routing_client(c);
    }

    // Watch for newly created routing clients
    host->coordinator->on_routing_client_created.connect([this](ur::client* c) {
        created_client(c);
    });
}

unique_ptr<channel> stream_responder::create_channel(uia::comm::socket_endpoint const& initiator_ep,
            byte_array const& initiator_eid, byte_array const&, byte_array&)
{
    internal::stream_peer* peer = get_host()->stream_peer(initiator_eid);

    unique_ptr<stream_channel> chan = make_unique<stream_channel>(get_host(), peer, initiator_eid);
    if (!chan->bind(initiator_ep))
    {
        logger::warning() << "Stream responder - could not bind new channel";
        return nullptr;
    }

    return chan;
}

void stream_responder::connect_routing_client(ur::client *c)
{
    logger::debug() << "Stream responder - connect routing client " << c->name();
    if (contains(connected_clients_, c)) {
        return;
    }

    connected_clients_.insert(c);
    c->on_ready.connect([this] { client_ready(); });
    c->on_lookup_notify.connect([this](ssu::peer_identity const& target_peer,
                                       uia::comm::endpoint const& peer_ep,
                                       uia::routing::client_profile const& peer_profile)
    {
        lookup_notify(target_peer, peer_ep, peer_profile);
    });
}

void stream_responder::created_client(ur::client *c)
{
    logger::debug() << "Stream responder - created client " << c->name();
    connect_routing_client(c);
}

void stream_responder::client_ready()
{
    logger::debug() << "Stream responder - routing client ready";

    // Retry all outstanding lookups in case they might succeed now.
    for (auto peer : get_host()->all_peers()) {
        peer->connect_channel();
    }
}

void stream_responder::lookup_notify(ssu::peer_identity const& target_peer,
    uia::comm::endpoint const& peer_ep,
    uia::routing::client_profile const& peer_profile)
{
    logger::debug() << "Stream responder - send r0 punch packet in response to lookup notify";
    // Someone at endpoint 'peer_ep' is apparently trying to reach us -
    // send them an R0 hole punching packet to their public endpoint.
    // @fixme perhaps make sure we might want to talk with them first?
    // e.g. check they're not in the blacklist.
    send_probe0(peer_ep);
}

//=================================================================================================
// Stream host state.
//=================================================================================================

void stream_host_state::instantiate_stream_responder()
{
    if (!responder_) {
        responder_ = make_shared<stream_responder>(get_host());
    }
    assert(responder_);
}

} // ssu namespace