#include <unistd.h>
#include <string>
#include <iostream>

#include "ch_frb_io.hpp"
#include "assembled_chunk_msgpack.hpp"

#include "rf_pipelines.hpp"
#include "chime_packetizer.hpp"
#include "reverter.hpp"
#include "simpulse.hpp"

#include <l1-rpc.hpp>
#include <chlog.hpp>

using namespace std;
using namespace ch_frb_io;
using namespace rf_pipelines;
using namespace simpulse;

static void usage() {
    cout << "hdf5-stream [options] <HDF5 filenames ...>\n" <<
        "    [-g Gbps],  throttle packet-sending rate\n" <<
        "    [-a <RPC address>], default \"tcp://*:5555\"\n" <<
        "    [-P <RPC port number>] (integer port number)\n" <<
        "    [-t <time of first pulse, seconds>]\n" <<
        "    [-p <period of pulses, seconds>]\n" <<
        "    [-n <number of pulses>]\n" <<
        "    [-f <fluence of pulses>], default 1e5\n" <<
        "    [-d <Dispersion Measure>], deafult 500\n" <<
        endl;
}

/*
 rf_pipelines transforms:

 - make_chime_stream_from_filename_list (hdf5)

 - saver
 - inject frb (w/ S/N for beam 1)
 - (noise_adder?)
 - chime_packetizer --> sends to L1 (beam 1)
 - reverter

 ... repeat for each beam...
 - inject frb (w/ S/N for beam 2)
 - (noise_adder?)
 - chime_packetizer --> sends to L1 (beam 2)
 - reverter


 */

// this function is required to pull assembled_chunks from the end of
// the L1 pipeline.  These would go on to RFI removal and Bonsai
// dedispersion in real life L1...
static void processing_thread_main(shared_ptr<ch_frb_io::intensity_network_stream> stream, int ithread);
                                   
int main(int argc, char **argv) {

    string dest = "127.0.0.1:10252";
    float gbps = 0.0;

    string rpc_port = "";
    int rpc_portnum = 0;

    double pulse_t0 = 0;
    double pulse_period = 0;
    int npulses = 0;

    double fluence = 1e5;
    double dm = 500;

    vector<double> fluence_fractions = { 1.0, 0.3, 0.1 };
    
    int c;
    while ((c = getopt(argc, argv, "g:a:P:t:p:n:f:d:h")) != -1) {
        switch (c) {
	case 'g':
	  gbps = atof(optarg);
	  break;

        case 'a':
            rpc_port = string(optarg);
            break;

        case 'P':
            rpc_portnum = atoi(optarg);
            break;

        case 't':
            pulse_t0 = atof(optarg);
            break;
        case 'p':
            pulse_period = atof(optarg);
            break;
        case 'n':
            npulses = atof(optarg);
            break;
        case 'f':
            fluence = atof(optarg);
            break;
        case 'd':
            dm = atof(optarg);
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
    
    shared_ptr<Saver> saver = make_saver(nt_per_chunk);
    shared_ptr<Reverter> rev;

    // HACK -- should get these from the stream...
    int nfreq = 16 * 1024;
    double freq_lo_mhz = 400;
    double freq_hi_mhz = 800;

    // Pulse properties
    double sm = 0.;
    double intrinsic_width = 0.01;
    double spectral_index = 0.0;

    // Find out the difference between undispersed arrival time and first
    // appearance in the CHIME band.  For DM=500, about 3 seconds.
    shared_ptr<single_pulse> pp = make_shared<single_pulse>
        (1024, nfreq, freq_lo_mhz, freq_hi_mhz,
         dm, sm, intrinsic_width, fluence,
         spectral_index, 0.);
    double pt0, pt1;
    pp->get_endpoints(pt0, pt1);

    // Create the list of pulses to be added
    vector<shared_ptr<single_pulse> > pulses;
    for (int i=0; i<npulses; i++) {
        double undispersed_arrival_time = pulse_t0 - pt0 + i*pulse_period;
	cout << "Creating simulated pulse " << i << endl;
        pulses.push_back(make_shared<single_pulse>
                         (1024, nfreq, freq_lo_mhz, freq_hi_mhz,
                          dm, sm, intrinsic_width, fluence,
                          spectral_index, undispersed_arrival_time));
    }

    // Save the data stream before adding pulses.
    transforms.push_back(saver);

    ch_frb_io::intensity_network_stream::initializer ini_params;
    
    for (size_t j=0; j<fluence_fractions.size(); j++) {
        int beam = 1 + j;
        // Add this beam to the packet receiver's list of beams
        ini_params.beam_ids.push_back(beam);

        // Create the transform that will add pulses to this beam
        // (weighted by the fluence_fraction for this beam)
        shared_ptr<wi_transform> pulser = make_pulse_adder(nt_per_chunk, pulses, fluence_fractions[j]);
        transforms.push_back(pulser);

        // Create the transform that will send packets for this beam
        auto packetizer = make_chime_packetizer(dest, nfreq_coarse_per_packet, nt_per_chunk, nt_per_packet, wt_cutoff, gbps, beam);
        transforms.push_back(packetizer);

        // Create the transform that will revert the stream (no more pulses)
        rev = make_shared<Reverter>(saver);
        transforms.push_back(rev);
    }
    
    // Initialize the packet receiver
    chime_log_open_socket();
    chime_log_set_thread_name("main");
    ini_params.accept_end_of_stream_packets = false;
    ini_params.mandate_fast_kernels = false;
    ini_params.mandate_reference_kernels = true;

    // Make input stream object
    shared_ptr<ch_frb_io::intensity_network_stream> instream = ch_frb_io::intensity_network_stream::make(ini_params);

    // Spawn one processing thread per beam
    std::vector<std::thread> processing_threads;
    for (size_t ibeam = 0; ibeam < ini_params.beam_ids.size(); ibeam++)
        // Note: the processing thread gets 'ibeam', not the beam id,
        // because that is what get_assembled_chunk() takes
        processing_threads.push_back(std::thread(std::bind(processing_thread_main, instream, ibeam)));

    if ((rpc_port.length() == 0) && (rpc_portnum == 0))
        rpc_port = "tcp://127.0.0.1:5555";
    else if (rpc_portnum)
        rpc_port = "tcp://127.0.0.1:" + to_string(rpc_portnum);
    
    chlog("Starting RPC server on " << rpc_port);
    L1RpcServer rpc(instream, rpc_port);
    rpc.start();
    
    // Start listening for packets.
    instream->start_stream();


    // Start the rf_pipelines stream from hdf5 files to network
    stream->run(transforms);

    // This won't happen (we're ignoring end-of-stream packets)
    instream->join_threads();

    
}



static void processing_thread_main(shared_ptr<ch_frb_io::intensity_network_stream> stream,
                                    int ithread) {
    chime_log_set_thread_name("proc-" + std::to_string(ithread));
    chlog("Processing thread main: thread " << ithread);
    for (;;) {
	// Get assembled data from netwrok
	auto chunk = stream->get_assembled_chunk(ithread);
	if (!chunk)
	    break;  // End-of-stream reached
        chlog("Finished beam " << chunk->beam_id << ", chunk " << chunk->ichunk);
    }
}


