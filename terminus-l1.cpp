#include <unistd.h>
#include <string>
#include <iostream>

#include "ch_frb_io.hpp"
#include "assembled_chunk_msgpack.hpp"

#include "rf_pipelines.hpp"
#include "chime_packetizer.hpp"
#include "reverter.hpp"

using namespace std;
using namespace ch_frb_io;
using namespace rf_pipelines;

static void usage() {
    cout << "hdf5-stream [options] <HDF5 filenames ...>\n" <<
        "    [-d DEST],  DEST like \"127.0.0.1:10252\"\n" <<
        "    [-b BEAM],  BEAM an integer beam id\n" <<
        "    [-t Gbps],  throttle packet-sending rate\n" << endl;
}

/*
 rf_pipelines transforms:

 - make_chime_stream_from_filename_list (hdf5)
 - inject frb
 - noisy_packetizer --> sends to L1 (beam 1)
 - noisy_packetizer --> sends to L1 (beam 2)
 - noisy_packetizer --> sends to L1 (beam 3)
 - noisy_packetizer --> sends to L1 (beam 4)



OR

 - make_chime_stream_from_filename_list (hdf5)

 - saver 1
 - inject frb (w/ S/N for beam 1)
 - (noise_adder?)
 - chime_packetizer --> sends to L1 (beam 1)
 - reverter 1

 ... repeat for ...
 - noisy_packetizer --> sends to L1 (beam 2)
 - noisy_packetizer --> sends to L1 (beam 3)
 - noisy_packetizer --> sends to L1 (beam 4)


 */

int main(int argc, char **argv) {

    string dest = "127.0.0.1:10252";
    float gbps = 0.0;

    int c;
    while ((c = getopt(argc, argv, "d:g:h")) != -1) {
        switch (c) {
	case 'd':
	  dest = string(optarg);
	  break;
	case 'g':
	  gbps = atof(optarg);
	  break;
        case 'h':
        case '?':
        default:
	  usage();
	  return 0;
        }
    }
    argc -= optind;
    argv += optind;

    if (argc == 0) {
      cout << "Need hdf5 input filenames!" << endl;
      usage();
      return -1;
    }

    vector<string> fns;
    for (int i=0; i<argc; i++)
      fns.push_back(string(argv[i]));

    auto stream = make_chime_stream_from_filename_list(fns);

    vector<shared_ptr<wi_transform> > transforms;

    int nt_per_chunk = ch_frb_io::constants::nt_per_assembled_chunk;
    int nfreq_coarse_per_packet = 4;
    int nt_per_packet = 2;
    float wt_cutoff = 1.;
    int beam = 1;
    
    shared_ptr<Saver> saver = make_saver(nt_per_chunk);
    shared_ptr<Reverter> rev = make_shared<Reverter>(saver);
    
    auto packetizer = make_chime_packetizer(dest, nfreq_coarse_per_packet, nt_per_chunk, nt_per_packet, wt_cutoff, gbps, beam);

    transforms.push_back(saver);
    transforms.push_back(packetizer);
    transforms.push_back(rev);

    beam = 2;
    packetizer = make_chime_packetizer(dest, nfreq_coarse_per_packet, nt_per_chunk, nt_per_packet, wt_cutoff, gbps, beam);

    // Can't just re-use the transform, must create new one.
    rev = make_shared<Reverter>(saver);

    transforms.push_back(packetizer);
    transforms.push_back(rev);

    beam = 3;
    packetizer = make_chime_packetizer(dest, nfreq_coarse_per_packet, nt_per_chunk, nt_per_packet, wt_cutoff, gbps, beam);
    
    transforms.push_back(packetizer);
    // don't need to restore the last one in the transform chain...
    //rev = make_shared<Reverter>(saver);
    //transforms.push_back(rev);
    
    stream->run(transforms);
}
