#pragma once

class stream_protocol
{
public:
    // Control chunk magic value for the structured streams.
    // 0x535355 = 'SSU': 'Structured Streams Unleashed'
    static const uint32_t magic = 0x00535355;

    struct stream_header
    {
    	uint16_t sid;
    	uint8_t  type;
    	uint8_t  window;
    };

    enum class packet_type : uint8_t {
    	invalid  = 0x0,
    	init     = 0x1,
    	reply    = 0x2,
    	data     = 0x3,
    	datagram = 0x4,
    	ack      = 0x5,
    	reset    = 0x6,
    	attach   = 0x7,
    	detach   = 0x8,
    };

    struct init_header : public stream_header
    {
    	uint16_t nsid;
    	uint16_t tx_seq_no;
    };
    typedef init_header reply_header;
    struct data_header : public stream_header
    {
    	uint32_t tx_seq_no;
    };
    typedef stream_header datagram_header;
    typedef stream_header ack_header;
    typedef stream_header reset_header;
    typedef stream_header attach_header;
    typedef stream_header detach_header;
};
