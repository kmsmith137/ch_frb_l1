#include <iostream>
#include <deque>
#include <thread>
#include <mutex>
#include <condition_variable>

#include "l0-sim.hpp"

using namespace std;

L0Simulator::L0Simulator(ch_frb_io::intensity_network_ostream::initializer ini,
                         double gb_to_send,
                         double drop_rate) :
    ichunk(0) {
    // Make output stream object and print a little summary info
    stream = ch_frb_io::intensity_network_ostream::make(ini);

    nchunks = int(gb_to_send * 1.0e9 / stream->nbytes_per_chunk) + 1;
    int npackets = nchunks * stream->npackets_per_chunk;
    int nbytes = nchunks * stream->nbytes_per_chunk;
    cerr << "l0-sim: sending " << (nbytes/1.0e9) << " GB data (" << nchunks << " chunks, " << npackets << " packets)\n";

    intensity = vector<float>(stream->elts_per_chunk, 0.0);
    weights = vector<float>(stream->elts_per_chunk, 1.0);

    // After some testing (in branch uniform-rng, on frb-compute-0),
    // found that std::ranlux48_base is the fastest of the std::
    // builtin random number generators.
    std::random_device rd;
    unsigned int seed = rd();
    std::ranlux48_base rando(seed);
    //cout << "range " << rando.min() << " to " << rando.max() << endl;
    // Range is 2**48.

    // Thought: if we pre-scaled to uint8_t, maybe we can get 6 random
    // numbers per call by pulling out individual bytes...  Would have
    // to modify the packet.encode() method to do this.

#if 0
    // I'd like to simulate Gaussian noise, but the Gaussian random number generation 
    // actually turns out to be a bottleneck!
    std::mt19937 rng(rd());
    std::normal_distribution<> dist;
#endif

}

void L0Simulator::run() {

    for (; ichunk < nchunks; ichunk++) {
        send_one_chunk();
    }
    // All done!
    stream->end_stream(true);  // "true" joins network thread
}

void L0Simulator::send_one_chunk() {
    int stride = stream->nt_per_packet;

    // Send data.  The output stream object will automatically throttle packets to its target bandwidth.

    // To avoid the cost of simulating Gaussian noise, we use the following semi-arbitrary procedure.
    //for (unsigned int i = 0; i < intensity.size(); i++)
    //intensity[i] = ichunk + i;

    // Hackily scale the integer random number generator to
    // produce uniform numbers in [mean - 2 sigma, mean + 2 sigma].
    float mean = 100;
    float stddev = 40;
    float r0 = mean - 2.*stddev;
    float scale = 4.*stddev / (rando.max() - rando.min());
    for (unsigned int i = 0; i < intensity.size(); i++)
        intensity[i] = r0 + scale * (float)rando();

    int64_t fpga_count = int64_t(ichunk) * int64_t(stream->fpga_counts_per_chunk);
    stream->send_chunk(&intensity[0], &weights[0], stride, fpga_count);
}

