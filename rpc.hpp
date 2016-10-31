#include <vector>
#include <iostream>

#include <msgpack.hpp>

#include <ch_frb_io.hpp>

using namespace std;
using namespace ch_frb_io;

class GetChunks_Request {
public:
    vector<uint64_t> beams;
    uint64_t min_chunk;
    uint64_t max_chunk;
    MSGPACK_DEFINE(beams, min_chunk, max_chunk);
};


namespace msgpack {
MSGPACK_API_VERSION_NAMESPACE(MSGPACK_DEFAULT_API_NS) {
namespace adaptor {

// Place class template specialization here
template<>
struct convert<shared_ptr<assembled_chunk> > {
    msgpack::object const& operator()(msgpack::object const& o,
                                      shared_ptr<assembled_chunk>& ch) const {
        if (o.type != msgpack::type::ARRAY) throw msgpack::type_error();
        cout << "convert msgpack object to shared_ptr<assembled_chunk>..." << endl;
        if (o.via.array.size != 12) throw msgpack::type_error();
        msgpack::object* arr = o.via.array.ptr;

        int beam_id                = arr[0].as<int>();
        int nupfreq                = arr[1].as<int>();
        int nt_per_packet          = arr[2].as<int>();
        int fpga_counts_per_sample = arr[3].as<int>();
        int nt_coarse              = arr[4].as<int>();
        int nscales                = arr[5].as<int>();
        int ndata                  = arr[6].as<int>();
        uint64_t ichunk            = arr[7].as<uint64_t>();
        uint64_t isample           = arr[8].as<uint64_t>();

        ch = assembled_chunk::make(beam_id, nupfreq, nt_per_packet, fpga_counts_per_sample, ichunk);
        ch->isample = isample;

        if (arr[ 9].type != msgpack::type::BIN) throw msgpack::type_error();
        if (arr[10].type != msgpack::type::BIN) throw msgpack::type_error();
        if (arr[11].type != msgpack::type::BIN) throw msgpack::type_error();

        int nsdata = nscales * sizeof(float);
        if (arr[ 9].via.bin.size != nsdata) throw msgpack::type_error();
        if (arr[10].via.bin.size != nsdata) throw msgpack::type_error();
        if (arr[11].via.bin.size != ndata ) throw msgpack::type_error();
        
        memcpy(ch->scales,  arr[ 9].via.bin.ptr, nsdata);
        memcpy(ch->offsets, arr[10].via.bin.ptr, nsdata);
        memcpy(ch->data,    arr[11].via.bin.ptr, ndata);

        return o;
    }
};

template<>
struct pack<shared_ptr<assembled_chunk> > {
    template <typename Stream>
    packer<Stream>& operator()(msgpack::packer<Stream>& o, shared_ptr<assembled_chunk>  const& ch) const {
        // packing member variables as an array.
        cout << "Pack shared_ptr<assembled-chunk> into msgpack object..." << endl;
        o.pack_array(12);
        o.pack(ch->beam_id);
        o.pack(ch->nupfreq);
        o.pack(ch->nt_per_packet);
        o.pack(ch->fpga_counts_per_sample);
        o.pack(ch->nt_coarse);
        o.pack(ch->nscales);
        o.pack(ch->ndata);
        o.pack(ch->ichunk);
        o.pack(ch->isample);
        // PACK FLOATS AS BINARY
        int nscalebytes = ch->nscales * sizeof(float);
        o.pack_bin(nscalebytes);
        o.pack_bin_body(reinterpret_cast<const char*>(ch->scales),
                        nscalebytes);
        o.pack_bin(nscalebytes);
        o.pack_bin_body(reinterpret_cast<const char*>(ch->offsets),
                        nscalebytes);

        o.pack_bin(ch->ndata);
        o.pack_bin_body(reinterpret_cast<const char*>(ch->data), ch->ndata);
        return o;
    }
};

template <>
struct object_with_zone<shared_ptr<assembled_chunk> > {
    void operator()(msgpack::object::with_zone& o, shared_ptr<assembled_chunk>  const& v) const {
        o.type = type::ARRAY;
        cout << "Convert shared_ptr<assembled_chunk> into msgpack object_with_zone" << endl;
        /*
        o.via.array.size = 2;
        o.via.array.ptr = static_cast<msgpack::object*>(
            o.zone.allocate_align(sizeof(msgpack::object) * o.via.array.size));
        o.via.array.ptr[0] = msgpack::object(v.get_name(), o.zone);
        o.via.array.ptr[1] = msgpack::object(v.get_age(), o.zone);
         */
    }
};

} // namespace adaptor
} // MSGPACK_API_VERSION_NAMESPACE(MSGPACK_DEFAULT_API_NS)
} // namespace msgpack


