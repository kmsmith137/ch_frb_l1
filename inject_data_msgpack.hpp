#ifndef _INJECT_DATA_MSGPACK_HPP
#define _INJECT_DATA_MSGPACK_HPP

#include <vector>
#include <iostream>

#include <msgpack.hpp>

#include <ch_frb_io.hpp>
#include <rf_pipelines.hpp>

/** Code for packing objects into msgpack mesages, and vice versa. **/

struct inject_data_request : public rf_pipelines::inject_data {
    // FIXME -- should probably hand-craft this!
    MSGPACK_DEFINE(beam, mode, fpga0, sample_offset, ndata, data);
};

//struct inject_data_request_2 : public rf_pipelines::inject_data {
struct inject_data_request_2 : public inject_data_request {
};

template <typename Stream>
void pack_inject_data(msgpack::packer<Stream>& o,
                      std::shared_ptr<rf_pipelines::inject_data> const& inj) {
    // pack member variables as an array.
    uint8_t version = 1;
    // We are going to pack N items as a msgpack array (with mixed types)
    o.pack_array(7);
    o.pack(version);
    o.pack(inj->beam);
    o.pack(inj->mode);
    o.pack(inj->fpga0);
    o.pack(inj->sample_offset);
    o.pack(inj->ndata);
    // PACK FLOATS AS BINARY
    o.pack_bin(inj->data.size() * sizeof(float));
    o.pack_bin_body(reinterpret_cast<const char*>(inj->data.data()));
}

namespace msgpack {
MSGPACK_API_VERSION_NAMESPACE(MSGPACK_DEFAULT_API_NS) {
namespace adaptor {

  // Unpack a msgpack object into an inject_data_request
template<>
struct convert<std::shared_ptr<inject_data_request_2> > {
    msgpack::object const& operator()(msgpack::object const& o,
                                      std::shared_ptr<inject_data_request_2>& inj) const {
        if (o.type != msgpack::type::ARRAY) throw msgpack::type_error();
        // Make sure array is big enough to check version
        if (o.via.array.size < 1)
            throw msgpack::type_error();
        msgpack::object* arr = o.via.array.ptr;
        std::cout << "inject_data_2: unpacking version" << std::endl;
        uint8_t version            = arr[0].as<uint8_t>();
        if (version != 1)
            throw std::runtime_error("ch_frb_io: inject_data: expected version=1");
        if (o.via.array.size != 7)
            throw std::runtime_error("ch_frb_io: inject_data: expected array size 7");

        inj->beam = arr[1].as<int>();
        inj->mode = arr[2].as<int>();
        inj->fpga0 = arr[3].as<uint64_t>();
        std::cout << "inject_data_2: ver " << (int)version << ", beam " << inj->beam << ", mode " << inj->mode << ", fpga0 " << inj->fpga0 << std::endl;
        inj->sample_offset = arr[4].as<std::vector<int32_t> >();
        inj->ndata = arr[5].as<std::vector<uint16_t> >();
        std::cout << "inject_data_2: n sample_offset, ndata " << inj->sample_offset.size() <<
            ", " << inj->ndata.size() << std::endl;

        std::cout << "data type: " << int(arr[6].type) << std::endl;
        if (arr[6].type != msgpack::type::BIN) throw msgpack::type_error();
        int sz = arr[6].via.bin.size;
        std::cout << "inject_data_2: n bytes " << sz << std::endl;
        inj->data.resize(sz / sizeof(float));
        memcpy(reinterpret_cast<void*>(inj->data.data()), arr[6].via.bin.ptr, sz);
        std::cout << "did memcpy" << std::endl;
        return o;
    }
};

// Pack an inject_data object into a msgpack stream.
template<>
struct pack<std::shared_ptr<rf_pipelines::inject_data> > {
    template <typename Stream>
    packer<Stream>& operator()(msgpack::packer<Stream>& o, std::shared_ptr<rf_pipelines::inject_data>  const& inj) const {
        pack_inject_data(o, inj);
        return o;
    }
};

} // namespace adaptor
} // MSGPACK_API_VERSION_NAMESPACE(MSGPACK_DEFAULT_API_NS)
} // namespace msgpack

#endif // _INJECT_DATA_MSGPACK_HPP
