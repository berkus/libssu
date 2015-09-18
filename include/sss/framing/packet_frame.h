#pragma once

#include "arsenal/fusionary.hpp"

template <typename H>
class packet_frame_t
{
public:
    int write(boost::asio::mutable_buffer& output) const
    {
        auto l = boost::asio::buffer_size(output);
        output = fusionary::write(output, header_);
        return l - boost::asio::buffer_size(output);
    }

    int read(boost::asio::const_buffer& input)
    {
        auto l = boost::asio::buffer_size(input);
        input = fusionary::read(header_, input).second;
        return l - boost::asio::buffer_size(input);
    }

    bool operator==(const packet_frame_t& o)
    {
        return header_ == o.header_;
    }

protected:
    H header_;
};
