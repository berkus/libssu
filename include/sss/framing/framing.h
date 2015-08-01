#include "packet_frame.h"
#include <memory>
#include <boost/asio/buffers.hpp>

// clangcomplete compiled with custom llvm that's why
// /usr/local/opt/llvm/include/c++/v1/iosfwd:90:10: fatal error: 'wchar.h' file not found
// is happening'
// need to fix/reset default include paths somehow??

/**
 * framed packet
 * ⤷ IP header
 * ⤷ UDP header
 * ⤷ unencrypted packet header
 * ⤷ packet header
 * ⤷ collection of frames as (offset, size) pairs (const_buffers) into the packet buffer
*/
class framed_packet
{
    asio::mutable_buffer packet;              // whole packet
    asio::mutable_buffer unencrypted_header;  // packet subrange covering unencrypted header
    asio::mutable_buffer packet_header;       // packet subrange covering packet header
    std::vector<asio::mutable_buffer> frames; // packet subranges covering sequence of frames
};

// EMPTY frame - tag 0 byte, nothing contained - can be used to pad 1 or 2 byte gaps where PADDING
// frame doesn't fit

/**
 * Given multiple packets to send and a packet buffer, figure out most efficient packing,
 * complying to security policy etc. and write those packets into the buffer.
 * Written packets are cleared from the queue (and inserted into the wait queue for ack if needed).
 * Prepared buffer is forwarded to channel layer for encryption.
 *
 * Channel and stream layers submit data to send to framing, which then assembles packets
 * using priority rules.
 *
 * Framing owns client's data not yet prepared for sending.
 * Assembled packets are owned by channel transmission layer.
 * When channel is shut down these frames must be returned to owning stream.
 * (They should be stream-buffered until acked)
 */
class framing
{

    void enframe(asio::mutable_buffer output);
    void deframe(asio::const_buffer input);

    // References to streams and channel associated with this framing instance.
    // When parsing received frames, call into stream_[lsid]->rx_*() and channel_->rx_*() functions
    // to process associated frames.
    vector<base_stream*> streams_;
    channel* channel_;
};
