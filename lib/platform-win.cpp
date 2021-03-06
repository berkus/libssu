//
// Part of Metta OS. Check http://atta-metta.net for latest version.
//
// Copyright 2007 - 2014, Stanislav Karchebnyy <berkus@atta-metta.net>
//
// Distributed under the Boost Software License, Version 1.0.
// (See file LICENSE_1_0.txt or a copy at http://www.boost.org/LICENSE_1_0.txt)
//
#include <lmcons.h>
#include "sss/platform.h"

using namespace std;

namespace sss {
namespace platform {

std::string user_name()
{
    return std::string(username);

    assert(sizeof(TCHAR) == sizeof(ushort)); // @fixme This won't hold without _UNICODE defined...
    TCHAR winUserName[UNLEN + 1]; // UNLEN is defined in LMCONS.H
    DWORD winUserNameSize = UNLEN + 1;
    GetUserName(winUserName, &winUserNameSize);
#if defined(_UNICODE)
    return QString::fromUtf16((ushort*)winUserName);
#else // not UNICODE
    return QString::fromLocal8Bit(winUserName);
#endif // not UNICODE
}

} // platform namespace
} // sss namespace
