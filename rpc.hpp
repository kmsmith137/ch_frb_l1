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

 * get_statistics(void)

     Retrieves status and statistics from an L1 node.

     Returns an array of maps, where the maps are from string to
     uint64_t.
     - The first element of the array contains status for the
       whole node.
     - The second element contains a mapping from sender IP:port to
     packet counts received from that sender.
     - The remaining elements contain stats for each beam.

     Example return value (in python notation; # are annotations, not
     part of the message):

     [
       # Node statistics: first packet properties, packet counts
       {'first_packet_received': 1, 'fpga_count': 0, 'nupfreq': 16,
        'nbeams': 4, 'nt_per_packet': 16, 'fpga_counts_per_sample': 400,
        'count_bytes_received': 628752384,
        'count_packets_received': 147456,
        'count_packets_good': 147456,
        'count_packets_bad': 0,
        'count_packets_dropped': 0,
        'count_packets_endofstream': 0,
        'count_beam_id_mismatch': 0,
        'count_stream_mismatch': 0,
        'count_assembler_queued': 28,
        'count_assembler_hits': 589824,
        'count_assembler_drops': 0,
        'count_assembler_misses': 0,
       },

       # Per-node packet counts: number of packets from L0 nodes (this was from
       # an 8-node local test
       {'127.0.0.1:20023': 18432, '127.0.0.1:20031': 18432, '127.0.0.1:20027': 18432,
        '127.0.0.1:20011': 18432, '127.0.0.1:20003': 18432, '127.0.0.1:20015': 18432,
        '127.0.0.1:20007': 18432, '127.0.0.1:20019': 18432},

       # Per-Beam stats
       {'beam_id': 12, 'ringbuf_next': 7, 'ringbuf_capacity': 8,
        'ringbuf_chunk_min': 0, 'ringbuf_chunk_max': 6, 'ringbuf_ready': 0,
        'ringbuf_ntotal': 7},
       {'beam_id': 13, 'ringbuf_next': 7, 'ringbuf_capacity': 8,
        'ringbuf_chunk_min': 0, 'ringbuf_chunk_max': 6, 'ringbuf_ready': 0,
        'ringbuf_ntotal': 7},
       {'beam_id': 14, 'ringbuf_next': 7, 'ringbuf_capacity': 8,
        'ringbuf_chunk_min': 0, 'ringbuf_chunk_max': 6, 'ringbuf_ready': 0,
        'ringbuf_ntotal': 7},
       {'beam_id': 15, 'ringbuf_next': 7, 'ringbuf_capacity': 8,
        'ringbuf_chunk_min': 0, 'ringbuf_chunk_max': 6, 'ringbuf_ready': 0,
        'ringbuf_ntotal': 7},
      ]


 ** NOT IMPLEMENTED ** get_chunks(GetChunks_Request)
     
     Retrieves assembled_chunk data from the L1 ring buffer.

     See below for the contents of GetChunks_Request; in short,
     request a list of beam ids and a range of chunks.

     Returns an array of assembled_chunks from the L1 node's ring
     buffer, as one giant message.


 * write_chunks(WriteChunks_Request)

     Retrieves assembled_chunk data from the L1 ring buffer and writes
     them to as files to the L1 node's filesystem, in a custom msgpack
     format.

     See below for the contents of WriteChunks_Request; in short,
     request a list of beam ids and a range of chunks, and specify the
     filenames to which they are to be written.

     Returns a list of WriteChunk_Reply objects, one per beam*chunk.

     FIXME -- document multiple async replies.

 */


/*
class GetChunks_Request {
public:
    vector<uint64_t> beams;
    uint64_t min_chunk;    // or 0 for no limit
    uint64_t max_chunk;    // or 0 for no limit
    bool compress;
    MSGPACK_DEFINE(beams, min_chunk, max_chunk, compress);
};
 */

class WriteChunks_Request {
public:
    vector<uint64_t> beams;
    uint64_t min_fpga;    // or 0 for no limit
    uint64_t max_fpga;    // or 0 for no limit
    string filename_pattern; // filename printf pattern; file = sprintf(pattern, int beam, uint64_t fpgacounts_start, uint64_t fpgacounts_N)
    int priority;
    MSGPACK_DEFINE(beams, min_fpga, max_fpga, filename_pattern, priority);
};

class WriteChunks_Reply {
public:
    uint64_t beam;
    uint64_t fpga0;
    uint64_t fpgaN;
    string filename;
    bool success;
    string error_message;
    MSGPACK_DEFINE(beam, fpga0, fpgaN, filename, success, error_message);
};

