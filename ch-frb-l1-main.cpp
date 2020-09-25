#include "ch_frb_l1.hpp"
#include "chlog.hpp"

using namespace ch_frb_l1;

// -------------------------------------------------------------------------------------------------

int main(int argc, const char **argv)
{
    ch_frb_io::chime_log_set_thread_name("ch-frb-l1-main");

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

    return 0;
}

