#include <vector>
#include <iostream>

#include <msgpack.hpp>

#include <ch_frb_io.hpp>
#include <assembled_chunk_msgpack.hpp>

/*
 This header contains code for both RPC clients (if in C++) and servers.

 RPC calls:

 Each RPC call contains a header, of type Rpc_Request, with the name
 of the function to be called, and a token.  This token is included in
 each reply message (replies are multi-part messages; the first part
 is the token).

 The RPC functions are:

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


 * list_chunks(void)

     Retrieves the list of chunks in the ring buffers of this L1 node.

     Returns an array of [ beam, fpga0, fpga1, bitmask ] arrays, one for each buffered assembled_chunk.  For simplicity, each of these elements is a uint64_t.


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

     The server replies immediately with a list of the chunks to be
     written out, as a list of WriteChunk_Reply objects.  Then, as the
     chunks are actually written to disk, it sends a series of
     WriteChunk_Reply objects.  The initial reply indicates to the
     client how many additional chunk replies it should expect.

     See below for the contents of WriteChunks_Request; in short,
     request a list of beam ids and a range of FPGA-count times, and
     specify the filenames to which they are to be written.

     The filename pattern is not a printf-like pattern, instead it
     supports the following replacements:
         (STREAM)  -> %02i beam_id
         (BEAM)    -> %04i beam_id
         (CHUNK)   -> %08i ichunk
         (NCHUNK)  -> %02i  size in chunks
         (BINNING) -> %02i  size in chunks
         (FPGA0)   -> %012i start FPGA-counts
         (FPGAN)   -> %08i  FPGA-counts size
     (see ch_frb_io : assembled_chunk : format_filename)

 * get_writechunk_status(string pathname)

     Returns the status of a previous write_chunks() request.

     Returns: (status, error_message)

     Status is "SUCCEEDED", "FAILED", "UNKNOWN", or "QUEUED"
     Error message gives whatever information we have if it "FAILED".
     UNKNOWN means we don't have any information about that path name.

 * shutdown()

     Flushes the intensity_network_stream and shuts down the RPC
     server ASAP.  Any queued write requests that have not been
     completed will be dropped.

 * start_logging(string address)

 * stop_logging(string address)

     Starts or stops chlog() from sending to the given log server.

 */

// See l1-rpc.cpp for some implementation details.

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

class Rpc_Request {
public:
    std::string function;
    uint32_t token;
    MSGPACK_DEFINE(function, token);
};

class WriteChunks_Request {
public:
    std::vector<uint64_t> beams;
    uint64_t min_fpga;    // or 0 for no limit
    uint64_t max_fpga;    // or 0 for no limit

    // CURRENTLY UNIMPLEMENTED:
    // dispersion measure, when requesting a sweep
    float dm;
    // uncertainty in DM, when requesting a sweep
    float dm_error;
    // when requesting a sweep, requested width (in seconds) around the given DM
    float sweep_width;
    // when requesting frequency-binned data
    int frequency_binning;

    std::string filename_pattern; // filename pattern
    int priority;
    MSGPACK_DEFINE(beams, min_fpga, max_fpga, dm, dm_error, sweep_width, frequency_binning, filename_pattern, priority);
};

class WriteChunks_Reply {
public:
    uint64_t beam;
    uint64_t fpga0;
    uint64_t fpgaN;
    std::string filename;
    bool success;
    std::string error_message;
    MSGPACK_DEFINE(beam, fpga0, fpgaN, filename, success, error_message);
};

