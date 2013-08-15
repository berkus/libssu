//
// Part of Metta OS. Check http://metta.exquance.com for latest version.
//
// Copyright 2007 - 2013, Stanislav Karchebnyy <berkus@exquance.com>
//
// Distributed under the Boost Software License, Version 1.0.
// (See file LICENSE_1_0.txt or a copy at http://www.boost.org/LICENSE_1_0.txt)
//
#pragma once

#include "byte_array.h"
#include "link.h"

namespace ssu {

class channel_armor;

/**
 * Abstract base class representing a channel between a local Socket and a remote endpoint.
 */
class channel : public link_channel
{
public:
    static const int hdrlen = 8/*XXX*/;

	virtual void start(bool initiate);
	virtual void stop();
};

} // namespace ssu
