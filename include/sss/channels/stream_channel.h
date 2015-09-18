//
// Part of Metta OS. Check http://atta-metta.net for latest version.
//
// Copyright 2007 - 2014, Stanislav Karchebnyy <berkus@atta-metta.net>
//
// Distributed under the Boost Software License, Version 1.0.
// (See file LICENSE_1_0.txt or a copy at http://www.boost.org/LICENSE_1_0.txt)
//
#pragma once

#include <queue>
#include <unordered_map>
#include "sss/channels/channel.h"
#include "sss/streams/base_stream.h"

namespace sss {

namespace internal {
class stream_peer;
}

/**
 * Channel implementation for SSS streams.
 */
class stream_channel : public channel, public stream_protocol
{
    friend class base_stream;          /// @fixme
    friend class stream_rx_attachment; /// @fixme
    friend class stream_tx_attachment; /// @fixme

    using super = channel;

    /**
     * Retry connection attempts for persistent streams once every minute.
     */
    static const async::timer::duration_type connect_retry_period;

    /**
     * Number of stall warnings we get from our primary stream
     * before we start a new lookup/key exchange phase to try replacing it.
     */
    static constexpr int stall_warnings_max = 3;

    int stall_warnings_{0}; ///< Stall warnings before new lookup.

    /**
     * Stream peer this channel is associated with.
     * A stream_channel is always a direct child of its stream_peer
     * so there should be no chance of this pointer ever dangling.
     */
    internal::stream_peer* peer_{nullptr};

    /**
     * Top-level stream used for connecting to services.
     */
    std::shared_ptr<base_stream> root_{nullptr};

    // Hash table of active streams indexed by stream ID
    std::unordered_map<local_stream_id_t, stream_tx_attachment*>
        transmit_sids_; // Our SID namespace
    std::unordered_map<local_stream_id_t, stream_rx_attachment*>
        receive_sids_; // Peer's SID namespace

    counter_t transmit_sid_counter_{1}; // Next stream counter to assign.
    counter_t transmit_sid_acked_{0};   // Last acknowledged stream counter.
    counter_t received_sid_counter_{0}; // Last stream counter received.

    /**
     * Closed stream IDs waiting for close acknowledgment.
     */
    std::unordered_set<local_stream_id_t> closed_streams_;

    /**
     * Streams queued for transmission on this channel.
     * This should be a priority queue for simplicity of enqueueing.
     * We use deque with the limitation that only push_back() and pop_front() are used.
     * Of course insertion into a deque is a bit more involved.
     * @fixme would prefer a smarter scheduling algorithm, e.g., stride.
     */
    std::deque<base_stream*> sending_streams_;

    /**
     * Packets transmitted and waiting for acknowledgment,
     * indexed by assigned transmit sequence number.
     */
    std::unordered_map<packet_seq_t, base_stream::tx_frame_t> waiting_ack_;

    /**
     * Packets already presumed lost ("missed")
     * but still waiting for potential acknowledgment until expiry.
     */
    std::unordered_map<packet_seq_t, base_stream::tx_frame_t> waiting_expiry_;

    /**
     * RxSID of stream on which we last received a packet -
     * this determines for which stream we send receive window info
     * when transmitting "bare" Ack packets.
     */
    local_stream_id_t ack_sid_;

    // Handlers.
    void got_link_status_changed(uia::comm::socket::status new_status);
    void got_ready_transmit();

public:
    stream_channel(std::shared_ptr<host> host,
                   internal::stream_peer* peer,
                   uia::peer_identity const& id);
    ~stream_channel();

    void start(bool initiate) override;
    void stop() override;

    inline internal::stream_peer* target_peer() { return peer_; }
    inline std::shared_ptr<base_stream> root_stream() { return root_; }

    counter_t allocate_transmit_sid();

    void enqueue_stream(base_stream* stream);
    void dequeue_stream(base_stream* stream);

    /**
     * Detach all streams currently transmit-attached to this channel,
     * and send any of their outstanding packets back for retransmission.
     */
    void detach_all();

    /**
     * Override channel's default transmit_ack() method
     * to include stream-layer info in explicit ack packets.
     */
    bool transmit_ack(byte_array& pkt, packet_seq_t ackseq, int ack_count) override;

    void acknowledged(packet_seq_t txseq, int npackets, packet_seq_t rxackseq) override;
    void missed(packet_seq_t txseq, int npackets) override;
    void expire(packet_seq_t txseq, int npackets) override;

    bool channel_receive(packet_seq_t pktseq, byte_array const& pkt) override;
};

} // sss namespace
