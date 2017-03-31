#include <unistd.h>
#include <string>
#include <iostream>

#include "ch_frb_io.hpp"
#include "assembled_chunk_msgpack.hpp"

#include "rf_pipelines.hpp"

using namespace std;
using namespace ch_frb_io;
using namespace rf_pipelines;

static void usage() {
    cout << "hdf5-stream [options] <HDF5 filenames ...>\n" <<
        "    [-d DEST],  DEST like \"127.0.0.1:10252\"\n" <<
        "    [-b BEAM],  BEAM an integer beam id\n" <<
        "    [-t Gbps],  throttle packet-sending rate\n" << endl;
}

int main(int argc, char **argv) {

    string dest = "127.0.0.1:10252";
    float gbps = 0.0;
    int beam = 0;

    int c;
    while ((c = getopt(argc, argv, "d:g:b:h")) != -1) {
        switch (c) {
        case 'b':
            beam = atoi(optarg);
            break;
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

    int nfreq_coarse_per_packet = 4;
    int nt_per_chunk = ch_frb_io::constants::nt_per_assembled_chunk;
    int nt_per_packet = 2;
    float wt_cutoff = 1.;
    
    auto packetizer = make_chime_packetizer(dest, nfreq_coarse_per_packet,
                                            nt_per_chunk, nt_per_packet,
                                            wt_cutoff, gbps, beam);

    vector<shared_ptr<wi_transform> > transforms;
    transforms.push_back(packetizer);
    
    stream->run(transforms);
}
