#include <curl/curl.h>
#include "ch_frb_l1.hpp"

using namespace ch_frb_l1;

// -------------------------------------------------------------------------------------------------

int main(int argc, char **argv)
{
    // for fetching frame0_ctime
    curl_global_init(CURL_GLOBAL_ALL);

    l1_server server(argc, argv);

    server.start_logging();
    server.spawn_l1b_subprocesses();
    server.make_output_devices();
    server.make_memory_slab_pools();
    server.make_input_streams();
    server.make_mask_stats();
    server.make_prometheus_servers();
    server.spawn_dedispersion_threads();
    server.make_rpc_servers();

    server.join_all_threads();
    server.print_statistics();

    curl_global_cleanup();
    return 0;
}

