//
// Part of Metta OS. Check http://metta.exquance.com for latest version.
//
// Copyright 2007 - 2013, Stanislav Karchebnyy <berkus@exquance.com>
//
// Distributed under the Boost Software License, Version 1.0.
// (See file LICENSE_1_0.txt or a copy at http://www.boost.org/LICENSE_1_0.txt)
//
#include <deque>
#include "channel.h"
#include "logging.h"
#include "host.h"
#include "timer.h"
#include "make_unique.h"

using namespace std;
namespace bp = boost::posix_time;

namespace ssu {

//=================================================================================================
// channel private_data implementation
//=================================================================================================

struct transmit_event_t
{
    int32_t size_;   ///< Total size of packet including hdr
    bool    data_;   ///< Was an upper-layer data packet
    bool    pipe_;   ///< Currently counted toward transmit_data_pipe

    inline transmit_event_t(int32_t size, bool is_data)
        : size_(size)
        , data_(is_data)
        , pipe_(is_data)
    {
        logger::debug() << "New transmission event for " << size_ << (data_ ? " data bytes" : " control bytes");
    }
};

static constexpr unsigned CWND_MIN = 2;     // Min congestion window (packets/RTT)
static constexpr unsigned CWND_MAX = 1<<20; // Max congestion window (packets/RTT)

class channel::private_data
{
public:
    shared_ptr<host>          host_;

    //-------------------------------------------
    // Transmit state
    //-------------------------------------------

    /// Next sequence number to transmit.
    packet_seq_t tx_sequence_{1};
    /// Record of transmission events (XX data sizes).
    deque<transmit_event_t> tx_events_;
    /// Seqno of oldest recorded tx event.
    packet_seq_t tx_event_sequence_{0};
    /// Highest transmit sequence number ACK'd.
    packet_seq_t tx_ack_sequence_{0};
    // uint64_t recovseq;   ///< Sequence at which fast recovery finishes
    /// Transmit sequence number of "marked" packet.
    packet_seq_t mark_sequence_{1};
    /// Snapshot of tx_ack_sequence_ at time mark was placed.
    packet_seq_t mark_base_{0};
    /// Time at which marked packet was sent.
    bp::ptime mark_time_;
    uint32_t tx_ack_mask_;  ///< Mask of packets transmitted and ACK'd
    /// Data packets currently in flight.
    uint32_t tx_inflight_count_{0};
    /// Data bytes currently in flight.
    uint32_t tx_inflight_size_{0};
    /// Number of ACK'd packets since last mark.
    uint32_t mark_acks_{0};
    /// Number of ACKs expected after last mark.
    uint32_t mark_sent_{0};

    //-------------------------------------------
    // Congestion control
    // @todo Move this into CC strategy class.
    //-------------------------------------------
    // unique_ptr<cc_strategy> congestion_control;
    uint32_t cwnd{CWND_MIN};       ///< Current congestion window
    bool cwndlim{true};      ///< We were cwnd-limited this round-trip
    bool nocc_{false};

    // Retransmit state
    async::timer retransmit_timer_;  ///< Retransmit timer.

    //-------------------------------------------
    // Receive state
    //-------------------------------------------

    /// Highest sequence number received so far.
    packet_seq_t rx_sequence_{0};
    uint32_t rx_mask_{0};     ///< Mask of packets received so far

    // Receive-side ACK state
    /// Highest sequence number acknowledged so far.
    packet_seq_t rx_ack_sequence_{0};
    // //quint32 rxackmask;  // Mask of packets received & acknowledged
    /// Number of contiguous packets received before rx_ack_sequence_.
    int rx_ack_count_{0};
    /// Number of contiguous packets not yet ACKed.
    uint8_t rx_unacked_{0};
    // bool delayack;      ///< Enable delayed acknowledgments
    async::timer ack_timer_;  ///< Delayed ACK timer.
    unsigned miss_threshold_{3}; ///< Threshold at which to infer packets dropped
    // @todo make adaptive for robustness to reordering

    //-------------------------------------------
    // Channel statistics.
    //-------------------------------------------

    async::timer::duration_type cumulative_rtt_;

public:
    private_data(shared_ptr<host> host)
        : host_(host)
        , retransmit_timer_(host.get())
        , ack_timer_(host.get())
    {}

    ~private_data() {
        logger::debug() << "~channel::private_data";
    }

    void reset_congestion_control();

