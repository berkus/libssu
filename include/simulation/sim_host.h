//
// Part of Metta OS. Check http://metta.exquance.com for latest version.
//
// Copyright 2007 - 2013, Stanislav Karchebnyy <berkus@exquance.com>
//
// Distributed under the Boost Software License, Version 1.0.
// (See file LICENSE_1_0.txt or a copy at http://www.boost.org/LICENSE_1_0.txt)
//
#pragma once

#include <queue>
#include "host.h"
#include "link.h"

namespace ssu {
namespace simulation {

class simulator;
class sim_packet;
class sim_connection;
class sim_link;

class sim_host : public host
{
    std::shared_ptr<simulator> simulator_;

    /// Virtual network connections of this host.
    std::unordered_map<endpoint, std::shared_ptr<sim_connection>> connections_;

    /// Links bound on this host by port.
    typedef uint16_t port_t;
    std::unordered_map<port_t, std::shared_ptr<sim_link>> links_;

    /// Queue of packets to be delivered on this host.
    // If the packet drops itself from this queue it's likely to be deleted as
    // this queue owns them usually.
    std::queue<sim_packet*> packet_queue_;

public:
    std::shared_ptr<simulator> get_simulator() const { return simulator_; }

    sim_host(std::shared_ptr<simulator> sim);
    ~sim_host();

    boost::posix_time::ptime current_time() override;
    std::unique_ptr<async::timer_engine> create_timer_engine_for(async::timer* t) override;

    std::unique_ptr<link> create_link() override;

    /** Enqueue packet, assume ownership of the packet. */
    void enqueue_packet(sim_packet* packet);
    /** Dequeue the packet, dequeued packet will be deleted upon return. */
    void dequeue_packet(sim_packet* packet);
    /** Check if this packet is still on this host's receive queue. */
    bool packet_on_queue(sim_packet* packet) const;

    void register_connection_at(endpoint const& address, std::shared_ptr<sim_connection> conn);
    void unregister_connection_at(endpoint const& address, std::shared_ptr<sim_connection> conn);

    std::shared_ptr<sim_connection> connection_at(endpoint const& ep);
    std::shared_ptr<sim_host> neighbor_at(endpoint const& ep, endpoint& src);

    std::shared_ptr<sim_link> link_for(uint16_t port);
};

} // simulation namespace
} // ssu namespace
