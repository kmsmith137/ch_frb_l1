### MANUAL.md

This manual is incomplete and some sections are placeholders!

### CONTENTS

  - [High-level overview](#user-content-overview)
  - [Quick-start examples which can run on a laptop](#user-content-laptop)
  - [Examples on the two-node McGill backend](#user-content-two-node-backend)
  - [Config file reference: L1 server](#user-content-l1-config)
  - [Config file reference: L0 simulator](#user-content-l0-config)
  - [RPC reference](#user-content-rpc-reference)

<a name="overview"></a>
### HIGH-LEVEL OVERVIEW  

The L1 server is not finished yet!
Please note the following caveats:

  - **No RFI removal yet**.  Instead there is a placeholder
    processing stage which detrends the data, but does not
    mask RFI.

  - The L0 simulator can only simulate noise; it cannot
    simulate pulses or replay RFI captures.

  - The L1 server is very fragile; if anything goes wrong
    (such as a thread running slow and filling a ring buffer)
    then it will probably crash!

  - Some parts have not been throroughly tested, and
    you may encounter bugs.

The main high-level components are:

  - ch-frb-l1: The "L1 server".

    This is the master executable, which receives UDP packets
    containing one or more beams, dedisperses each beam in a
    separate thread, and passes coarse-grained triggers to "L1b"
    (which runs as a separate process for each beam).

  - ch-frb-simulate-l0: The "L0 simulator".

    This is for testing the L1 server.  It simulates a packet stream
    and sends it to L1, in the same packet format that the CHIME
    correlator will use.

  - RPC client python library.  (To be documented later.)

  - Monitoring webapp.  (To be documented later.)

For compilation instructions, see one of the following:
  - [doc/install.md](./doc/install.md):
      general-purpose install instructions
  - [doc/quick_install_frb1.md](./doc/quick_install_frb1.md):
      simplified install instructions for frb1.physics.mcgill.ca
  - [doc/quick_install_l1_node.md](./doc/quick_install_l1_node.md):
      simplified install instructions for L1 compute nodes (frb-compute-0, frb-compute-1, ...)

<a name="laptop"></a>
### QUICK-START EXAMPLES WHICH CAN RUN ON A LAPTOP

Example 1:

  - Start the L1 server:
    ```
    ./ch-frb-l1  \
       l1_configs/l1_toy_1beam.yaml  \
       rfi_configs/rfi_placeholder.json  \
       bonsai_configs/bonsai_toy_1tree.txt  \
       l1b_config_placeholder
    ```
    There are four configuration files, which will be described shortly!

  - In another window, start the L0 simulator:
    ```
    ./ch-frb-simulate-l0 l0_configs/l0_toy_1beam.yaml 30
    ```

<a name="configuration-file-overview"></a>
### CONFIGURATION FILE OVERVIEW


  - The directory `ch_frb_l1/bonsai_configs/benchmarks` contains four bonsai config files.  The 
    computational cost of each can be measured with the command-line utility:
    ```
    bonsai-time-dedisperser bonsai_configs/benchmarks/params_noups_nbeta1.txt 20
    ```
    This will run 20 copies of the dedisperser, one on each core, when it measures the running time.
    In addition, the placeholder RFI removal in `ch-frb-l1.cpp` should take around 19% of a core.
    Using this procedure, I get the following timings:
    ```
    params_noups_nbeta1.txt: 0.38 + 0.19 = 0.57
    params_noups_nbeta2.txt: 0.42 + 0.19 = 0.61
    params_ups_nbeta1.txt: 0.57 + 0.19 = 0.76
    params_ups_nbeta2.txt: 0.74 + 0.19 = 0.93
    ```
    A puzzle here is that the last config (params_ups_nbeta2.txt) fails to run quickly enough,
    even though it should be able to keep up with the data by a small (7%) margin.

    I usually test using `params_ups_nbeta1.txt`.  This is the most computationally
    expensive option which currently works!


Now is a good place to explain L1 streams.
The L1 node is configured to divide its input into multiple streams, with the
following properties:

 - Each stream corresponds to a unique (ip_address, udp_port) pair.
   For example, a node with four network interfaces, using a single UDP
   port on each, would use four streams.  (Assuming no link bonding!)

 - The beams received by the node must be divided evenly between streams.
   For example, if a node is configured with 16 beams and 2 streams, then
   the correlator is responsible for sending 8 beams to each of the node's
   two (ip_address, udp_port) pairs.

 - Each stream is handled by an independent set of threads, and has an
   independent RPC server.  For example, to tell the node to write all
   of its beams to disk, a separate RPC would need to be sent to every
   stream.
   
   If the node has multiple CPU's, then each stream's threads will be
   "pinned" to one of the CPU's.

The stream configuration of an L1 node is determined by the `ipaddr`, `port`,
and `rpc_address` fields in the L1 configuration file.  See the section
[Config file reference: L1 server](#user-content-l1-config) for more info.

Now let's consider some examples.

  - The "full-scale" 16-beam L1 server, running on an L1 node with
    two CPU's, four NIC's, and no link bonding.

    In this case, the L1 node would need to be configured to use
    four streams (one each NIC), and the correlator nodes would need
    to be configured to send four beams to each IP address.

  - The "full-scale" L1 server with link bonding.

    In this case, the four NIC's behave as one virtual NIC with
    four times the bandwidth and one IP address.  We would still
    need to use two streams (rather than one), to divide processing
    between the two CPU's in the node.  The two streams would have
    the same IP address, but would use different UDP ports.  The
    correlator nodes would need to be configured to send 8 beams
    to each UDP port.

  - A subscale test case running on a laptop.  In this case there
    is usually no reason to use more than one stream.

The L1 server can run in one of two modes:

  - A "full-scale" mode which assumes 16 beams, two 10-core CPU's,
    and either 2 or 4 streams.  In this mode, we usually use 16384
    frequency channels, which means that the L1 server needs 160 GB
    of memory!

  - A "subscale" mode which assumes <=4 beams, and either 1, 2, or 4 streams.
  
    This is intended for development/debugging (e.g. on a laptop).  In this
    case, you'll almost certainly want to decrease the number of frequency
    channels, so that the memory and CPU usage are reasonable.  With 1024
    frequency channels and <= 4 beams, the L0 simulator and L1 server should
    easily run on a laptop over the "loopback" network interface.


<a name="two-node-backend"></a>
### EXAMPLES ON THE TWO-NODE MCGILL BACKEND

This is a placeholder section, which will contain a description of
the McGill two-node backend, and some examples which can be run.


  - Right now the following ports are open on the firewalls of the compute nodes: 10252/udp, 6677/udp, 6677/tcp.
    If you need to open more, the syntax is:
    ```
    sudo firewall-cmd --zone=public --add-port=10252/udp --permanent
    sudo firewall-cmd --reload
    sudo firewall-cmd --list-all
    ```

  - You may run into a nuisance issue where the L1 process hangs for a long time (uninterruptible with control-C)
    after throwing an exception, because it is trying to write its core dump.  My solution is
    `sudo killall -9 abrt-hook-ccpp` in another window.  Let me know if you find a better one!

<a name="l1-config"></a>
### CONFIG FILE REFERENCE: L1 SERVER

The L1 configuration file is a YAML file.
There are some examples in the `ch_frb_l1/l1_configs` directory.

Parameters defining stream configuration, and beams to be processed:

  - `nbeams`: Number of beams (integer).

  - `beam_ids`: 

  - `ipaddr`: IP address (e.g. "127.0.0.1" for loopback interface, or something
     like "10.0.0.100" for a node on the real CHIME network).

     This should either be a list of strings (one for each stream),
     or a single stream (if all streams use the same IP address).

  - `port`: UDP port.  This should be either a list of integers (one for each stream),
     or a single integer (if all streams use the same UDP port).

     Taken together, the ipaddr and port parameters determine the number of streams
     
  - `rpc_address`:

Parameters defining L1B linkage.

  - `l1b_executable_filename`:

  - `l1b_search_path`:

  - `l1b_pipe_capacity`:

  - `l1b_pipe_blocking`:
  
Debugging:

  - `slow_kernels`:

  - `track_global_trigger_max`:

<a name="l0-config"></a>
### CONFIG FILE REFERENCE: L0 SIMULATOR

  - `nbeams`: Number of beams (integer).
  
  - `nthreads`

  - `nupfreq`
  
  - `fpga_counts_per_sample`
  
  - `max_packet_size`
  
  - `gbps_per_stream`
  
  - `ipaddr`

  - `port`

  - `nfreq_coarse_per_packet`

  - `nt_per_packet`


<a name="rpc reference"></a>
### RPC REFERENCE

  - `get_statistics`

  - `write_chunks`