    inline async::timer::duration_type elapsed_since_mark() {
        return host_->current_time() - mark_time_;
    }
};

// @todo Move this to cc_strategy implementation.
void channel::private_data::reset_congestion_control()
{
    cumulative_rtt_ = bp::milliseconds(500);
}

//=================================================================================================
// channel
//=================================================================================================

constexpr int channel::header_len;
constexpr packet_seq_t channel::max_packet_sequence;

channel::channel(shared_ptr<host> host)
    : link_channel()
    , pimpl_(make_unique<private_data>(host))
{
    // Initialize transmit congestion control state
    pimpl_->tx_events_.push_back(transmit_event_t(0, false));
    assert(pimpl_->tx_events_.size() == 1);
    pimpl_->mark_time_ = host->current_time();

    pimpl_->reset_congestion_control();

    pimpl_->retransmit_timer_.on_timeout.connect(boost::bind(&channel::retransmit_timeout, this, _1));

    // Delayed ACK state
    pimpl_->ack_timer_.on_timeout.connect(boost::bind(&channel::ack_timeout, this));
}

channel::~channel()
{}

shared_ptr<host> channel::get_host()
{
    return pimpl_->host_;
}

void channel::start(bool initiate)
{
    logger::debug() << "channel: start " << (initiate ? "(initiator)" : "(responder)");

    assert(armor_);

    super::start(initiate);

    pimpl_->nocc_ = is_link_congestion_controlled();

    // We're ready to go!
    start_retransmit_timer();
    on_ready_transmit();

    set_link_status(link::status::up);
}

void channel::stop()
{
    logger::debug() << "channel: stop";
    pimpl_->retransmit_timer_.stop();
    pimpl_->ack_timer_.stop();
    // stats_timer_.stop();

    super::stop();

    set_link_status(link::status::down);
}

int channel::may_transmit()
{
    logger::debug() << "channel: may_transmit";
    if (pimpl_->nocc_)
        return super::may_transmit();

    if (pimpl_->cwnd > pimpl_->tx_inflight_count_) {
        int allowance = pimpl_->cwnd - pimpl_->tx_inflight_count_;
        logger::debug() << "channel: congestion window limits may_transmit to " << allowance;
        return allowance;
    }

    logger::debug() << "channel: congestion window limits may_transmit to 0";
    pimpl_->cwndlim = true;
    return 0;
}

uint32_t channel::make_first_header_word(channel_number channel, uint32_t tx_sequence)
{
    constexpr uint32_t seq_bits = 24;  
    constexpr uint32_t seq_mask = (1 << seq_bits) - 1;

    // 31-24: channel number
    // 23-0: tx sequence number
    return (tx_sequence & seq_mask) | ((uint32_t)channel << seq_bits);
}

uint32_t channel::make_second_header_word(uint8_t ack_count, uint32_t ack_sequence)
{
    constexpr uint32_t ack_cnt_bits = 4; 
    constexpr uint32_t ack_cnt_mask = (1 << ack_cnt_bits) - 1;
    constexpr uint32_t ack_seq_bits = 24;   
    constexpr uint32_t ack_seq_mask = (1 << ack_seq_bits) - 1;

    // 31-28: reserved field
    // 27-24: ack count
    // 23-0: ack sequence number
    return (ack_sequence & ack_seq_mask) | ((uint32_t)ack_count & ack_cnt_mask) << ack_seq_bits;
}

bool channel::channel_transmit(byte_array& packet, uint64_t& packet_seq)
{
    assert(packet.size() > header_len); // Must be non-empty data packet.

    // Include implicit acknowledgment of the latest packet(s) we've acked
    uint32_t ack_seq = make_second_header_word(pimpl_->rx_ack_count_, pimpl_->rx_ack_sequence_);
    if (pimpl_->rx_unacked_)
    {
        pimpl_->rx_unacked_ = 0;
        pimpl_->ack_timer_.stop();
    }

    // Send the packet
    bool success = transmit(packet, ack_seq, packet_seq, true);

    // If the retransmission timer is inactive, start it afresh.
    // (If this was a retransmission, retransmit_timeout() would have restarted it).
    if (!pimpl_->retransmit_timer_.is_active()) {
        start_retransmit_timer();
    }

    return success;
}

bool channel::transmit(byte_array& packet, uint32_t ack_seq, uint64_t& packet_seq, bool is_data)
{
    assert(is_active());

    logger::debug() << "Channel sending a packet";

    // Don't allow tx_sequence_ counter to wrap (@fixme re-key before it does!)
    packet_seq = pimpl_->tx_sequence_;
    assert(packet_seq < max_packet_sequence);
    uint32_t tx_seq = make_first_header_word(remote_channel(), packet_seq);

    // Fill in the transmit and ACK sequence number fields.
    assert(packet.size() >= header_len);
    big_uint32_t* pkt_header = reinterpret_cast<big_uint32_t*>(packet.data());
    pkt_header[0] = tx_seq;
    pkt_header[1] = ack_seq;

    // Encrypt and compute the MAC for the packet
    byte_array epkt = armor_->transmit_encode(pimpl_->tx_sequence_, packet);

    // Bump transmit sequence number,
    // and timestamp if this packet is marked for RTT measurement
    // This is the "Point of no return" -
    // a failure after this still consumes sequence number space.
    if (pimpl_->tx_sequence_ == pimpl_->mark_sequence_)
    {
        pimpl_->mark_time_ = pimpl_->host_->current_time();
        pimpl_->mark_acks_ = 0;
        pimpl_->mark_base_ = pimpl_->tx_ack_sequence_;
        pimpl_->mark_sent_ = pimpl_->tx_sequence_ - pimpl_->tx_ack_sequence_;
    }
    pimpl_->tx_sequence_ += 1;

    // Record the transmission event
    transmit_event_t evt(packet.size(), is_data);
    if (is_data)
    {
        pimpl_->tx_inflight_count_++;
        pimpl_->tx_inflight_size_ += evt.size_;
    }
    pimpl_->tx_events_.push_back(evt);
    assert(pimpl_->tx_event_sequence_ + pimpl_->tx_events_.size() == pimpl_->tx_sequence_);
    assert(pimpl_->tx_inflight_count_ <= (unsigned)pimpl_->tx_events_.size());

    logger::debug() << this << " channel.transmit tx seq " << pimpl_->tx_sequence_
        << " size " << epkt.size();

    // Ship it out
    return send(epkt);
}

void channel::start_retransmit_timer()
{
    async::timer::duration_type timeout =
        bp::milliseconds(pimpl_->cumulative_rtt_.total_milliseconds() * 2);
    pimpl_->retransmit_timer_.start(timeout); // Wait for full round-trip time.
}

void channel::retransmit_timeout(bool failed)
{
    logger::debug() << this << " Retransmit timeout" << (failed ? " - FAILED" : "")
        << ", interval " << pimpl_->retransmit_timer_.interval();

    // Restart the retransmission timer
    // with an exponentially increased backoff delay.
    pimpl_->retransmit_timer_.restart();

    // Assume that all in-flight data packets have been dropped,
    // and notify the upper layer as such.
    // Snapshot txseq first, because the missed() calls in the loop
    // might cause more packets to be transmitted.
    packet_seq_t seqlim = pimpl_->tx_sequence_;
    for (packet_seq_t seq = pimpl_->tx_event_sequence_; seq < seqlim; ++seq)
    {
        transmit_event_t& e = pimpl_->tx_events_[seq - pimpl_->tx_event_sequence_];
        if (e.pipe_)
        {
            e.pipe_ = false;
            pimpl_->tx_inflight_count_--;
            pimpl_->tx_inflight_size_ -= e.size_;
            missed(seq, 1);
            logger::debug() << this << "rtx timeout missed seq " << seq
                << ", in flight " << pimpl_->tx_inflight_count_;
        }
    }
    if (seqlim == pimpl_->tx_sequence_)
    {
        assert(pimpl_->tx_inflight_count_ == 0);
        assert(pimpl_->tx_inflight_size_ == 0);
    }

    // Force at least one new packet transmission regardless of cwnd.
    // This might not actually send a packet
    // if there's nothing on our transmit queue -
    // i.e., if no reliable sessions have outstanding data.
    // In that case, rtxtimer stays disarmed until the next transmit.
    on_ready_transmit();

    // If we exceed a threshold timeout, signal a failed connection.
    // The subclass has no obligation to do anything about this, however.
    set_link_status(failed ? link::status::down : link::status::stalled);
}


constexpr int max_ack_count = 0xf;

void channel::acknowledge(uint16_t pktseq, bool send_ack)
{
    constexpr int min_ack_packets = 2;
    constexpr int max_ack_packets = 4;

    logger::debug() << "channel: acknowledge " << pktseq << (send_ack ? " (sending)" : " (not sending)");

    // Update our receive state to account for this packet
    int32_t seq_diff = pktseq - pimpl_->rx_ack_sequence_;
    if (seq_diff == 1)
    {
        // Received packet is in-order and contiguous.
        // Roll rx_ack_sequence_ forward appropriately.
        pimpl_->rx_ack_sequence_ = pktseq;
        pimpl_->rx_ack_count_++;
        pimpl_->rx_ack_count_ = min(pimpl_->rx_ack_count_, max_ack_count);

        // ACK the received packet if appropriate.
        // Delay our ACK for up to min_ack_packets received non-ACK-only packets,
        // or up to max_ack_packets continuous ack-only packets.
        ++pimpl_->rx_unacked_;
        if (!send_ack and pimpl_->rx_unacked_ < max_ack_packets) {
            // Only ack acks occasionally,
            // and don't start the ack timer for them.
            return;
        }
        if (pimpl_->rx_unacked_ < max_ack_packets) {
            // Schedule an ack for transmission by starting the ack timer.
            // We normally do this even in for non-delayed acks,
            // so that we can process any other already-received packets first
            // and have a chance to combine multiple acks into one.
            if (pimpl_->rx_unacked_ < min_ack_packets) {
                // Data packet - start delayed ack timer.
                if (!pimpl_->ack_timer_.is_active())
                    pimpl_->ack_timer_.start(bp::milliseconds(10));
            } else {
                // Start with zero timeout - immediate callback from event loop.
                pimpl_->ack_timer_.start(bp::milliseconds(0));
            }
        } else {
            // But make sure we send an ack every max_ack_packets (4) no matter what...
            flush_ack();
        }
    }
    else if (seq_diff > 1)
    {
        // Received packet is in-order but discontiguous.
        // One or more packets probably were lost.
        // Flush any delayed ACK immediately, before updating our receive state.
        flush_ack();

        // Roll rx_ack_sequence_ forward appropriately.
        pimpl_->rx_ack_sequence_ = pktseq;

        // Reset the contiguous packet counter
        pimpl_->rx_ack_count_ = 0;    // (0 means 1 packet received)

        // ACK this discontiguous packet immediately
        // so that the sender is informed of lost packets ASAP.
        if (send_ack)
            tx_ack(pimpl_->rx_ack_sequence_, 0);
    }
    else if (seq_diff < 0)
    {
        // Old packet recieved out of order.
        // Flush any delayed ACK immediately.
        flush_ack();

        // ACK this out-of-order packet immediately.
        if (send_ack)
            tx_ack(pktseq, 0);
    }
}

inline bool channel::tx_ack(packet_seq_t ackseq, int ack_count)
{
    byte_array pkt;
    return transmit_ack(pkt, ackseq, ack_count);
}

inline void channel::flush_ack()
{
    if (pimpl_->rx_unacked_)
    {
        pimpl_->rx_unacked_ = 0;
        tx_ack(pimpl_->rx_ack_sequence_, pimpl_->rx_ack_count_);
    }
    pimpl_->ack_timer_.stop();
}

inline void channel::ack_timeout()
{
    flush_ack();
}

bool channel::transmit_ack(byte_array& packet, packet_seq_t ackseq, int ack_count)
{
    logger::debug() << "channel: transmit_ack seq " << ackseq << ", count " << ack_count+1;

    assert(ack_count <= max_ack_count);

    if (packet.size() < header_len)
        packet.resize(header_len);

    uint32_t ack_word = make_second_header_word(ack_count, ackseq);
    packet_seq_t pktseq;

    return transmit(packet, ack_word, pktseq, false);
}

void channel::acknowledged(uint64_t txseq, int npackets, uint64_t rxackseq)
{
    logger::debug() << this << " channel: tx seq "
        << txseq << "-" << txseq+npackets-1 << " acknowledged";
}

void channel::missed(uint64_t txseq, int npackets)
{
    logger::debug() << this << "channel: tx seq " << txseq << " missed";
}

void channel::expire(uint64_t txseq, int npackets)
{
    logger::debug() << this << "channel: tx seq " << txseq << " expired";
}

constexpr int maskBits = 32;

void channel::receive(byte_array const& pkt, link_endpoint const& src)
{
    if (!is_active()) {
        logger::warning() << this << " receive: inactive channel";
        return;
    }
    if (pkt.size() < header_len) {
        logger::warning() << this << " receive: runt packet";
        return;
    }

    // Determine the full 64-bit packet sequence number
    uint32_t tx_seq = pkt.as<big_uint32_t>()[0];

    channel_number pktchan = tx_seq >> 24;
    assert(pktchan == local_channel());    // Enforced by link

    int32_t seqdiff = ((int32_t)(tx_seq << 8)
                    - ((int32_t)pimpl_->rx_sequence_ << 8))
                    >> 8;

    packet_seq_t pktseq = pimpl_->rx_sequence_ + seqdiff;
    logger::debug() << "channel: receive - rxseq " << pktseq << ", size " << pkt.size();

    // Immediately drop too-old or already-received packets
    static_assert(sizeof(pimpl_->rx_mask_)*8 == maskBits, "Invalid rx_mask size");

    if (seqdiff > 0) {
        if (pktseq < pimpl_->rx_sequence_) {
            logger::warning() << "Channel receive: 64-bit wraparound detected!";
            return;
        }
    } else if (seqdiff <= -maskBits) {
        logger::debug() << "Channel receive: too-old packet dropped";
        return;
    } else if (seqdiff <= 0) {
        if (pimpl_->rx_mask_ & (1 << -seqdiff)) {
            logger::debug() << "Channel receive: duplicate packet dropped";
            return;
        }
    }

    byte_array msg = pkt;
    // Authenticate and decrypt the packet
    if (!armor_->receive_decode(pktseq, msg)) {
        logger::warning() << "Received packet auth failed on rx " << pktseq;
        return;
    }

    {
        // Log decoded packet.
        logger::file_dump decoded(msg);
    }

    // Record this packet as received for replay protection
    if (seqdiff > 0) {
        // Roll rxseq and rxmask forward appropriately.
        pimpl_->rx_sequence_ = pktseq;
        // @fixme This if is not necessary...
        if (seqdiff < maskBits)
            pimpl_->rx_mask_ = (pimpl_->rx_mask_ << seqdiff) | 1;
        else
            pimpl_->rx_mask_ = 1; // bit 0 = packet just received
    } else {
        // Set appropriate bit in rx_mask_
        assert(seqdiff < 0 and seqdiff > -maskBits);
        pimpl_->rx_mask_ |= (1 << -seqdiff);
    }

    // Decode the rest of the channel header
    // This word is encrypted so take it from already decrypted byte array
    uint32_t ack_seq = msg.as<big_uint32_t>()[1];

    // Update our transmit state with the ack info in this packet
    unsigned ackct = (ack_seq >> 24) & 0xf;

    int32_t ack_diff = ((int32_t)(ack_seq << 8)
                    - ((int32_t)pimpl_->tx_ack_sequence_ << 8))
                    >> 8;
    packet_seq_t ackseq = pimpl_->tx_ack_sequence_ + ack_diff;
    logger::debug() << "channel: receive - ack seq " << ackseq;

    if (ackseq >= pimpl_->tx_sequence_)
    {
        logger::warning() << "Channel receive: got ACK for packet seq " << ackseq
            << " not transmitted yet";
        return;
    }

    // Account for newly acknowledged packets
    unsigned new_packets = 0;

    if (ack_diff > 0)
    {
        // Received acknowledgment for one or more new packets.
        // Roll forward tx_ack_sequence_ and tx_ack_mask_.
        pimpl_->tx_ack_sequence_ = ackseq;
        if (ack_diff < maskBits)
            pimpl_->tx_ack_mask_ <<= ack_diff;
        else
            pimpl_->tx_ack_mask_ = 0;

        // Determine the number of newly-acknowledged packets
        // since the highest previously acknowledged sequence number.
        // (Out-of-order ACKs are handled separately below.)
        new_packets = min(unsigned(ack_diff), ackct+1);

        logger::debug() << this << " Advanced by " << ack_diff
            << ", ack count " << ackct
            << ", new packets " << new_packets
            << ", tx ack seq " << pimpl_->tx_ack_sequence_;

        // Record the new in-sequence packets in tx_ack_mask_ as received.
        // (But note: ackct+1 may also include out-of-sequence pkts.)
        pimpl_->tx_ack_mask_ |= (1 << new_packets) - 1;

        // Notify the upper layer of newly-acknowledged data packets
        for (packet_seq_t seq = pimpl_->tx_ack_sequence_ - new_packets + 1;
                seq <= pimpl_->tx_ack_sequence_;
                ++seq)
        {
            transmit_event_t& e = pimpl_->tx_events_[seq - pimpl_->tx_event_sequence_];
            if (e.pipe_)
            {
                e.pipe_ = false;
                pimpl_->tx_inflight_count_--;
                pimpl_->tx_inflight_size_ -= e.size_;

                acknowledged(seq, 1, pktseq);
            }
        }

        // Infer that packets left un-acknowledged sufficiently late
        // have been dropped, and notify the upper layer as such.
        // XX we could avoid some of this arithmetic if we just
        // made sequence numbers start a bit higher.
        packet_seq_t miss_lim = pimpl_->tx_ack_sequence_ - min(pimpl_->tx_ack_sequence_, (packet_seq_t)
                    max(pimpl_->miss_threshold_, new_packets));

        for (packet_seq_t miss_seq = pimpl_->tx_ack_sequence_ - min(pimpl_->tx_ack_sequence_, (packet_seq_t)
                    (pimpl_->miss_threshold_ + ack_diff - 1));
                miss_seq <= miss_lim;
                ++miss_seq)
        {
            transmit_event_t& e = pimpl_->tx_events_[miss_seq - pimpl_->tx_event_sequence_];
            if (e.pipe_)
            {
                logger::debug() << this << " seq " << pimpl_->tx_event_sequence_ << " inferred dropped";
                e.pipe_ = false;
                pimpl_->tx_inflight_count_--;
                pimpl_->tx_inflight_size_ -= e.size_;
                // ccMissed(miss_seq);
                missed(miss_seq, 1);
                logger::debug() << this << " infer-missed seq " << miss_seq << " tx inflight " << pimpl_->tx_inflight_count_;
            }
        }

        // Finally, notice packets as they exit our ack window,
        // and garbage collect their transmit records,
        // since they can never be acknowledged after that.
        if (pimpl_->tx_ack_sequence_ > maskBits)
        {
            while (pimpl_->tx_event_sequence_ <= pimpl_->tx_ack_sequence_ - maskBits)
            {
                logger::debug() << this << " seq " << pimpl_->tx_event_sequence_ << " expired";
                assert(!pimpl_->tx_events_.front().pipe_);
                pimpl_->tx_events_.pop_front();
                pimpl_->tx_event_sequence_++;
                expire(pimpl_->tx_event_sequence_ - 1, 1);
            }
        }
        //---------------------------------------------------------------------

        // Reset the retransmission timer, since we've made progress.
        // Only re-arm it if there's still outstanding unACKed data.
        set_link_status(link::status::up);
        logger::debug() << "STILL INFLIGHT " << pimpl_->tx_inflight_count_;
        if (pimpl_->tx_inflight_count_ > 0)
        {
            start_retransmit_timer();
        }
        else
        {
            logger::debug() << "Stopping retransmission timer";
            pimpl_->retransmit_timer_.stop();
        }

        // Now that we've moved tx_ack_sequence_ forward to the packet's ackseq,
        // they're now the same, which is important to the code below.
        ack_diff = 0;
    }

    assert(ack_diff <= 0);

    // Handle acknowledgments for any straggling out-of-order packets
    // (or an out-of-order acknowledgment for in-order packets).
    // Set the appropriate bits in our tx_ack_mask_,
    // and count newly acknowledged packets within our window.
    uint32_t newmask = (1 << ackct) - 1;
    if ((pimpl_->tx_ack_mask_ & newmask) != newmask) {
        for (unsigned i = 0; i <= ackct; i++) {
            int bit = -ack_diff + i;
            if (bit >= maskBits)
                break;
            if (pimpl_->tx_ack_mask_ & (1 << bit))
                continue;   // already ACKed
            pimpl_->tx_ack_mask_ |= (1 << bit);

            transmit_event_t &e = pimpl_->tx_events_[pimpl_->tx_ack_sequence_ - bit - pimpl_->tx_event_sequence_];
            if (e.pipe_)
            {
                e.pipe_ = false;
                pimpl_->tx_inflight_count_--;
                pimpl_->tx_inflight_size_ -= e.size_;

                acknowledged(pimpl_->tx_ack_sequence_ - bit, 1, pktseq);
            }

            new_packets++;
        }
    }

    // Count the total number of acknowledged packets since the last mark.
    pimpl_->mark_acks_ += new_packets;


    // Always clamp cwnd against CWND_MAX.
    pimpl_->cwnd = min(pimpl_->cwnd, CWND_MAX);

    // Pass the received packet to the upper layer for processing.
    // It'll return true if it wants us to ack the packet, false otherwise.
    if (channel_receive(pktseq, msg))
        acknowledge(pktseq, true);
        // XX should still replay-protect even if no ack!

    // Signal upper layer that we can transmit more, if appropriate
    if (new_packets > 0 and may_transmit())
        on_ready_transmit();
}

} // ssu namespace
