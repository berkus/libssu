//
// Part of Metta OS. Check http://metta.exquance.com for latest version.
//
// Copyright 2007 - 2014, Stanislav Karchebnyy <berkus@exquance.com>
//
// Distributed under the Boost Software License, Version 1.0.
// (See file LICENSE_1_0.txt or a copy at http://www.boost.org/LICENSE_1_0.txt)
//
#include "ssu/host.h"
#include "ssu/udp_socket.h"

using namespace std;
using namespace ssu;
using namespace uia;

int main()
{
	try
	{
		shared_ptr<host> host(make_shared<host>());
		comm::endpoint local_ep(boost::asio::ip::udp::v4(), stream_protocol::default_port);
		udp_socket l(host);
		l.bind(local_ep);
		l.send(local_ep, "\0SSUohai!", 10);
		host->run_io_service();
	}
	catch (exception& e)
	{
		cerr << e.what() << endl;
	}
}
