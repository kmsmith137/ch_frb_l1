#include <unistd.h>
#include <string>
#include <iostream>

#include <bonsai.hpp>

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
    cout << "terminus-l1 [options] <HDF5 filenames ...>\n" <<
        "    [-g Gbps],  throttle packet-sending rate\n" <<
        "    [-a <RPC address>], default \"tcp://*:5555\"\n" <<
        "    [-P <RPC port number>] (integer port number)\n" <<
        "    [-t <time of first pulse, seconds>]\n" <<
        "    [-p <period of pulses, seconds>]\n" <<
        "    [-n <number of pulses>]\n" <<
        "    [-f <fluence of pulses>], default 1e5\n" <<
        "    [-d <Dispersion Measure>], default 500\n" <<
        "    [-c <bonsai config file>], default bonsai_configs/benchmarks/params_noups_nbeta1.txt\n" <<
        "    [-b <l1b address>], default tcp://127.0.0.1:6666\n" <<
        "    [-B <beam-id>], can be repeated, default -B 1 -B 2 -B 3\n" <<
        "    [-F <fluence-fraction>], can be repeated, fraction of fluence appearing in beam; default -F 1 -F 0.3 -F 0.1\n" <<
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

// this pulls assembled_chunks from the L1 network receiver and
// performs RFI removal and Bonsai dedispersion on them.
static void processing_thread_main(shared_ptr<ch_frb_io::intensity_network_stream> stream,
                                   int ithread,
                                   const bonsai::config_params &cp);


int main(int argc, char **argv) {

    string dest = "127.0.0.1:10252";
    float gbps = 0.0;

    string rpc_port = "";
    int rpc_portnum = 0;

    string l1b_address = "tcp://127.0.0.1:6666";

    string bonsai_config_file = "bonsai_configs/benchmarks/params_noups_nbeta1.txt";
    
    double pulse_t0 = 0;
    double pulse_period = 0;
    int npulses = 0;

    double fluence = 1e5;
    double dm = 500;

    vector<double> fluence_fractions;
    vector<int> beams;
    
    int c;
    while ((c = getopt(argc, argv, "g:a:P:t:p:n:f:d:c:b:B:F:h")) != -1) {
        switch (c) {
        case 'B':
            beams.push_back(atoi(optarg));
            break;
        case 'F':
            fluence_fractions.push_back(atof(optarg));
            break;
        case 'c':
            bonsai_config_file = optarg;
            break;
        case 'b':
            l1b_address = optarg;
            break;
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

    if ((fluence_fractions.size() > 0) && (beams.size() > 0) &&
        (fluence_fractions.size() != beams.size())) {
        cout << "If specified, fluence fractions -F and beam ids -B must be the same length." << endl;
        return -1;
    }
    if ((fluence_fractions.size() == 0) && (beams.size() == 0)) {
        fluence_fractions = { 1.0, 0.3, 0.1 };
    }
    if ((fluence_fractions.size() > 0) && (beams.size() == 0)) {
        for (size_t j=0; j<fluence_fractions.size(); j++) {
            beams.push_back(1+j);
        }
    }
    // if beams are specified but fluence isn't...?
    if (fluence_fractions.size() == 0) {
        cout << "If beams are specified with -B, must also specify fluence fractions for them with -F" << endl;
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
        int beam = beams[j];
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

    zmq::context_t zmqctx;

    size_t nbeams = ini_params.beam_ids.size();
    
    bonsai::config_params bonsai_config(bonsai_config_file);

    bonsai_config.write("bc.txt", "txt", true);
    
    for (size_t ibeam = 0; ibeam < nbeams; ibeam++)
        // Note: the processing thread gets 'ibeam', not the beam id,
        // because that is what get_assembled_chunk() takes
        processing_threads.push_back(std::thread(std::bind(processing_thread_main, instream, ini_params.beam_ids[ibeam], bonsai_config)));

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



// make_rfi_chain(): currently a placeholder which returns an arbitrarily constructed transform chain.
//
// The long-term plan here is:
//   - keep developing RFI removal, until all transforms are C++
//   - write code to serialize a C++ transform chain to yaml
//   - add a command-line argument <transform_chain.yaml> to ch-frb-l1 

static vector<shared_ptr<rf_pipelines::wi_transform>> make_rfi_chain()
{
    int nt_chunk = 1024;
    int polydeg = 2;

    auto t1 = rf_pipelines::make_polynomial_detrender(nt_chunk, rf_pipelines::AXIS_FREQ, polydeg);
    auto t2 = rf_pipelines::make_polynomial_detrender(nt_chunk, rf_pipelines::AXIS_TIME, polydeg);
    
    return { t1, t2 };
}


static void processing_thread_main(shared_ptr<ch_frb_io::intensity_network_stream> instream,
                                   int beam_id,
                                   const bonsai::config_params &cp) {
    chime_log_set_thread_name("proc-" + std::to_string(beam_id));
    chlog("Processing thread main: beam " << beam_id);

    auto stream = rf_pipelines::make_chime_network_stream(instream, beam_id);
    auto transform_chain = make_rfi_chain();

    bonsai::dedisperser::initializer ini_params;
    ini_params.verbosity = 0;
	
    auto dedisperser = make_shared<bonsai::dedisperser> (cp, ini_params);
    transform_chain.push_back(rf_pipelines::make_bonsai_dedisperser(dedisperser));

    // (transform_chain, outdir, json_output, verbosity)
    stream->run(transform_chain, string(), nullptr, 0);
}

