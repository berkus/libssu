#include <cstddef> // std::size_t
#include <boost/archive/binary_oarchive.hpp>
#include "msgpack.hpp"

/**
 * MessagePack is a tagged format which is compact and rather efficient.
 * This file implements version v5 described at https://gist.github.com/frsyuki/5432559
 * It is byte-oriented and fits well for compact network serialization.
 */
class msgpack_oarchive
    : public boost::archive::binary_oarchive_impl<
        msgpack_oarchive,
        std::ostream::char_type,
        std::ostream::traits_type>
{
    typedef boost::archive::binary_oarchive_impl<
                msgpack_oarchive,
                std::ostream::char_type,
                std::ostream::traits_type> base_t;
    typedef boost::archive::basic_binary_oprimitive<
                msgpack_oarchive, 
                std::ostream::char_type, 
                std::ostream::traits_type> primitive_base_t;
    // give serialization implementation access to this class
    friend base_t;
    friend primitive_base_t;
    friend class boost::archive::basic_binary_oarchive<msgpack_oarchive>;
    friend class boost::archive::save_access;
    // friend class boost::archive::detail::interface_oarchive<msgpack_oarchive>;

    // default fall through for any types not specified here
    template<class T>
    void save(const T & t){
        this->primitive_base_t::save(t);
    }

    // void save_override(const bool t, int)
    // {
        // write_bool(t);
        // msgpack::encode_boolean(*this, t);
    // }

    // void save_override(byte_array const& t, int)
    // {
        // msgpack::encode_array(*this, t, 0xffffffff);
    // }

public:
    msgpack_oarchive(std::ostream & os, unsigned int flags = 0) :
        base_t(os, flags | boost::archive::no_header)
    {
        this->base_t::init(flags);
    }

    // msgpack_oarchive(byte_array& buffer);
    // how to make this work without loads of code? we need to instantiate two extra classes...
};

#include <boost/archive/impl/basic_binary_oarchive.ipp>
// #include <boost/archive/impl/archive_pointer_oserializer.ipp>
#include <boost/archive/impl/basic_binary_oprimitive.ipp>

// @todo #define BOOST_SERIALIZATION_REGISTER_ARCHIVE(Archive)

namespace boost {
namespace archive {

// explicitly instantiate for this type of binary stream
template class binary_oarchive_impl<
    msgpack_oarchive, 
    std::ostream::char_type, 
    std::ostream::traits_type
>;
// template class detail::archive_pointer_oserializer<msgpack_oarchive>;

} // archive namespace
} // boost namespace

#define BOOST_ARCHIVE_CUSTOM_OARCHIVE_TYPES msgpack_oarchive