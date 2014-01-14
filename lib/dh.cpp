//
// Part of Metta OS. Check http://metta.exquance.com for latest version.
//
// Copyright 2007 - 2014, Stanislav Karchebnyy <berkus@exquance.com>
//
// Distributed under the Boost Software License, Version 1.0.
// (See file LICENSE_1_0.txt or a copy at http://www.boost.org/LICENSE_1_0.txt)
//
#include <boost/date_time/posix_time/posix_time_types.hpp>
#include "ssu/dh.h"
#include "ssu/host.h"
#include "krypto/utils.h"

using namespace std;

/*
 * The Diffie-Hellman parameter setup has been taken unaltered
 * from SST implementation in Netsteria. @fixme
 */

//=================================================================================================
// Diffie-Hellman parameters for JFDH key agreement
//=================================================================================================

/*
 * These DH parameters need to be standardized for a given cipher suite
 * because nodes negotiating a secret key must use the same parameters.
 * The following DH parameters were chosen according to the key size
 * recommendations in NIST Special Publication 800-57, April 2005 draft,
 * corresponding to 80-bit, 112-bit, and 128-bit security, respectively.
 */

namespace {

DH *get_dh1024()
{
    static unsigned char dh1024_p[]={
        0xE1,0x5A,0x9A,0x8F,0x0F,0x55,0x31,0x50,0x18,0x9E,0x78,0x8C,
        0x6D,0x1E,0x62,0x0B,0xEE,0x4C,0xF0,0x34,0x74,0x82,0x61,0xA8,
        0x42,0x60,0x9C,0x53,0x47,0xFE,0x40,0x49,0x96,0x36,0x1D,0x5F,
        0xAD,0xF0,0xE5,0x4A,0x43,0x94,0x03,0x54,0xCA,0x35,0xA9,0xD4,
        0xE5,0xC3,0xE5,0x32,0x2E,0x26,0xB8,0xE8,0x32,0xE8,0xF1,0xDA,
        0x8E,0xA8,0xBE,0x4D,0xEB,0x79,0x34,0x27,0x37,0x4B,0x13,0x0C,
        0xB0,0x86,0x10,0x1C,0x83,0x8F,0x84,0x49,0xD4,0xE9,0xCB,0x85,
        0x11,0xEC,0x6A,0xF5,0x9C,0x3C,0xBC,0x2A,0x46,0xED,0x4D,0xFE,
        0x0E,0xB1,0x1B,0xE3,0x86,0x93,0x65,0x8D,0xCE,0x7B,0xAD,0xB2,
        0x5A,0xD8,0xFB,0xF9,0x1A,0x49,0xA2,0x23,0xE6,0x01,0x11,0x74,
        0xB9,0xAB,0xAB,0xF4,0x3E,0x2E,0x8E,0x23,
    };
    static unsigned char dh1024_g[]={
        0x02,
    };
    DH *dh{0};

    if ((dh = DH_new()) == nullptr) return nullptr;
    dh->p = BN_bin2bn(dh1024_p, sizeof(dh1024_p), nullptr);
    dh->g = BN_bin2bn(dh1024_g, sizeof(dh1024_g), nullptr);
    if ((dh->p == nullptr) || (dh->g == nullptr)) {
        DH_free(dh);
        return nullptr;
    }
    return dh;
}

DH *get_dh2048()
{
    static unsigned char dh2048_p[]={
        0x85,0x6A,0x9F,0xCD,0xE4,0xE8,0x33,0x07,0x23,0x27,0x10,0xC6,
        0x07,0x59,0x37,0x38,0x02,0xB0,0x6B,0xB9,0xE5,0x7B,0x91,0x61,
        0x76,0x14,0xF1,0xB7,0xBE,0x44,0xA5,0xFF,0x48,0x9A,0x6D,0x3D,
        0x6A,0x76,0x3F,0xFA,0x07,0xE6,0xB0,0xCA,0xB6,0x4B,0xA5,0x69,
        0x89,0x5F,0x2C,0x41,0xD1,0xD1,0x4E,0x1E,0x43,0xE8,0x4F,0xE4,
        0x8B,0x10,0x91,0x04,0x4B,0x06,0xDA,0x76,0xB5,0x4B,0x10,0x01,
        0x21,0x87,0x48,0x17,0x07,0xCB,0x87,0x53,0xD6,0xE4,0xD9,0x82,
        0xC6,0xA3,0xD8,0x9E,0x47,0x23,0x63,0x6A,0xBB,0x40,0x8F,0x20,
        0x06,0x70,0x6B,0xC9,0x50,0x5F,0xD9,0xC7,0x8E,0x81,0x58,0x72,
        0x16,0x26,0x18,0xBE,0xFF,0x9A,0x57,0x86,0x39,0xA7,0xDC,0xFC,
        0xFC,0xEB,0x8F,0x4F,0xB8,0xDF,0x6F,0xE4,0xD1,0x20,0xD0,0x72,
        0xB4,0x8C,0xCC,0x38,0xAC,0x7D,0x24,0x72,0x3A,0x10,0x96,0xB1,
        0x83,0x04,0xF1,0x4F,0xB4,0x20,0xEC,0x3C,0xC1,0x89,0xC9,0xC5,
        0x84,0x2C,0xD9,0xC7,0x3A,0x4D,0xC3,0xC4,0x51,0xC4,0x48,0xF5,
        0x05,0x28,0x2E,0x2E,0x27,0x78,0x99,0x75,0xC0,0x06,0x9D,0x98,
        0xD8,0x90,0x5F,0x8A,0x6F,0x94,0x72,0xCC,0x22,0x35,0x0A,0xB9,
        0x66,0x86,0xBB,0x78,0x5A,0x10,0x81,0xF2,0x6E,0xE8,0x2D,0x60,
        0x10,0x29,0x30,0x45,0x6D,0x6D,0x36,0x91,0xDF,0x26,0xF7,0xDA,
        0x13,0xB4,0x21,0xC9,0x3B,0x97,0x65,0x2A,0xC4,0xF7,0x66,0xED,
        0xF9,0xBB,0x86,0x78,0x59,0x21,0x9B,0xC6,0xF6,0x8D,0x27,0xCB,
        0x12,0x40,0x23,0xCC,0x9C,0x12,0xFE,0x51,0x4D,0xE8,0x5A,0x52,
        0x59,0xD5,0x48,0xF3,
    };
    static unsigned char dh2048_g[]={
        0x02,
    };
    DH *dh{0};

    if ((dh = DH_new()) == nullptr) return nullptr;
    dh->p = BN_bin2bn(dh2048_p, sizeof(dh2048_p), nullptr);
    dh->g = BN_bin2bn(dh2048_g, sizeof(dh2048_g), nullptr);
    if ((dh->p == nullptr) || (dh->g == nullptr)) {
        DH_free(dh);
        return nullptr;
    }
    return dh;
}

DH *get_dh3072()
{
    static unsigned char dh3072_p[]={
        0xBC,0x90,0x66,0x67,0x0F,0xE0,0x7F,0x9E,0xA4,0x8E,0xB6,0x47,
        0x85,0xE6,0x9C,0xD6,0xC1,0x3B,0x12,0xD2,0x9A,0x53,0xB2,0x19,
        0x64,0xA1,0x6D,0xFE,0xE4,0xBB,0x56,0x10,0xCE,0x5C,0x61,0x41,
        0xEC,0xD3,0x2D,0x25,0xA9,0x1E,0x20,0x5B,0x0C,0x0E,0x3C,0x96,
        0x1D,0x14,0x51,0x12,0xC2,0xF0,0x54,0xF9,0xE1,0x56,0x63,0x40,
        0x7A,0x34,0xDB,0x3E,0x89,0x22,0x82,0xA0,0xFA,0x00,0x8A,0x8E,
        0xBB,0x26,0x2E,0xC6,0x0B,0xBE,0x7C,0x35,0x3F,0x2B,0x2D,0xD2,
        0xF1,0x2E,0x68,0xEE,0xBE,0x89,0x28,0x0B,0x5F,0x62,0x8A,0x51,
        0xF6,0x27,0xE2,0x16,0x52,0x0F,0x25,0x68,0x3C,0x5F,0x14,0x18,
        0x58,0x1B,0x4F,0x55,0x9C,0x87,0x16,0x4D,0x12,0xB9,0x13,0xC6,
        0xE8,0xE6,0xE5,0xAC,0xAF,0x24,0xB1,0x49,0x29,0xDE,0x7A,0x5D,
        0x89,0x3E,0x53,0x30,0x1B,0x76,0xA6,0x32,0x63,0xDC,0x6D,0x4F,
        0xFA,0x30,0x81,0xEA,0x5A,0x5F,0x12,0x5D,0x86,0xB3,0xB6,0x79,
        0x7C,0x7D,0xD9,0x7C,0xC2,0xD3,0x3E,0x63,0xAB,0x6F,0x88,0x87,
        0xD9,0x83,0xAB,0x58,0x9E,0x4F,0xE1,0x84,0xED,0x07,0xD1,0x59,
        0x13,0xA7,0x6E,0xB8,0xAC,0xBB,0x51,0xDB,0xC1,0xC3,0x6A,0x0C,
        0xC1,0x17,0x76,0x4B,0xA9,0x89,0x29,0x97,0x54,0xB8,0x52,0xE5,
        0x83,0x16,0xC9,0xCB,0x3C,0xEE,0x9D,0xD1,0x60,0xB9,0xB8,0xAF,
        0x13,0x4B,0xD4,0x06,0x3A,0xD8,0xAD,0x7F,0x5D,0xEF,0x2A,0x17,
        0xAA,0x36,0xBC,0xA4,0x6E,0x30,0x8C,0xB3,0x55,0xA3,0x96,0x72,
        0x11,0xF0,0x67,0xCA,0xC8,0x50,0xD2,0xCD,0xBA,0x79,0x11,0xAE,
        0xC2,0xC4,0x3B,0x8B,0x54,0xB3,0xF2,0x71,0x32,0x98,0xD9,0x7A,
        0x7C,0x76,0x22,0xA8,0x73,0x81,0xB6,0x21,0x97,0x9C,0x1E,0xBF,
        0x7E,0x98,0x4C,0xCD,0x4D,0xE2,0x38,0xAE,0x9F,0x11,0x72,0xFF,
        0x55,0xB6,0xC7,0xF7,0x20,0x26,0xD0,0x94,0x42,0x8B,0x38,0xAF,
        0xBC,0x30,0x98,0x3E,0x2C,0x02,0x3F,0x58,0xE3,0x9D,0xD9,0x88,
        0x10,0xE2,0xBD,0x72,0x41,0x3C,0xF3,0x58,0xDB,0x81,0x83,0x3B,
        0xEE,0x6A,0xBF,0x72,0x13,0x33,0xC6,0xFA,0x0A,0x7A,0xED,0x68,
        0x4E,0x83,0x0C,0x3C,0x49,0x56,0x5F,0xD9,0x01,0x48,0x5A,0xA4,
        0xC7,0x30,0x4B,0xB5,0x61,0xA4,0x8B,0xAF,0x33,0xDF,0x40,0xA2,
        0x85,0xD7,0x4E,0x1B,0x10,0x1E,0x9F,0x66,0xF0,0x0E,0xB8,0xFC,
        0x83,0xC7,0xBB,0x9C,0x18,0xC2,0xE1,0x83,0xD8,0x19,0x6D,0xF3,
    };
    static unsigned char dh3072_g[]={
        0x02,
    };
    DH *dh{0};

    if ((dh = DH_new()) == nullptr) return nullptr;
    dh->p = BN_bin2bn(dh3072_p, sizeof(dh3072_p), nullptr);
    dh->g = BN_bin2bn(dh3072_g, sizeof(dh3072_g), nullptr);
    if ((dh->p == nullptr) || (dh->g == nullptr)) {
        DH_free(dh);
        return nullptr;
    }
    return dh;
}

} // anonymous namespace

