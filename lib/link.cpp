//
// Part of Metta OS. Check http://metta.exquance.com for latest version.
//
// Copyright 2007 - 2014, Stanislav Karchebnyy <berkus@exquance.com>
//
// Distributed under the Boost Software License, Version 1.0.
// (See file LICENSE_1_0.txt or a copy at http://www.boost.org/LICENSE_1_0.txt)
//
#include "ssu/link.h"
#include "ssu/link_channel.h"
#include "ssu/link_receiver.h"
#include "arsenal/logging.h"
#include "arsenal/flurry.h"
#include "arsenal/byte_array_wrap.h"
#include "arsenal/settings_provider.h"
#include "ssu/host.h"
#include "ssu/platform.h"
#include "arsenal/any_int_cast.h"

using namespace std;
using namespace boost::asio;

namespace ssu {

//=================================================================================================
// helper function
//=================================================================================================

bool bind_socket(boost::asio::ip::udp::socket& sock, ssu::endpoint const& ep, std::string& error_string)
{
    boost::system::error_code ec;
    sock.open(ep.protocol(), ec);
    if (ec) {
        error_string = ec.message();
        logger::warning() << "udp socket open error - " << ec.message();
        return false;
    }
    sock.bind(ep, ec);
    if (ec) {
        error_string = ec.message();
        logger::warning() << "udp socket bind error - " << ec.message();
        return false;
    }
    error_string = "";
    return true;
}

//=================================================================================================
// link_receiver
//=================================================================================================

void link_receiver::bind(magic_t magic)
{
    assert(!is_bound());
    // Receiver's magic value must leave the upper byte 0
    // to distinguish control packets from channel data packets.
    assert(magic <= 0xffffff);
    assert(!host_->has_receiver_for(magic));

    magic_ = magic;
    logger::debug() << "Link receiver " << this << " binds for magic " << hex(magic_, 8, true);
    host_->bind_receiver(magic_, this);
}

void link_receiver::unbind()
{
    if (is_bound()) {
        logger::debug() << "Link receiver " << this << " unbinds magic " << hex(magic_, 8, true);
        host_->unbind_receiver(magic_);
        magic_ = 0;
    }
}

//=================================================================================================
// link_host_state
//=================================================================================================

link_receiver*
link_host_state::receiver(magic_t magic)
{
    auto it = receivers_.find(magic);
    if (it == receivers_.end())
    {
        logger::debug() << "Receiver not found looking for magic " << hex(magic, 8, true);
        return 0;
    }
    return it->second;
}

shared_ptr<link>
link_host_state::create_link()
{
    return make_shared<udp_link>(get_host());
}

void
link_host_state::init_link(settings_provider* settings, uint16_t default_port)
{
    if (primary_link_ and primary_link_->is_active())
        return;

    if (primary_link6_ and primary_link6_->is_active())
        return;

    // See if a port number is recorded in our settings;
    // if so, use that instead of the specified default port.
    if (settings) {
        auto s_port = settings->get("port");
        if (!s_port.empty()) {
            int port = any_int_cast<int16_t>(s_port);
            if (port > 0 && port <= 65535) {
                default_port = port;
            }
        }
    }

    ip::udp::endpoint local_ep6(ip::address_v6::any(), default_port);
    ip::udp::endpoint local_ep(ip::address_v4::any(), default_port);

    // Create and bind the main links.
    primary_link_ = create_link();
    primary_link6_ = create_link();

    // See https://raw.github.com/boostcon/2011_presentations/master/wed/IPv6.pdf
    do {
        if (primary_link_->bind(local_ep)) {
            break;
        }
        logger::warning() << "Can't bind to port " << dec << default_port << " ("
            << primary_link_->error_string() << ") - trying another";

        local_ep.port(0);
        if (primary_link_->bind(local_ep)) {
            break;
        }
        logger::fatal() << "Couldn't bind the link on ipv4 - " << primary_link_->error_string();
    } while(0);

    do {
        if (primary_link6_->bind(local_ep6)) {
            break;
        }
        logger::warning() << "Can't bind to port " << dec << default_port << " ("
            << primary_link6_->error_string() << ") - trying another";

        local_ep6.port(0);
        if (primary_link6_->bind(local_ep6)) {
            break;
        }
        logger::warning() << "Couldn't bind the link on ipv6 ("
            << primary_link6_->error_string() << "), trying ipv4";
    } while(0);

    default_port = primary_link_->local_port();
    //ipv6 may have a different port here...
    // @todo Fix port to whatever worked for the first bind and fail if second bind fails?

    // Remember the port number we ended up using.
    if (settings) {
        settings->set("port", static_cast<int64_t>(default_port));
    }
}

std::unordered_set<endpoint>
link_host_state::active_local_endpoints()
{
    std::unordered_set<endpoint> result;
    for (link* l : active_links())
    {
        assert(l->is_active());
        for (auto ep : l->local_endpoints()) {
            result.insert(ep);
        }
    }
    return result;
}

//=================================================================================================
// link_endpoint
//=================================================================================================

bool
link_endpoint::send(const char *data, int size) const
{
    if (auto l = link_/*.lock()*/)
    {
        return l->send(*this, data, size);
    }
    logger::debug() << "Trying to send on a nonexistent link";
    return false;
}

//=================================================================================================
// link
//=================================================================================================

std::string link::status_string(link::status s)
{
    switch (s) {
        case status::down: return "down";
        case status::stalled: return "stalled";
        case status::up: return "up";
    }
}

link::~link()
{
    // Unbind all channels - @todo this should be automatic, use shared_ptr<link_channel>s?
    for (auto v : channels_)
    {
        v.second->unbind();
    }
}

void
link::set_active(bool active)
{
    active_ = active;
    if (active_) {
        host_->activate_link(this);
    }
    else {
        host_->deactivate_link(this);
    }
}

/**
 * Two packet types we could receive are stream packet (multiple types), starting with channel header
 * with non-zero channel number. It is handled by specific link_channel.
 * Another type is negotiation packet which usually attempts to start a session negotiation, it should
 * have zero channel number. It is handled by registered link_receiver (XXX rename to link_responder?).
 *
 *  Channel header (8 bytes)
 *   31          24 23                                0
 *  +--------------+-----------------------------------+
 *  |   Channel    |     Transmit Seq Number (TSN)     | 4 bytes
 *  +------+-------+-----------------------------------+
 *  | Rsvd | AckCt | Acknowledgement Seq Number (ASN)  | 4 bytes
 *  +------+-------+-----------------------------------+
 *        ... more channel-specific data here ...        variable length
 *
 *  Negotiation header (8+ bytes)
 *   31          24 23                                0
 *  +--------------+-----------------------------------+
 *  |  Channel=0   |     Negotiation Magic Bytes       | 4 bytes
 *  +--------------+-----------------------------------+
 *  |               Flurry array of chunks             | variable length
 *  +--------------------------------------------------+
 */
void
link::receive(const byte_array& msg, const link_endpoint& src)
{
    if (msg.size() < 4)
    {
        logger::debug() << "Ignoring too small UDP datagram";
        return;
    }

    logger::file_dump(msg, "received raw link packet");

    // First byte should be a channel number.
    // Try to find an endpoint-specific channel.
    channel_number cn = msg.at(0);
    link_channel* chan = channel_for(src, cn);
    if (chan)
    {
        return chan->receive(msg, src);
    }

    // If that doesn't work, it may be a global control packet:
    // if so, pass it to the appropriate link_receiver.
    try {
        magic_t magic = msg.as<big_uint32_t>()[0];

        link_receiver* recvr = host_->receiver(magic);
        if (recvr)
        {
            return recvr->receive(msg, src);
        }
        else
        {
            logger::debug() << "Received an invalid message, ignoring unknown channel/receiver "
                            << hex(magic, 8, true) << " buffer contents " << msg;
        }
    }
    catch (exception& e)
    {
        logger::debug() << "Error deserializing received message: '" << e.what()
                        << "' buffer contents " << msg;
    }
}

bool
link::bind_channel(endpoint const& ep, channel_number chan, link_channel* lc)
{
    assert(channel_for(ep, chan) == nullptr);
    channels_.insert(make_pair(make_pair(ep, chan), lc));
    return true;
}

void
link::unbind_channel(endpoint const& ep, channel_number chan)
{
    channels_.erase(make_pair(ep, chan));
}

bool
link::is_congestion_controlled(endpoint const&)
{
    return false;
}

int
link::may_transmit(endpoint const&)
{
    logger::fatal() << "may_transmit() called on a non-congestion-controlled link";
    return -1;
}

//=================================================================================================
// udp_link
//=================================================================================================

udp_link::udp_link(shared_ptr<host> host)
    : link(host)
    , udp_socket(host->get_io_service())
    , received_from(this, endpoint()) // @fixme Dummy endpoint initializer here... init in bind()?
    , strand_(host->get_io_service())
{}

/**
 * See http://stackoverflow.com/questions/12794107/why-do-i-need-strand-per-connection
 * Run prepare_async_receive() through a strand always to make this operation thread safe.
 */
void
udp_link::prepare_async_receive()
{
    boost::asio::streambuf::mutable_buffers_type buffer = received_buffer.prepare(2048);
    udp_socket.async_receive_from(
        boost::asio::buffer(buffer),
        received_from,
        boost::bind(&udp_link::udp_ready_read, this,
          boost::asio::placeholders::error,
          boost::asio::placeholders::bytes_transferred));
}

vector<endpoint>
udp_link::local_endpoints()
{
    vector<endpoint> result{udp_socket.local_endpoint()};
    auto addresses = platform::local_endpoints();
    auto port = local_port();
    for (auto v : addresses) {
        v.port(port);
        result.emplace_back(v);
    }
    return result;
}

uint16_t
udp_link::local_port()
{
    return udp_socket.local_endpoint().port();
}

bool
udp_link::bind(endpoint const& ep)
{
    // if (ep.address().is_v6()) {
        // udp_socket.set_option(ip::v6_only(true));
    // }
    logger::debug() << "udp_link bind on endpoint " << ep;
    if (!bind_socket(udp_socket, ep, error_string_))
        return false;
    logger::debug() << "Bound udp_link on " << ep;
    // once bound, can start receiving datagrams.
    prepare_async_receive();
    set_active(true);
    return true;
}

void
udp_link::unbind()
{
    logger::debug() << "udp_link unbind";
    udp_socket.shutdown(ip::udp::socket::shutdown_both);
    udp_socket.close();
    set_active(false);
}

bool
udp_link::send(const endpoint& ep, const char *data, size_t size)
{
    boost::system::error_code ec;
    size_t sent = udp_socket.send_to(buffer(data, size), ep, 0, ec);
    if (ec or sent < size) {
        error_string_ = ec.message();
    }
    return sent == size;
}

void
udp_link::udp_ready_read(const boost::system::error_code& error, size_t bytes_transferred)
{
    if (!error)
    {
        logger::debug() << "Received "
            << dec << bytes_transferred << " bytes via UDP link from " << received_from
            << " on link " << this;
        byte_array b(buffer_cast<const char*>(received_buffer.data()), bytes_transferred);
        receive(b, received_from);
        received_buffer.consume(bytes_transferred);
        strand_.dispatch([this] { prepare_async_receive(); });
    }
    else
    {
        error_string_ = error.message();
        logger::warning() << "UDP read error - " << error_string_;
    }
}

}
