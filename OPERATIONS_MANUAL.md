### OPERATIONS_MANUAL.md (ch_frb_l1)

The OPERATIONS_MANUAL.md contains instructions for running the L1 server on-site,
in a "cookbook" format with production-scale examples.

See [MANUAL.md](./MANUAL.md) for a systematic tutorial/reference with self-contained examples
and installation instructions.

### CONTENTS

  - [The DRAO backend](#user-content-drao-backend)
  - [Starting the L1 server](#user-content-starting-the-l1-server)
  - [Web services](#user-content-web-services)
  - [Analyzing acquisitions](#user-content-analyzing-acquisitions)
  - [Cookbook of miscellaneous tasks](#user-content-cookbook-of-miscellaneous-tasks)

<a name="drao-backend"></a>
### The DRAO backend

- Gateway machines: `tubular.drao.nrc.ca` and `liberty.drao.nrc.ca`.
  All ssh connections to the outside world go through one of these machines!

- L1 compute nodes: `cf0g0`, `cf0g1`, ..., `cf0g7`.  These machines are diskless and
  share a root filesystem.  The root filesystem is mounted read-write on `cf0g0` and
  mounted read-only on the other nodes.  Therefore, to make changes (such as recompiling,
  editing a config file, etc.) you should use `cf0g0`, and the changes will automatically
  appear on the other nodes.

  There is also an SSD (actually two SSD's in RAID-0) in each compute node, mounted
  at `/local`.

- L1 head node: `cf0hn`.  This exports (via NFS) the root filesystem to the compute nodes,
  and is used for a few other things, like rebooting the nodes (see "Cookbook of miscelleanous
  tasks" below).

- L4 node: `cf0g9`.  Our web services run here, and can be accessed from outside DRAO
  using ad hoc ssh tunnels.  See [Web services](#user-content-web-services) below.

- NFS server: `cf0fs`.  A central machine where L1 nodes can write data (although currently
  we usually use the local SSD's in the L1 nodes instead of the NFS server).

  Right now our NFS server is a pulsar node which has been retrofitted with a few HDD's.
  It may be a little underpowered, but we have a much larger NFS server coming soon.

- My `.ssh/config` looks something like [this](./doc/ssh_config.txt).

<a name="starting-the-l1-server"></a>
### Starting the L1 server

- Right now, you have to start an ssh session by hand on every node, and you also need to use a different
  L1 yaml file on every node!  This will be improved soon.  If you want to start a long-running
  server instance, the `screen` (or `tmux`) utility is useful.

- I suggest that everyone use their own user account for running the L1 server.  This is convenient
  during debugging/commissioning, where we all want to be on different git branches.  When we transition 
  into an "operations" mode, we can switch to using a dedicated account for the L1 server.

- The examples below run a placeholder version of the L1B code which just prints messages as it receives
  coarse-grained triggers.  (It it also supposed to make a trigger plot, but this doesn't work in
  production yet, since the production L1 server has no way of shutting down gracefully.)

- **Example 1**: 16 beams, running the L1 server in "toy" mode.  "Toy" mode means that the server
  does not do RFI removal or dedisperse the data.  It does assemble packets, maintain a telescoping
  ring buffer for triggered NFS writes, and (optionally) stream incoming data to its local SSD.

  First, I suggest verfiying that the correlator is sending data (`sudo iftop`)
  and verify that nothing else is running on the node (`htop`).
  Then, launch the L1 server with:
  ```
  # To see what all the ch-frb-l1 command-line arguments do, type `ch-frb-l1` with no arguments!
  # This starts the L1 server on node 0.  For e.g. node 1, replace cf0g0 by cf0g1, and 
  # replace l1_production_16beam_0.yaml by l1_production_16beam_1.yaml.
  
  ssh cf0g0
  cd git/ch_frb_l1
  ./ch-frb-l1 -tv l1_configs/l1_production_16beam_0.yaml
  ```
  After ~90 seconds or so, the server should start processing data.  At this point, you
  may want to check out the "Dustin webapp" for live L1 monitoring, see [Web services](#user-content-web-services)
  below.

  To stop the server, press control-C in the terminal window where the server is running.
  This will be improved soon, by implementing an RPC-triggered "graceful exit"!

- **Example 1b**: Same as example 1, but streaming incoming data to the node's local SSD.

  To enable streaming, you need to hand-edit all the l1 yaml files.
  At the bottom of the file, you'll see some streaming-related fields.
  You should uncomment the line:
  ```
  stream_acqname: "test_acq"
  ```
  possibly replacing `test_acq` by a different acqname.
  (Unfortunately you need to hand-edit all 8 configuration files!  We'll improve this soon.)

  After making this change, you need to restart the L1 server.  It should start streaming
  data to its local SSD.  A good way to confirm this is to monitor disk usage with `df`.

  The server will fill its local disk in 2 hours (if initially empty), so you should stop
  it before this with control-C.  I usually capture at least 20 minutes of data, since bonsai
  currently has a "slow start" problem (see MANUAL.md for more info) and has trouble analyzing
  the first 10 minutes of data.
  
  Congratulations, you now have an acquisition!  You may now want to postprocess it with our
  offline pipeline.  For info on how to do this,
  see [Analyzing acquisitions](#user-content-analyzing-acquisitions) below.

I don't recommend trying examples 2 and 3 just yet!
Instead I suggest just running the server in its "toy" mode, following example 1,
and postprocessing the captured data with the offline pipeline.

- **Example 2**: 16 beams, with "placeholder" RFI removal (detrenders but no clippers), and dedispersion
  using the least optimal settings (no spectral index search, no low-DM upsampled tree).  This won't work
  on real data, since we don't remove RFI!
  ```
  # This starts the L1 server on node 0.  For e.g. node 1, replace cf0g0 by cf0g1, and 
  # replace l1_production_16beam_0.yaml by l1_production_16beam_1.yaml.
  ssh cf0g0
  cd git/ch_frb_l1
  ./ch-frb-l1 l1_configs/l1_production_16beam_0.yaml rfi_configs/rfi_placeholder.json /data/bonsai_configs/bonsai_production_noups_nbeta1_v2.hdf5 l1b_placeholder
  ```

- **Example 3**: 8 beams, with real RFI removal (provisional), and dedispersion with the most optimal settings
  (spectral index search, low-DM upsampled tree).
  ```
  # This starts the L1 server on node 0.  For e.g. node 1, replace cf0g0 by cf0g1, and 
  # replace l1_production_16beam_0.yaml by l1_production_16beam_1.yaml.
  ssh cf0g0
  cd git/ch_frb_l1
  ./ch-frb-l1 l1_configs/l1_production_8beam_0.yaml rfi_configs/rfi_production_v1.json /data/bonsai_configs/bonsai_production_ups_nbeta2_v2.hdf5 l1b_placeholder
  ```

<a name="web-services"></a>
### Web services

- Using the Dustin "webapp", for live L1 monitoring and control.

  There is a persistent instance of the webapp maintained by Dustin, which runs at port 5002 on cf0g9.
  Since this is behind the DRAO firewall, you'll need to create an ad hoc ssh tunnel.
  In a terminal window on your laptop, do:
  ```
  ssh -L 5002:cf0g9:5002 tubular
  ```
  Leave this window open!  (If you close the ssh session in the window, then the tunnel will terminate.)
  Then point your browser to `localhost:5002` and the webapp should appear.  If this doesn't work, let Dustin know!
  
  If for some reason you want to run your own instance of the webapp, you can launch it as follows:
  ```
  ssh cf0g9
  cd git/ch_frb_l1
  ./run-webapp.sh l1_configs/l1_production_16beam_webapp.yaml PORT
  ```
  where PORT is a TCP port number which doesn't conflict with any our existing services (see "Port number assignments" below).
  Then follow the instructions in the previous paragraph with 5002 replaced by PORT.

- Using the Maya "web viewer", for viewing results of our offline analysis pipeline.

  There is a persistent instance of the web viewer maintained by Kendrick, which runs at port 5003 on cf0g9.
  The instructions are the same as the "Dustin webapp" case above, with 5002 replaced by 5003.
  (I.e. `ssh -L 5003:cf0g9:5003 tubular`, then point browser at `localhost:5003`.)

  If for some reason you want to run your own instance of the web viewer, you can launch it as follows:
  ```
  ssh cf0g9
  cd git/rf_pipelines/web_viewer
  ./run-web-viewer.sh /frb-archiver-1/web_viewer PORT
  ```
  where PORT is a TCP port number which doesn't conflict with any our existing services (see "Port number assignments" below).
  Then view it through the ssh tunnel as usual (`ssh -L PORT:cf0g9:PORT tubular`, then point browser at `localhost:PORT`

- Port number assignments (placeholder section for now, to be fleshed out later)
  ```
  5001 = Michelle L4
  5002 = Dustin L1 "web app"
  5003 = Maya L1 "web viewer"
  5004 = Alex coarse-grained trigger viewer

  5100-5109 = Davor
  5110-5119 = Michelle
  5120-5129 = Dustin
  5130-5139 = Kendrick
  5140-5149 = Alex
  5150-5159 = Utkarsh

  5555 = L1 RPC  
  UDP 1313 = used by correlator to send real data packets
  UDP 6677 = used for simulated data packets in examples in MANUAL.md

  9090 = Prometheus
  9100 = node_exporter
  9116 = snmp_exporter
  9093 = alertmanager
  ```

<a name="analyzing-acquisitions"></a>
### Analyzing acquisitions

After acquiring an acqusition (see "example 1b" above), you can run our "offline pipeline"
to dedisperse, make web-browsable plots, etc.

For now, this section just contains hints for getting started.
There is a lot more to say here!

- Make sure to clone the
  [mrafieir/ch_frb_rfi](https://github.com/mrafieir/ch_frb_rfi) repo,
  which isn't needed to run the real-time search, but is needed for
  offline analysis and prototyping new RFI removal.

- With our current scripting framework, each node can only analyze
  the data stored on its own local SSD.  The offline pipeline uses
  a lot of CPU and RAM, so you can't run it at the same time as an
  L1 server instance!  (Use `htop` to check`.)

- After acquiring a new acquisition, you should run the command:
  ```
  ch-frb-make-acq-inventory ssd
  ```
  which will populate `/local/$(USER)/acq_json` with json files
  corresponding to the acquisitions on the node.

  Note that this won't work if the SSD is 100% full!  If this situation
  arises, you'll need to delete a few acqusition data files by hand to 
  make room.

- You can then use the `rfp-run` command in multithreaded mode (`-t NTHREADS`)
  to run instances of the offline pipeline.  An rfp-run invocation
  looks something like this:
  ```
  # Use 20 threads, since the L1 nodes have 20 cores.
  # The -W flag makes plots for the web viewer.
  rfp-run -W -t 20 <acq_file.json> <rfi_file.json> <bonsai_file.json>
  ```
  where any of the three json files can be a "run-list" pointing to multiple
  json files which are looped over.

     - The `acq_file.json` argument can be any of the json files produced 
       by the `ch-frb-make-acq-inventory` script.
     - The `rfi_file.json` argument can be any of the json files in `git/ch_frb_l1/rfi_configs`
       or `git/ch_frb_rfi/json_files/rfi_16k`.
     - The `bonsai_file.json` argument can be any of the json files
       in `git/ch_frb_rfi/json_files/bonsai_16k`.

  Here is an example rfp-run invocation which analyzes all acquisitions on the node's SSD.
  (This assumes that `~/git/ch_frb_l1` and `~/git/ch_frb_rfi` contain the appropriate repos!)
  ```
  rfp-run -W -t 20 /local/acq_json/$(USER)/master_runlist.json ~/git/ch_frb_l1/rfi_configs/rfi_placeholder.json ~/git/ch_frb_rfi/json_files/bonsai_16k/bonsai_production_noups_nbeta1_v2.json
  ```
  This may take a while to run, depending on how much data has been captured!

- Here is the most fun part!  After this finishes, you should be able to use the 
  "Maya web viewer" to inspect the result of the run.  See [Web services](#user-content-web-services)
  above for instructions.

- Some interesting variants on the above `rfp-run` invocation.

    - Replace `rfi_placeholder.json` by `rfi_production_v1.json`
      to get real RFI removal instead of "placeholder" (= detrending
      but no actual masking) RFI removal.  Or use tools in `ch_frb_rfi`
      to make your own RFI removal strategy and serialize it to a json file!

    - Replace `master_runlist.json` by another json file in the
      `/local/$(USER)/acq_json` tree, to analyze specific acqusitions,
      rather than analyzing everything on the SSD.

- To delete an acquisition, just `rm -rf` the appropriate subdirectory of `/local/acq_data`,
  then rerun `ch-frb-make-acq-inventory` to bring the json files in sync with the current
  contents of the SSD.

- This discussion just scratches the surface of how to use the offline pipeline.
  For a lot more info, see [rf_pipelines/MANUAL.md](https://github.com/kmsmith137/rf_pipelines/blob/master/MANUAL.md)
  or [ch_frb_rfi/MANUAL.md](https://github.com/mrafieir/ch_frb_rfi/blob/master/MANUAL.md).


<a name="cookbook-of-miscellaneous-tasks"></a>
### Cookbook of miscellaneous tasks

Everyone should feel free to add to this list!

- Checking disk usage: `df -h`
- Inspecting network traffic in/out of node: `iftop`
- How to figure out which process is listening on a given TCP or UDP port: `sudo netstat -plntu4 | grep PORT`

- Shutting down the L1 server.  Currently the only way to do this is to
  press control-C in the ssh window running the L1 server instance!

- Rebooting an L1 node
  ```
  # ssh to head node
  ssh cf0hn

  # This reboots node 0.  To reboot e.g. node 1, replace cf0i0 by cf0i1.
  ipmitool -I lanplus -H cf0i0 -U ADMIN -P ADMIN power reset
  ```
