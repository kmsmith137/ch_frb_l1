#ifndef L0_SIM_H_
#define L0_SIM_H_

#include <vector>
#include <random>

#include <ch_frb_io.hpp>

class L0Simulator {

public:
    L0Simulator(ch_frb_io::intensity_network_ostream::initializer ini,
                double gb_to_send,
                double drop_rate = 0.0);

    void run();

    void send_one_chunk();

protected:
    std::shared_ptr<ch_frb_io::intensity_network_ostream> stream;
    int ichunk;
    int nchunks;
    std::vector<float> intensity;
    std::vector<float> weights;

    std::ranlux48_base rando;
};



#endif
