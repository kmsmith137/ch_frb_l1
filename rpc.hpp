#include <vector>
#include <iostream>

#include <msgpack.hpp>

#include <ch_frb_io.hpp>
#include <assembled_chunk_msgpack.hpp>

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