//=================================================================================================
// dh_hostkey_t
//=================================================================================================

namespace ssu {
namespace negotiation {

dh_hostkey_t::dh_hostkey_t(shared_ptr<ssu::host> host, negotiation::dh_group_type group, DH *dh)
    : host_(host)
    , expiration_timer_(host.get())
    , group_(group)
    , dh_(dh)
{
    logger::debug() << "Constructing new DH key";

    // Generate the random HMAC secret key
    hmac_secret_key_.resize(crypto::HMACKEYLEN);
    crypto::fill_random(hmac_secret_key_.as_vector());

    // Get the public key into a byte_array
    public_key_ = crypto::utils::bn2ba(dh->pub_key);

    // Force key refresh every hour.
    expiration_timer_.on_timeout.connect(boost::bind(&dh_hostkey_t::timeout, this));
    expiration_timer_.start(boost::posix_time::hours(1));
}

dh_hostkey_t::~dh_hostkey_t()
{
    expiration_timer_.stop();
    DH_free(dh_);
}

void dh_hostkey_t::timeout()
{
    logger::debug() << "Timing out DH key";
    host_->clear_dh_key(group_);
}

byte_array
dh_hostkey_t::calc_key(byte_array const& other_public_key)
{
    BIGNUM* other_bn = crypto::utils::ba2bn(other_public_key);

    byte_array secret;
    secret.resize(dh_size());
    int rc = DH_compute_key((unsigned char*)secret.data(), other_bn, dh_);
    assert(size_t(rc) <= dh_size());
    secret.resize(rc);

    BN_free(other_bn);
    return secret;
}

size_t
dh_hostkey_t::dh_size() const
{
    return DH_size(dh_);
}

} // negotiation namespace

//=================================================================================================
// dh_host_state
//=================================================================================================

dh_host_state::~dh_host_state()
{
    logger::debug() << "Destructing host key state";
    for (auto& key : dh_keys_) {
        key.reset();
    }
}

shared_ptr<negotiation::dh_hostkey_t>
dh_host_state::internal_generate_dh_key(negotiation::dh_group_type group, DH *(*groupfunc)())
{
    DH* dh = groupfunc();
    if (dh == nullptr)
        return nullptr;

    if (!DH_generate_key(dh))
    {
        DH_free(dh);
        return nullptr;
    }

    assert(dh_keys_[int(group)] == nullptr);
    dh_keys_[int(group)] = make_shared<negotiation::dh_hostkey_t>(get_host(), group, dh);

    return dh_keys_[int(group)];
}

shared_ptr<negotiation::dh_hostkey_t>
dh_host_state::get_dh_key(negotiation::dh_group_type group)
{
    if (group >= negotiation::dh_group_type::dh_group_max)
        return nullptr;
    if (dh_keys_[int(group)] != nullptr)
        return dh_keys_[int(group)];

    // Try to generate the requested host key
    switch (group)
    {
        case negotiation::dh_group_type::dh_group_1024:
            return internal_generate_dh_key(group, get_dh1024);
        case negotiation::dh_group_type::dh_group_2048:
            return internal_generate_dh_key(group, get_dh2048);
        case negotiation::dh_group_type::dh_group_3072:
            return internal_generate_dh_key(group, get_dh3072);
        default:
            logger::warning() << "Unknown DH host key group " << int(group) << " specified.";
            return nullptr;
    }
}

void dh_host_state::clear_dh_key(negotiation::dh_group_type group)
{
    assert(dh_keys_[int(group)] != nullptr);
    dh_keys_[int(group)].reset();
}

} // ssu namespace
