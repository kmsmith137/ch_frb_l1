#ifndef _INJECT_DATA_MSGPACK_HPP
#define _INJECT_DATA_MSGPACK_HPP

#include <vector>
#include <iostream>

#include <msgpack.hpp>

#include <ch_frb_io.hpp>
#include <msgpack_binary_vector.hpp>
#include <rf_pipelines.hpp>

/** Code for packing data-injection requests into msgpack mesages, and vice versa. **/

struct inject_data_binmsg {
    int beam;
    // mode == 0: ADD
    int mode;
    // offset for FPGAcounts values in fpga_offset array.
    uint64_t fpga0;
    // should be "nfreq" in length
    ch_frb_io::msgpack_binary_vector<int32_t> sample_offset;
    // should be "nfreq" in length
    ch_frb_io::msgpack_binary_vector<uint16_t> ndata;
    // should have length = sum(ndata)
    ch_frb_io::msgpack_binary_vector<float> data;
    
    MSGPACK_DEFINE(beam, mode, fpga0, sample_offset, ndata, data);

    void swap(rf_pipelines::inject_data& dest) {
        std::swap(this->beam, dest.beam);
        std::swap(this->mode, dest.mode);
        std::swap(this->fpga0, dest.fpga0);
        std::swap(this->sample_offset, dest.sample_offset);
        std::swap(this->ndata, dest.ndata);
        std::swap(this->data, dest.data);
    }
};

#endif // _INJECT_DATA_MSGPACK_HPP
