#include <unistd.h>
#include <iostream>
#include "ch_frb_io.hpp"
#include "ringbuf.hpp"
#include "l1-ringbuf.hpp"

using namespace ch_frb_io;
using namespace std;

std::ostream& operator<<(std::ostream& s, const assembled_chunk& ch) {
    s << "assembled_chunk(beam " << ch.beam_id << ", ichunk " << ch.ichunk << " at " << (void*)(&ch) << ")";
    return s;
}

static bool
assembled_chunk_overlaps_range(const shared_ptr<assembled_chunk> ch, 
                               uint64_t min_fpga_counts,
                               uint64_t max_fpga_counts) {
    if (min_fpga_counts == 0 && max_fpga_counts == 0)
        return true;
    uint64_t fpga0 = ch->isample * ch->fpga_counts_per_sample;
    uint64_t fpga1 = fpga0 + constants::nt_per_assembled_chunk * ch->fpga_counts_per_sample;
    cout << "Chunk FPGA counts range " << fpga0 << " to " << fpga1 << endl;
    if ((max_fpga_counts && (fpga0 > max_fpga_counts)) ||
        (min_fpga_counts && (fpga1 < min_fpga_counts)))
        return false;
    return true;
}

AssembledChunkRingbuf::AssembledChunkRingbuf(int binlevel, L1Ringbuf* parent, int maxsize) :
    Ringbuf<assembled_chunk>(maxsize),
    _binlevel(binlevel),
    _parent(parent)
{}

AssembledChunkRingbuf::~AssembledChunkRingbuf() {}

void AssembledChunkRingbuf::dropping(shared_ptr<assembled_chunk> t) {
    _parent->dropping(_binlevel, t);
}

L1Ringbuf::L1Ringbuf(uint64_t beam_id) :
    _beam_id(beam_id),
    _q(),
    _rb(),
    _dropped()
{
    // Create the ring buffer objects for each time binning
    // (0 = native rate, 1 = binned by 2, ...)
    for (size_t i=0; i<Nbins; i++)
        _rb.push_back(shared_ptr<AssembledChunkRingbuf>
                      (new AssembledChunkRingbuf(i, this, 4)));
    // Fill the "_dropped" array with empty shared_ptrs.
    for (size_t i=0; i<Nbins-1; i++)
        _dropped.push_back(shared_ptr<assembled_chunk>());
}

/*
 Tries to enqueue an assembled_chunk.  If no space can be
 allocated, returns false.  The ring buffer now assumes ownership
 of the assembled_chunk.
 */
bool L1Ringbuf::push(assembled_chunk* ch) {
    shared_ptr<assembled_chunk> p = _rb[0]->push(ch);
    if (!p)
        return false;
    _q.push_back(p);
    return true;
}

/*
 Returns the next assembled_chunk for downstream processing.
 */
shared_ptr<assembled_chunk> L1Ringbuf::pop() {
    if (_q.empty())
        return shared_ptr<assembled_chunk>();
    shared_ptr<assembled_chunk> p = _q.front();
    _q.pop_front();
    return p;
}

/*
 Prints a report of the assembled_chunks currently queued.
 */
void L1Ringbuf::print() {
    cout << "L1 ringbuf:" << endl;
    cout << "  downstream: [ ";
    for (auto it = _q.begin(); it != _q.end(); it++) {
        cout << (*it)->ichunk << " ";
    }
    cout << "];" << endl;
    for (size_t i=0; i<Nbins; i++) {
        vector<shared_ptr<assembled_chunk> > v = _rb[i]->snapshot(NULL);
        cout << "  binning " << i << ": [ ";
        for (auto it = v.begin(); it != v.end(); it++) {
            cout << (*it)->ichunk << " ";
        }
        cout << "]" << endl;
        if (i < Nbins-1) {
            cout << "  dropped " << i << ": ";
            if (_dropped[i])
                cout << _dropped[i]->ichunk << endl;
            else
                cout << "none" << endl;
        }
    }
}

void L1Ringbuf::retrieve(uint64_t min_fpga_counts, uint64_t max_fpga_counts,
                         vector<shared_ptr<assembled_chunk> >& chunks) {
    // Check downstream queue
    cout << "Retrieve: checking downstream queue" << endl;
    for (auto it = _q.begin(); it != _q.end(); it++) {
        if (assembled_chunk_overlaps_range(*it, min_fpga_counts, max_fpga_counts)) {
            cout << "  got: " << *(*it) << endl;
            chunks.push_back(*it);
        }
    }
    // Check ring buffers
    for (size_t i=0; i<Nbins; i++) {
        cout << "Retrieve: binning " << i << endl;
        size_t size0 = chunks.size();
        _rb[i]->snapshot(chunks, std::bind(assembled_chunk_overlaps_range, placeholders::_1, min_fpga_counts, max_fpga_counts));
        size_t size1 = chunks.size();
        for (size_t j=size0; j<size1; j++)
            cout << "  got: " << *(chunks[j]) << endl;
        if ((i < Nbins-1) && (_dropped[i])) {
            cout << "Checking dropped chunk at level " << i << endl;
            if (assembled_chunk_overlaps_range(_dropped[i], min_fpga_counts, max_fpga_counts)) {
                chunks.push_back(_dropped[i]);
                cout << "  got: " << *(_dropped[i]) << endl;
            }
        }
    }
}

// Called from the AssembledChunkRingbuf objects when a chunk is
// about to be dropped from one binning level of the ringbuf.  If
// the chunk does not have a partner waiting (in _dropped), then
// it is saved in _dropped.  Otherwise, the two chunks are merged
// into one new chunk and added to the next binning level's
// ringbuf.
void L1Ringbuf::dropping(int binlevel, shared_ptr<assembled_chunk> ch) {
    cout << "Bin level " << binlevel << " dropping a chunk" << endl;
    if (binlevel >= (int)(Nbins-1))
        return;

    if (_dropped[binlevel]) {
        cout << "Now have 2 dropped chunks from bin level " << binlevel << endl;
        // FIXME -- bin down
        assembled_chunk* binned = new assembled_chunk(ch->beam_id, ch->nupfreq, ch->nt_per_packet, ch->fpga_counts_per_sample, _dropped[binlevel]->ichunk);
        // push onto _rb[level+1]
        cout << "Pushing onto level " << (binlevel+1) << endl;
        _rb[binlevel+1]->push(binned);
        cout << "Dropping shared_ptr..." << endl;
        _dropped[binlevel].reset();
        cout << "Done dropping" << endl;
    } else {
        // Keep this one until its partner arrives!
        cout << "Saving as _dropped" << binlevel << endl;
        _dropped[binlevel] = ch;
    }
}

