#ifndef _CH_FRB_L1_RINGBUF_HPP
#define _CH_FRB_L1_RINGBUF_HPP

#include <vector>
#include <deque>
#include <iostream>

#include "ch_frb_io.hpp"
#include "ringbuf.hpp"

/*
namespace ch_frb_l1 {
#if 0
}; // pacify emacs c-mode
#endif
 */

std::ostream& operator<<(std::ostream& s, const ch_frb_io::assembled_chunk& ch);

class L1Ringbuf;

class AssembledChunkRingbuf : public Ringbuf<ch_frb_io::assembled_chunk> {

public:
    AssembledChunkRingbuf(int binlevel, L1Ringbuf* parent, int maxsize);
    virtual ~AssembledChunkRingbuf();

protected:
    // my time-binning. level: 0 = original intensity stream; 1 =
    // binned x 2, 2 = binned x 4.
    int _binlevel;
    L1Ringbuf* _parent;

    // Called when the given frame *t* is being dropped off the buffer
    // to free up some space for a new frame.
    virtual void dropping(std::shared_ptr<ch_frb_io::assembled_chunk> t);
};

class L1Ringbuf {
    friend class AssembledChunkRingbuf;

    static const size_t Nbins = 4;

public:
    L1Ringbuf(uint64_t beam_id);

    /*
     Tries to enqueue an assembled_chunk.  If no space can be
     allocated, returns false.  The ring buffer now assumes ownership
     of the assembled_chunk.
     */
    bool push(ch_frb_io::assembled_chunk* ch);

    /*
     Returns the next assembled_chunk for downstream processing.
     */
    std::shared_ptr<ch_frb_io::assembled_chunk> pop();

    /*
     Prints a report of the assembled_chunks currently queued.
     */
    void print();

    void retrieve(uint64_t min_fpga_counts, uint64_t max_fpga_counts,
                  std::vector<std::shared_ptr<ch_frb_io::assembled_chunk> >& chunks);

public:
    uint64_t _beam_id;
    
protected:
    // The queue for downstream
    std::deque<std::shared_ptr<ch_frb_io::assembled_chunk> > _q;

    // The ring buffers for each time-binning.  Length fixed at Nbins.
    std::vector<std::shared_ptr<AssembledChunkRingbuf> > _rb;

    // The assembled_chunks that have been dropped from the ring
    // buffers and are waiting for a pair to be time-downsampled.
    // Length fixed at Nbins-1.
    std::vector<std::shared_ptr<ch_frb_io::assembled_chunk> > _dropped;

    // Called from the AssembledChunkRingbuf objects when a chunk is
    // about to be dropped from one binning level of the ringbuf.  If
    // the chunk does not have a partner waiting (in _dropped), then
    // it is saved in _dropped.  Otherwise, the two chunks are merged
    // into one new chunk and added to the next binning level's
    // ringbuf.
    void dropping(int binlevel, std::shared_ptr<ch_frb_io::assembled_chunk> ch);
};


/*
}  // namespace ch_frb_l1
 */

#endif
