#include <vector>
#include <iostream>

#include <msgpack.hpp>

#include <ch_frb_io.hpp>

using namespace std;
using namespace ch_frb_io;

/*
 This header contains code for both RPC clients (if in C++) and servers.

 RPC calls:

 * get_beam_metadata(void)

     Retrieves status and statistics from an L1 node.

     Returns an array of maps, where the maps are from string to uint64_t.
     The first element of the array contains status for the whole node.
     The second element contains a mapping from sender IP:port to packet counts received from that sender.
     The remaining nodes contain stats for each beam.


 * get_chunks(GetChunks_Request)
     
     Retrieves assembled_chunk data from the L1 ring buffer.

     See below for the contents of GetChunks_Request; in short, request a list
     of beam ids and a range of chunks.

     Returns an array of intensity_chunks from the L1 node's ring buffer, as one giant message.

 * write_chunks(WriteChunks_Request)

     Retrieves assembled_chunk data from the L1 ring buffer and writes them to as HDF5 files to the L1 node's filesystem.

     See below for the contents of WriteChunks_Request; in short, request a list
     of beam ids and a range of chunks, and specify the filenames to which they are to be written.

     Returns a list of WriteChunk_Reply objects, one per beam*chunk.
 */


class GetChunks_Request {
public:
    vector<uint64_t> beams;
    uint64_t min_chunk;    // or 0 for no limit
    uint64_t max_chunk;    // or 0 for no limit
    MSGPACK_DEFINE(beams, min_chunk, max_chunk);
};

class WriteChunks_Request {
public:
    vector<uint64_t> beams;
    uint64_t min_chunk;    // or 0 for no limit
    uint64_t max_chunk;    // or 0 for no limit
    string filename_pattern; // filename printf pattern; file = sprintf(pattern, beam, ichunk)
    MSGPACK_DEFINE(beams, min_chunk, max_chunk, filename_pattern);
};

class WriteChunks_Reply {
public:
    uint64_t beam;
    uint64_t chunk;
    string filename;
    bool success;
    string error_message;
    MSGPACK_DEFINE(beam, chunk, filename, success, error_message);
};




/** Below here is code for packing objects into msgpack mesages, and vice verse. **/

namespace msgpack {
MSGPACK_API_VERSION_NAMESPACE(MSGPACK_DEFAULT_API_NS) {
namespace adaptor {

// Place class template specialization here
template<>
struct convert<shared_ptr<assembled_chunk> > {
    msgpack::object const& operator()(msgpack::object const& o,
                                      shared_ptr<assembled_chunk>& ch) const {
        if (o.type != msgpack::type::ARRAY) throw msgpack::type_error();
        //cout << "convert msgpack object to shared_ptr<assembled_chunk>..." << endl;
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

        if (ch->nt_coarse != nt_coarse) {
            throw runtime_error("ch_frb_l1 rpc: assembled_chunk nt_coarse mismatch");
        }

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
        //cout << "Pack shared_ptr<assembled-chunk> into msgpack object..." << endl;
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

    /* Apparently not needed yet?
template <>
struct object_with_zone<shared_ptr<assembled_chunk> > {
    void operator()(msgpack::object::with_zone& o, shared_ptr<assembled_chunk>  const& v) const {
        o.type = type::ARRAY;
        cout << "Convert shared_ptr<assembled_chunk> into msgpack object_with_zone" << endl;
...
     */

} // namespace adaptor
} // MSGPACK_API_VERSION_NAMESPACE(MSGPACK_DEFAULT_API_NS)
} // namespace msgpack



