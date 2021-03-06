//
// Part of Metta OS. Check http://atta-metta.net for latest version.
//
// Copyright 2007 - 2015, Stanislav Karchebnyy <berkus@atta-metta.net>
//
// Distributed under the Boost Software License, Version 1.0.
// (See file LICENSE_1_0.txt or a copy at http://www.boost.org/LICENSE_1_0.txt)
//
#include "arsenal/logging.h"
#include "arsenal/algorithm.h"
#include "sss/channels/channel.h"
#include "sss/internal/stream_peer.h"

using namespace std;

namespace sss {

/**
 * Maximum number of in-use SIDs to skip while trying to allocate one,
 * before we just give up and detach an existing one in this range.
 */
constexpr int max_sid_skip = 16;

// Stream ID 0 always refers to the root stream.
constexpr local_stream_id_t root_sid = 0;

stream_channel::stream_channel(shared_ptr<host> host,
                               internal::stream_peer* peer,
                               uia::peer_identity const& id)
    : channel(host, sodiumpp::secret_key(), sodiumpp::public_key("")) // @todo
    , peer_(peer)
    , root_(base_stream::create(host, id, nullptr))
{
    root_->state_ = base_stream::state::connected;

    // Pre-attach the root stream to this channel in both directions.
    root_->tx_attachments_[0].set_attaching(this, root_sid);
    root_->tx_attachments_[0].set_active(1);
    root_->tx_current_attachment_ = &root_->tx_attachments_[0];

    root_->rx_attachments_[0].set_active(this, root_sid, 1);

    // Listen on the root stream for top-level application streams
    root_->listen(stream::listen_mode::unlimited);

    // @todo Channel IDs

    on_ready_transmit.connect(boost::bind(&stream_channel::got_ready_transmit, this));
    on_link_status_changed.connect(boost::bind(&stream_channel::got_link_status_changed, this, _1));
}

stream_channel::~stream_channel()
{
    logger::debug() << "Stream channel - destructing";
    // on_link_status_changed
    // .disconnect(boost::bind(&stream_channel::got_link_status_changed, this, _1));
    stop();
    root_->state_ = base_stream::state::disconnected;
}

void
stream_channel::got_ready_transmit()
{
    if (sending_streams_.empty()) {
        return;
    }

    logger::debug() << "Stream channel - ready to transmit";

    // Round-robin between our streams for now.
    do {
        // Grab the next stream in line to transmit
        base_stream* stream = sending_streams_.front();
        sending_streams_.pop_front();

        // Allow it to transmit one packet.
        // It will add itself back onto sending_streams_ if it has more.
        stream->transmit_on(this);

    } while (not sending_streams_.empty() and may_transmit());
}

void
stream_channel::got_link_status_changed(uia::comm::socket::status new_status)
{
    logger::debug() << "Stream channel - link status changed, new status "
                    << uia::comm::socket::status_string(new_status);

    if (new_status != uia::comm::socket::status::down) {
        return;
    }

    // Link went down indefinitely - self-destruct.
    auto peer = target_peer();
    assert(peer);

    // If we were our target's primary channel, disconnect us.
    // if (peer->primary_channel_ == this)
    // {
    //     logger::debug() << "Primary channel to host ID " << peer->remote_host_id()
    //         << " on endpoint " << remote_endpoint()
    //         << " failed";
    //     peer->clear_primary_channel();
    // }

    // Stop and destroy this channel.
    delete this;
}

counter_t
stream_channel::allocate_transmit_sid()
{
    counter_t sid = transmit_sid_counter_;
    if (contains(transmit_sids_, sid)) {
        int maxsearch = 0x7ff0 - (transmit_sid_counter_ - transmit_sid_acked_);
        maxsearch     = min(maxsearch, max_sid_skip);
        do {
            if (maxsearch-- <= 0) {
                logger::fatal() << "allocate_transmit_sid: no free SIDs";
                // @fixme: do the actual detach
            }
        } while (contains(transmit_sids_, ++sid));
    }
    // Update our stream counter
    assert(sid >= transmit_sid_counter_);
    transmit_sid_counter_ = sid + 1;

    return sid;
}

void
stream_channel::start(bool initiate)
{
    logger::debug() << "Stream channel - start as " << (initiate ? "initiator" : "responder");
    super::start(initiate);
    assert(is_active());

    // Set the root stream's USID based on our channel ID
    root_->usid_.half_channel_id_ = initiate ? tx_channel_id() : rx_channel_id();
    root_->usid_.counter_ = 0;
    assert(!root_->usid_.is_empty());

    // If our target doesn't yet have an active channel, use this one.
    // This way either an incoming or outgoing channel can be a primary.
    target_peer()->channel_started(this);
}

void
stream_channel::stop()
{
    logger::debug() << "Stream channel - stop";
    super::stop();

    // XXX clean up sending_streams_, waiting_ack_ -- detach_all() cleans up waiting_ack_

    // Detach and notify all affected streams.
    auto tsids_copy = transmit_sids_;
    for (auto it : tsids_copy) {
        assert(it.second->channel_ == this);
        it.second->clear();
    }
    auto rsids_copy = receive_sids_;
    for (auto it : rsids_copy) {
        assert(it.second->channel_ == this);
        it.second->clear();
    }
}

//=================================================================================================
// Framing
//=================================================================================================
#if 0
/**
 * Assemble packet preamble - fixed prefix part and framing area start.
 */
size_t
stream_channel::assemble_preamble(boost::asio::mutable_buffer out_packet)
{
    // Fill outgoing packet information - fixed part.
    return 0;
}

size_t
stream_channel::assemble_item(int type, item t, boost::asio::mutable_buffer out_packet)
{
    return 0;
}

/**
 * Assemble full outgoing packet for sending.
 *
 * Framing layer is used here to pack actual frames into the packet and give estimations
 * of frame overhead.
 */
void
stream_channel::assemble_packet(asio::mutable_buffer out_packet)
{
    out_packet += assemble_preamble(out_packet);
    while (out_packet.size() > 0) {
        types_by_priority = [
            SETTINGS,
            ACK,
            RESET,
            PRIORITY,
            DECONGESTION,
            DETACH,
            CLOSE,
            STREAM_ATTACH,
            PADDING,
            EMPTY
        ];
        // Check remaining items based on their priorities.
        // Frame type priorities are increased every time the frame is skipped.
        for (type = 0; type < countof(types_by_priority); ++type) {
            if (not queue_by(type).empty()) {
                if (type == STREAM_ATTACH) {
                    // stream/attach queue has specific treatment - we want to ensure that ALL
                    // streams make progress.
                    // this means we take streams with highest priority and divide packet
                    // bandwidth between them. all other streams get their priority boosted.
                    auto items = queue(type).takeAll(priority == queue(type).front().priority);
                    auto remainingBandwidth = out_packet.size();
                    // @todo Juggling in case remaining bandwidth is too small
                    auto streamBandwidth = remainingBandwidth / items.size();
                    // loop over items and assemble them
                    for (auto&& item : items) {
                        out_packet += assemble_item(out_packet, type, item, streamBandwidth);
                    }
                    for (auto&& item : items) { // Remove those streams from queue
                        queue(type).pop_front();
                        // @fixme: Only remove if they have nothing else to send
                        // otherwise simply not increase their priority.
                    }
                    // Increase priority for the rest of streams
                    for (auto&& item : queue(type)) {
                        ++item.priority;
                    }
                } else {
                    for (auto& item : queue(type)) {
                        if (item.size() > out_packet.size()) { // won't fit
                            ++item.priority; // look at it sooner in the next iteration
                        }
                        // item fits!
                        out_packet += assemble_item(out_packet, type, item);
                        // @todo take it out of queue
                        item.used = true;
                    }
                }
                queue(type).sort(); // re-sort the queue by priority
            }                       // end if not empty
        }                           // end for
    }                               // end while
}
#endif
//=================================================================================================
// Stream interface
//=================================================================================================

void
stream_channel::enqueue_stream(base_stream* stream)
{
    logger::debug() << "Stream channel - enqueue stream " << stream;

    // Find the correct position at which to enqueue this stream,
    // based on priority.
    auto it = upper_bound(sending_streams_.begin(),
                          sending_streams_.end(),
                          stream->current_priority(),
                          [this](base_stream::priority_t prio, base_stream* str) {
                              return str->current_priority() >= prio;
                          });

    logger::debug() << "Stream channel - enqueue stream at pos " << (it - sending_streams_.begin())
                    << " of total " << sending_streams_.size() << " streams";

    sending_streams_.insert(it, stream);
}

void
stream_channel::dequeue_stream(base_stream* stream)
{
    logger::debug() << "Stream channel - dequeue stream " << stream;
    sending_streams_.erase(remove(sending_streams_.begin(), sending_streams_.end(), stream),
                           sending_streams_.end());
}

void
stream_channel::detach_all()
{
    // Save off and clear the channel's entire waiting_ack_ table -
    // it'll be more efficient to go through it once
    // and send all the waiting packets back to their streams,
    // than for each stream to pull out its packets individually.
    auto ack_copy = waiting_ack_;
    waiting_ack_.clear();

    // Detach all the streams with transmit-attachments to this flow.
    auto tsids_copy = transmit_sids_;
    for (auto v : tsids_copy) {
        v.second->clear();
    }
    assert(transmit_sids_.empty());

    // Finally, send back all the waiting packets to their streams.
    logger::debug() << "Returning " << ack_copy.size() << " channel packets for retransmission";
    for (auto v : ack_copy) {
        base_stream::tx_frame_t& p = v.second;
        assert(!p.is_null());
        if (!p.late) {
            p.late = true;
            p.owner->missed(this, p);
        } else {
            p.owner->expire(this, p);
        }
    }
}

bool
stream_channel::transmit_ack(byte_array& pkt, packet_seq_t ackseq, int ack_count)
{
    logger::debug() << "Stream channel - transmit ACK " << ackseq;

    // Pick a stream on which to send a window update -
    // for now, just the most recent stream on which we received a segment.
    stream_rx_attachment* attach{nullptr};
    if (contains(receive_sids_, ack_sid_)) {
        attach = receive_sids_[ack_sid_];
    } else {
        attach = receive_sids_[root_sid];
    }
    assert(attach);

    // Build a bare Ack packet header
    /*    auto header          = as_header<ack_header>(pkt);
        header->stream_id    = attach->stream_id_;
        header->type_subtype = type_and_subtype(packet_type::ack, 0);
        header->window       = attach->stream_->receive_window_byte();
    */
    // Let channel protocol put together its part of the packet and send it.
    return super::transmit_ack(pkt, ackseq, ack_count);
}

void
stream_channel::acknowledged(packet_seq_t txseq, int npackets, packet_seq_t rxackseq)
{
    logger::debug() << "Stream channel - ACKed seq " << txseq;
    for (; npackets > 0; txseq++, npackets--) {
        // find and remove the packet
        if (!contains(waiting_ack_, txseq))
            continue;

        base_stream::tx_frame_t p = waiting_ack_[txseq];
        waiting_ack_.erase(txseq);

        logger::debug() << "Stream channel - acknowledged packet " << txseq << " of size "
                        << p.payload_size();
        p.owner->acknowledged(this, p, rxackseq);
    }
}

void
stream_channel::missed(packet_seq_t txseq, int npackets)
{
    logger::debug() << "Stream channel - missed seq " << txseq;
    for (; npackets > 0; txseq++, npackets--) {
        // find but don't remove (common case for missed packets)
        if (!contains(waiting_ack_, txseq)) {
            logger::warning() << "Missed packet " << txseq << " but can't find it!";
            continue;
        }

        base_stream::tx_frame_t p = waiting_ack_[txseq];

        logger::debug() << "Stream channel - missed packet " << txseq << " of size "
                        << p.payload_size();

        if (!p.late) {
            p.late = true;
            if (!p.owner->missed(this, p)) {
                waiting_ack_.erase(txseq);
            }
        }
    }
}

void
stream_channel::expire(packet_seq_t txseq, int npackets)
{
    logger::debug() << "Stream channel - expire seq " << txseq;
    for (; npackets > 0; txseq++, npackets--) {
        // find and unconditionally remove packet when it expires
        base_stream::tx_frame_t p = waiting_ack_[txseq];
        waiting_ack_.erase(txseq);

        if (p.is_null()) {
            logger::debug() << "Expired packet " << txseq << " but can't find it!";
            continue;
        }

        logger::debug() << "Stream channel - expired packet " << txseq << " of size "
                        << p.payload_size();

        p.owner->expire(this, p);
    }
}

bool
stream_channel::channel_receive(boost::asio::mutable_buffer pkt, packet_seq_t packet_seq)
{
    logger::debug() << "Stream channel - receive seq " << packet_seq;
    return base_stream::receive(packet_seq, pkt, this);
}

} // sss namespace
