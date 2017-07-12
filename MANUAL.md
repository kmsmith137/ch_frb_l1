### MANUAL.md

This manual is incomplete and some sections are placeholders!

### CONTENTS

  - [High-level overview](#user-content-overview)
  - [Quick-start examples which can run on a laptop](#user-content-laptop)
  - [Configuration file overview](#user-content-configuration-file-overview)
  - [L1 streams](#user-content-l1-streams)
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

  - The L1 server doesn't pass FPGA-count timestamps to L1b yet.

  - There is currently a technical issue in the bonsai code
    which requires an artificially large bonsai chunk size (8 seconds)
    in order to search the full (DM, pulse width) parameter space
    (roughly DM <= 13000 and width <= 100 ms).

    In the current set of bonsai configuration files, I've chosen
    to search the full parameter space, using an 8-second chunk size.
    It would also be possible to make an alternate set of configuration
    files which use a 1-second bonsai chunk size, but search a more
    limited parameter space (roughly DM <= 3200 and width <= 8 ms).
    Let me know if this would be useful!

    Eventually, this technical issue will be fixed, and it will be
    possible to simultaneously use a 1-second chunk size, and search
    the full CHIME parameter space.

  - The L1 server is fragile; if anything goes wrong
    (such as a thread running slow and filling a ring buffer)
    then it will throw an exception and die.

    I find that this is actually convenient for debugging, but
    for production we need to carefully enumerate corner cases and
    make sure that the L1 server recovers sensibly.
    
  - The code is in a "pre-alpha" state, and serious testing
    will probably uncover bugs!

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


### INSTALLATION

The L1 code consists of the following git repositories.
Depending on what you're doing, you may only need a subset of these!
In particular, the modules marked "frb1 only" include hardcoded pathnames on
frb1.physics.mcgill.ca, and probably won't be useful on other machines.
For a dependency graph, see [dependencies.png](./dependencies.png).

  - [kiyo-masui/bitshuffle](https://github.com/kiyo-masui/bitshuffle):
    "bitwise" compression algorithm used throughout CHIME.
  - [kmsmith137/simd_helpers](https://github.com/kmsmith137/simd_helpers):
    header-only library for writing x86 assembly language kernels.
  - [kmsmith137/sp_hdf5](https://github.com/kmsmith137/sp_hdf5):
    header-only library for reading/writing hdf5 from C++.
  - [kmsmith137/simpulse](https://github.com/kmsmith137/simpulse):
    library for simulating FRB's and pulsars.
  - [CHIMEFRB/ch_frb_io](https://github.com/CHIMEFRB/ch_frb_io):
    networking code, CHIME-specific file formats.
  - [CHIMEFRB/bonsai](https://github.com/CHIMEFRB/bonsai):
    fast tree dedispersion on x86.
  - [kmsmith137/rf_pipelines](https://github.com/kmsmith137/rf_pipelines):
    plugin-based radio astronomy pipelines.
  - [mrafieir/ch_frb_rfi](https://github.com/mrafieir/ch_frb_rfi):
    scritping framework for RFI removal and offline L1 analysis.  **(frb1 only)**
  - [mburhanpurkar/web_viewer](https://github.com/mburhanpurkar/web_viewer):
    web-based visualization of L1 pipeline outputs.  **(frb1 only)**
  - [kmsmith137/ch_frb_l1](https://github.com/kmsmith137/ch_frb_l1):
    toplevel repo, whose README you're reading right now.

There are also a lot of external dependencies!  (For a complete list,
see [doc/install.md](./doc/install.md).)

If you're using one of the CHIME machines (frb1, frb-compute-X), then all
external dependencies should already be installed, and you can use one of the
following "cheat sheets" to install the L1 pipeline from scratch.

  - [Installing from scratch on frb1.physics.mcgill.ca](./doc/quick_install_frb1.md)
  - [Installing from scratch on an frb-compute-X compute node](./doc/quick_install_l1_node.md)

If you're using another machine (e.g. a laptop) then the installation process
is more involved.  You'll probably need to write some Makefile include files
("Makefile.local" files).  We hope to streamline this process at some point!
For now, please see:

  - [General-purpose install instructions](./doc/install.md)


### HELP! MY PIPELINE IS BROKEN

Since the pipeline is under continuous development, and updates to pipeline
modules depend on each other, at some point you may find yourself with an 
inconsistent set of modules.  In this case, you can use the following
"cheat sheet" to put all pipeline modules on their master branches,
update from git, and rebuild everything from scratch.

   - [Rebuilding the pipeline](./doc/rebuilding_pipeline.md)


<a name="laptop"></a>
### QUICK-START EXAMPLES WHICH CAN RUN ON A LAPTOP

**Example 1:** simplest example: one beam, 1024 frequency channels, one dedispersion tree.
The L0 simulator and L1 server run on the same machine (e.g. a laptop), and exchange packets 
over the loopback interface (127.0.0.1).

  - Start the L1 server:
    ```
    ./ch-frb-l1  \
       l1_configs/l1_toy_1beam.yaml  \
       rfi_configs/rfi_placeholder.json  \
       bonsai_configs/bonsai_toy_1tree.txt  \
       l1b_config_placeholder
    ```
    There are four configuration files, which will be described in the
    next section ([Configuration file overview](#user-content-configuration-file-overview)).

    After you start the L1 server, you should see the line "ch_frb_io: listening for packets..."
    and the server will pause.

  - In another window, start the L0 simulator:
    ```
    ./ch-frb-simulate-l0 l0_configs/l0_toy_1beam.yaml 30
    ```
    This will simulate 30 seconds of data.  If you switch back to the L1 server's window,
    you'll see some output as chunks of data are processed.  After it finishes, you'll see
    a line "toy-l1b.py: wrote toy_l1b_beam0.png".  This is a plot of the coarse-grained
    triggers, which will not contain any interesting features, because the simulation
    is pure noise.


**Example 2:** similar to example 1, but slightly more complicated as follows.  
We process 4 beams, which are divided between UDP ports 6677 and 6688 (two beams per UDP port).
This is more representative of the real L1 server configuration, where 16 beams
will either be divided between four IP addresses, or divided between two UDP ports,
depending on whether we end up using link bonding.  See the section 
[L1 streams](#user-content-l1-streams) below for more discussion.

In example 2, we also use three dedispersion trees which search different
parts of the (DM, pulse width) parameter space.  See comments in the bonsai
config files for more details (and MANUAL.md in the bonsai repo).
This is more representative of the real search, where we plan to use
6 or 7 trees (I think!)

  - Start the L1 server:
    ```
    ./ch-frb-l1  \
       l1_configs/l1_toy_4beams.yaml  \
       rfi_configs/rfi_placeholder.json  \
       bonsai_configs/bonsai_toy_3trees.txt  \
       l1b_config_placeholder
    ```
    There are four configuration files, which will be described shortly!

  - In another window, start the L0 simulator:
    ```
    ./ch-frb-simulate-l0 l0_configs/l0_toy_4beams.yaml 30
    ```

After the example finishes, you should see four plots toy_l1b_beam$N.png,
where N=0,1,2,3.  Each plot corresponds to one beam, and contains three
rows of output, corresponding to the three dedispersion trees in this example.

<a name="configuration-file-overview"></a>
### CONFIGURATION FILE OVERVIEW

The command-line syntax for ch-frb-l1:
```
Usage: ch-frb-l1 [-vp] <l1_config.yaml> <rfi_config.txt> <bonsai_config.txt> <l1b_config_file>
  The -v flag increases verbosity of the toplevel ch-frb-l1 logic
  The -p flag enables a very verbose debug trace of the pipe I/O between L1a and L1b
```
The L1 server takes four parameter files as follows:

  - The 

  - The utility `bonsai-show-config` is useful!

  - [ instructions for reading a bonsai config file from python ]

  - As mentioned previously, there is a technical issue in the bonsai code.
    If you modify the 16K-frequency bonsai config files, there's a good chance you'll get cryptic
    errors like "bonsai_ups_nbeta2.txt: nt_tree[3]=256 is too small (minimum value for this config = 320)".
    Eventually this will be fixed!

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

Command-line syntax for ch-frb-simulate-l0:
```
Usage: ch-frb-simulate-l0 <l0_params.yaml> <num_seconds>
```

<a name="l1-streams"></a>
### L1 STREAMS

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

  - The "production-scale" 16-beam L1 server, running on an L1 node with
    two CPU's, four NIC's, and no link bonding.

    In this case, the L1 node would need to be configured to use
    four streams (one each NIC), and the correlator nodes would need
    to be configured to send four beams to each IP address.

  - The "production-scale" L1 server with link bonding.

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

  - A "production-scale" mode which assumes 16 beams, two 10-core CPU's,
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

##### High-level parameters

  - `nbeams` (integer).

    Number of beams processed by the node.

  - `ipaddr` (either a string, or a list of strings)

    One or more IP addresses, for the L1 server's stream(s).  This can be either a list
    of strings (one for each stream), or a single string (if all streams use the same
    IP address).  IP addresses are specified as strings, e.g. "127.0.0.1".

  - `port`: UDP port (either an integer, or a list of integers)

    One or more UDP ports, for the L1 server's stream(s).  This can be either a list
    of UDP ports (one for each stream), or a single UDP port (if all streams use the same
    port).

  - `rpc_address`:

    XXX

  - `beam_ids`: 

    XXX

    Note that in a setup with multiple L1 nodes, we would currently need a separate config
    file for each node, because the beam IDs would be different!  This will be fixed soon,
    by defining config file syntax which allows the beam IDs to be derived from the L1 node's
    IP address.

  - `slow_kernels` (boolean, default=false).

    By default (slow_kernels=false), the L1 server uses fast assembly-language kernels
    for packet-level operations such as decoding and buffer assembly.  However, these
    are only implemented if the L0 simulator sends 16 time samples per packet, the number
    of frequencies is >= 2048, and the CPU has the AVX2 instruction set.  If slow_kernels=true,
    then slow reference kernels will be used.

    When running the L1 server in its "production-scale" mode on 20-core nodes, I recommend
    setting slow_kernels=false, since the reference kernels may be too slow to keep
    up with the data.  You may need to force the L0 simulator to send 16 time samples
    per packet, by setting nt_per_packet=16 in its yaml file.

    When running the L1 server on subscale test instances, I recommend setting slow_kernels=true,
    since you may not have the AVX2 instruction set (e.g. on a laptop), and you may not want
    to use 16 times samples per packet on the simulator (this can lead to awkwardly small
    packets in subscale cases).

##### Buffer sizes

  - `assembled_ringbuf_nsamples` (integer, default 8192).

    The "assembled" ring buffer sits between the assembler thread and the dedispersion threads.
    If the dedispersion threads are running slow, then this ring buffer will start to fill up.
    The assembled_ringbuf_nsamples parameter sets the size of the ring buffer, in time samples
    (i.e. milliseconds).

    Currently, if the assembled ring buffer fills up, then the L1 server throws an exception.
    This behavior will eventually be replaced with some recovery logic which resets the dedispersion	
    state.

  - `telescoping_ringbuf_nsamples` (list of integers, optional).

    After data is processed by the assembler thread, it goes to the "telescoping" ring buffer,
    which saves it for later retrieval by RPC's.

    As the name suggests, the buffer is telescoping, in the sense that data is saved at
    multiple levels of time downsampling.  For example, the most recent 60 seconds of 
    data might be saved at full time resolution (1 ms), the next 60 seconds of data
    might be saved at 2 ms resolution, and the next 180 seconds might be saved at 4 ms
    resolution.

    The telescoping_ringbuf_nsamples field is a list of integers, which defines how many
    samples (i.e. milliseconds) are saved at each level of downsampling.  The scheme described
    in the previous paragraph could be configured as follows:
    ```
    # Telescoping ring buffer configuration
    #      60 seconds at 1 ms time resolution
    #   +  60 seconds at 2 ms time resolution
    #   + 180 seconds at 4 ms time resolution

    telescoping_ringbuf_nsamples = [ 60000, 60000, 180000 ]
    ```
    Note that the elements of the telescoping_ringbuf_nsamples list are _non-downsampled_
    sample counts, i.e. the number "180000" above corresponds to (180000 * 1 ms), not
    (180000 * 4 ms).

    If the telescoping_ringbuf_nsamples field is absent (or an empty list) then the
    telescoping ring buffer is disabled, and the node will not save data for RPC-based
    retrieval.

    An implementation detail: the telescoping_ringbuf_nsamples will be rounded up to
    the nearest "assembled_chunk", where the assembled_chunk size is 1024, 2048, 4096...
    depending on the level of downsampling.


##### Parameters defining L1b linkage

  - `l1b_executable_filename` (string).

     Filename of the L1b executable.  The L1 server spawns
     L1b subprocesses using the following command line:
     ```
     <l1b_executable_filename> <l1b_config_filename> <beam_id>
     ```
     Here, l1b_config_filename is one of the command-line arguments when the L1 server is started
     (see [configuration file overview](#user-content-configuration-file-overview) above for the 
     L1 server command-line syntax).  The beam_id is an integer.

     When each L1b subprocess is spawned, its standard input will be connected to a unix pipe
     which will be used to send coarse-grained triggers from L1a to L1b.  The bonsai configuration
     information (e.g. dimensions of coarse-grained trigger) arrays will also be sent over the
     pipe, so there is no need for the bonsai_config_filename to appear on the L1b command line.

     Currently, the L1b process should be written in python, and the `bonsai.PipedDedisperser`
     python class should be used to receive coarse-grained triggers through the pipe.  The
     program `toy-l1b.py` is a toy example, which "processes" coarse-grained triggers by
     combining them into a waterfall plot.

     If l1b_executable_filename is an empty string, then L1b subprocesses will not be spawned.

  - `l1b_search_path` (boolean, default=false).

     If true, then duplicate the actions of the
     shell in searching for the l1b executable, by trying all colon-separated directories
     in the $PATH environnment variable.

  - `l1b_buffer_nsamples` (integer, default=0).

    Number of time samples which can be buffered between L1a and L1b.

    Under the hood, this gets converted to a buffer size in bytes, which is used to set
    the capacity of the unix pipe between L1 and L1b.  The default (0) means that a
    system default pipe capacity is used (usually 64 kbytes, which is uncomfortably small).

    It is important to note that setting the capacity of a pipe is a linux-only feature!
    Therefore, on other operating systems, setting l1b_buffer_nsamples=0 is the only option.
    In linux, there is also a maximum allowed capacity (/proc/sys/fs/pipe-max-size).  If
    you get a permissions failure when increasing pipe capacity, then you probably need to
    increase the maximum (as root).

    When the L1 server computes the pipe capacity from l1b_buffer_nsamples, the buffer size
    will be rounded up to a multiple of the bonsai chunk size ('nt_chunk' in the bonsai config 
    file), and a little bit of padding will be added for metadata.

  - `l1b_pipe_timeout` (floating-point, default=0).

    Timeout in seconds for the unix pipe connecting L1a and L1b.

    This parameter determines what happens when L1a tries to write to the pipe, and the pipe
    is full (presumably because L1b is running slow).  The dedispersion thread will sleep
    until the timeout expires, or the pipe becomes writeable, whichever comes first.  If a
    second write also fails because the pipe is full, then the L1 server will throw an
    exception.

    **Important:** the default parameters (l1b_buffer_nsamples = l1b_pipe_timeout = 0) are asking for trouble!  
    In this case, the L1 server will crash if the pipe fills for an instant, and the pipe capacity will be a system 
    default (usually 64 kbytes) which is probably too small to hold the coarse-grained triggers from a single bonsai 
    chunk.  In this situation, the L1 server can crash even in its normal mode of operation, in the middle of sending
    a chunk of data from L1a to L1b.

    One alternative to this "doomsday scenario", which we plan to use in the production L1 server, is to set l1b_pipe_timeout=0,
    but set l1b_buffer_nsamples to some reasonable value (say 4096, corresponding to a 4-second buffer).
    This solution is suitable for a real-time search, since it puts a hard limit on how far L1b can
    fall behind in its processing (4 seconds) before it is considered an error.  It only works on Linux,
    which is fine for the real L1 nodes, but can't be emulated on an osx laptop.

    Another alternative, which is the only alternative to the doomsday scenario on a non-linux machine,
    is to set l1b_pipe_timeout to some reasonable value (a few seconds).  In this configuration, there is
    no hard limit on how far L1b can fall behind cumulatively, as long as it calls PipedDedisperser.get_triggers()
    frequently enough.  This configuration is suitable for subscale testing, and is used in all the toy
    L1 config file examples.

  - `l1b_pipe_blocking` (boolean, default=false): if true, then L1a's writes to the pipe
    will be blocking.  This has the same effect as setting l1b_pipe_timeout to a huge value.

##### Miscellaneous parameters

  - `track_global_trigger_max` (boolean, default=false).

    If track_global_trigger_max=true, then the L1 server will keep track of the FRB with the
    highest signal-to-noise in each beam.  When the L1 server exits, it prints the signal-to-noise,
    arrival time and DM to the terminal.

    This was implemented for early debugging, and is a little superfluous now that L1b is integrated!

<a name="l0-config"></a>
### CONFIG FILE REFERENCE: L0 SIMULATOR

  - `nbeams` (integer).

    Number of beams that the 

  - `nupfreq` (integer).

  - `nfreq_coarse_per_packet` (integer).

  - `nt_per_packet` (integer, optional).
  
    Recommend omitting in subscale, specifying 16 in production-scale.

  - `nthreads` (integer).
  
  - `fpga_counts_per_sample` (integer).
  
  - `max_packet_size` (integer).
  
  - `gbps_per_stream` (integer).
  
  - `ipaddr` (integer).

  - `port` (integer).


<a name="rpc reference"></a>
### RPC REFERENCE

  - `get_statistics`

  - `write_chunks`
