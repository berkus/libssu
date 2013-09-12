//
// Part of Metta OS. Check http://metta.exquance.com for latest version.
//
// Copyright 2007 - 2013, Stanislav Karchebnyy <berkus@exquance.com>
//
// Distributed under the Boost Software License, Version 1.0.
// (See file LICENSE_1_0.txt or a copy at http://www.boost.org/LICENSE_1_0.txt)
//
#include "base_stream.h"
#include "logging.h"
#include "host.h"
#include "private/stream_peer.h"
#include "stream_channel.h"

namespace ssu {

base_stream::base_stream(std::shared_ptr<host> h, 
                         const peer_id& peer_id,
                         std::shared_ptr<base_stream> parent)
    : abstract_stream(h)
    , parent_(parent)
{
    logger::debug() << "Constructing internal stream for peer " << peer_id;
    peerid_ = peer_id;
    peer_ = h->stream_peer(peer_id);
}

base_stream::~base_stream()
{
    logger::debug() << "Destructing internal stream";
}

//txenqflow()
void base_stream::tx_enqueue_channel(bool tx_immediately)
{
    if (!is_attached())
        return attach_for_transmit();

    logger::debug() << "Internal stream enqueue on channel";

    // stream_channel* channel = tx_current_attachment->channel;
    // assert(channel && channel->is_active());

    // if (!tx_enqueued_channel)
    // {
    //     if (tx_queue.empty())
    //     {
    //         if (owner) {
    //             owner->ready_write();
    //         }
    //     }
    //     else
    //     {
    //         channel->enqueue_stream(this);
    //         tx_enqueued_channel = true;
    //     }
    // }

    // if (tx_immediately && channel->may_transmit())
    //     channel->ready_transmit();
}

bool base_stream::is_attached()
{
    return tx_current_attachment_ != nullptr;
}

void base_stream::tx_attach()
{
    logger::debug() << "Internal stream tx_attach";

    int slot = tx_current_attachment_ - tx_attachments_; // either 0 or 1
    // tx_current_attachment_->channel_;

    packet p(this, packet_type::attach);
    auto header = p.header<attach_header>();

    header->stream_id = tx_current_attachment_->stream_id_;
    header->type_subtype = type_bits(packet_type::attach)
                 | (init_ ? subtype_bits(flags::attach_init) : 0)
                 | (slot & subtype_bits(flags::attach_slot_mask));
    header->window = receive_window_byte();
}

//calcReceiveWindow
void base_stream::recalculate_receive_window()
{
    logger::debug() << "Internal stream recalculate receive window";
    receive_window_byte_ = 0x1a;
}

//calc
void base_stream::recalculate_transmit_window()
{
    logger::debug() << "Internal stream recalculate transmit window";
}

void base_stream::connect_to(std::string const& service, std::string const& protocol)
{
    logger::debug() << "Connecting internal stream to " << service << ":" << protocol;
    attach_for_transmit();
}

void base_stream::disconnect()
{
    logger::debug() << "Disconnecting internal stream";
    state_ = state::disconnected;
    // @todo bring down the connection
}

void base_stream::attach_for_transmit()
{
    assert(!peerid_.is_empty());

    // If we already have a transmit-attachment, nothing to do.
    if (tx_current_attachment_ != nullptr) {
        assert(tx_current_attachment_->is_in_use());
        return;
    }

    // If we're disconnected, we'll never need to attach again...
    if (state_ == state::disconnected)
        return;

    logger::debug() << "Internal stream attaching for transmission";

    // See if there's already an active channel for this peer.
    // If so, use it - otherwise, create new one.
    if (!peer_->primary_channel_) {
        // Get the channel setup process for this host ID underway.
        // XXX provide an initial packet to avoid an extra RTT!
        logger::debug() << "Waiting for channel";
        peer_->on_channel_connected.connect(std::bind(&base_stream::flow_connected, this));
        return peer_->connect_channel();
    }

    stream_channel* channel = peer_->primary_channel_;
    assert(channel->is_active());

    // If we're initiating a new stream and our peer hasn't acked it yet,
    // make sure we have a parent USID to refer to in creating the stream.
    if (init_ && parent_usid_.is_empty())
    {
        auto parent = parent_.lock();
        // No parent USID yet - try to get it from the parent stream.
        if (!parent)
        {
            // Top-level streams just use any channel's root stream.
            if (top_level_) {
                parent_ = channel->root_stream();
                parent = parent_.lock();
            } else {
                return fail("Parent stream closed before child stream could be initiated");
            }
        }
        parent_usid_ = parent->usid_;
        // Parent itself doesn't have an USID yet - we have to wait until it does.
        if (parent_usid_.is_empty())
        {
            logger::debug() << "Parent of " << this << " has no USID yet - waiting";
            parent->on_attached.connect(std::bind(&base_stream::parent_attached, this));
            return parent->attach_for_transmit();
        }
    }

    //-----------------------------------------
    // Allocate a stream_id_t for this stream.
    //-----------------------------------------

    // Scan forward through our SID space a little ways for a free SID;
    // if there are none, then pick a random one and detach it.
    counter_t sid = channel->allocate_transmit_sid();

    // Find a free attachment slot.
    int slot = 0;
    while (tx_attachments_[slot].is_in_use())
    {
        if (++slot == max_attachments) {
            logger::fatal() << "attach_for_transmit: all slots are in use.";
            // @fixme: Free up some slot.
        }
    }

    // Attach to the stream using the selected slot.
    tx_attachments_[slot].set_attaching(channel, sid);
    tx_current_attachment_ = &tx_attachments_[slot];

    // Fill in the new stream's USID, if it doesn't have one yet.
    if (usid_.is_empty()) {
        set_usid(unique_stream_id_t(sid, channel->tx_channel_id()));
        logger::debug() << "Creating stream " << usid_;
    }

    // Get us in line to transmit on the flow.
    // We at least need to transmit an attach message of some kind;
    // in the case of Init or Reply it might also include data.

    //assert(!channel->tx_streams_.contains(this));
    tx_enqueue_channel();
    if (channel->may_transmit())
        channel->on_ready_transmit();
}

void base_stream::set_usid(unique_stream_id_t new_usid)
{
    usid_ = new_usid;
}

size_t base_stream::bytes_available() const
{
    return 0;
}

bool base_stream::at_end() const
{
    return true;
}

ssize_t base_stream::read_data(char* data, size_t max_size)
{
    return 0;
}

int base_stream::pending_records() const
{
    return 0;
}

ssize_t base_stream::read_record(char* data, size_t max_size)
{
    return 0;
}

byte_array base_stream::read_record(size_t max_size)
{
    return byte_array();
}

ssize_t base_stream::write_data(const char* data, size_t size, uint8_t endflags)
{
    return 0;
}

ssize_t base_stream::read_datagram(char* data, size_t max_size)
{
    return 0;
}

ssize_t base_stream::write_datagram(const char* data, size_t size, stream::datagram_type is_reliable)
{
    return 0;
}

byte_array base_stream::read_datagram(size_t max_size)
{
    return byte_array();
}

abstract_stream* base_stream::open_substream()
{
    logger::debug() << "Internal stream open substream";
    return 0;
}

abstract_stream* base_stream::accept_substream()
{
    logger::debug() << "Internal stream accept substream";
    return 0;
}

bool base_stream::is_link_up() const
{
    return false;
}

void base_stream::shutdown(stream::shutdown_mode mode)
{
    logger::debug() << "Shutting down internal stream";
}

void base_stream::set_receive_buffer_size(size_t size)
{
    logger::debug() << "Setting internal stream receive buffer size " << size << " bytes";
}

void base_stream::set_child_receive_buffer_size(size_t size)
{
    logger::debug() << "Setting internal stream child receive buffer size " << size << " bytes";
}

void base_stream::fail(std::string const& error)
{
    disconnect();
    set_error(error);
}

void base_stream::dump()
{
    logger::debug() << "Internal stream " << this
                    << " state " << int(state_);
    // << " TSN " << tasn
    // << " RSN " << rsn
    // << " ravail " << ravail
    // << " rahead " << rahead.size()
    // << " rsegs " << rsegs.size()
    // << " rmsgavail " << rmsgavail
    // << " rmsgs " << rmsgsize.size()
}

} // ssu namespace
