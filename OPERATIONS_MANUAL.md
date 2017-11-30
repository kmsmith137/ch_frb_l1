### OPERATIONS_MANUAL.md (ch_frb_l1)

The OPERATIONS_MANUAL.md contains instructions for running the L1 server on-site,
in a "cookbook" format with production-scale examples.

See [MANUAL.md](./MANUAL.md) for a systematic tutorial/reference with self-contained examples
and installation instructions.

### CONTENTS

  - [Starting the L1 server](#user-content-starting-the-l1-server)
  - [Cookbook of miscellaneous tasks](#user-content-cookbook-of-miscellaneous-tasks)

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
  ```
  # To see what all the ch-frb-l1 command-line arguments do, type `ch-frb-l1` with no arguments!
  # This starts the L1 server on node 0.  For e.g. node 1, replace cf0g0 by cf0g1, and 
  # replace l1_production_16beam_0.yaml by l1_production_16beam_1.yaml.
  
  ssh cf0g0
  cd git/ch_frb_l1
  ./ch-frb-l1 -tv l1_configs/l1_production_16beam_0.yaml
  ```
  By default, this will not stream incoming data to the local SSD.  To enable streaming,

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

### Starting and accessing web services

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
  ```

<a name="cookbook-of-miscellaneous-tasks"></a>
### Cookbook of miscellaneous tasks

Everyone should feel free to add to this list!

- Shutting down the L1 server.  Currently the only way to do this is to
  press control-C in the ssh window running the L1 server instance!

- Rebooting an L1 node
  ```
  # ssh to head node
  ssh cf0hn

  # This reboots node 0.  To reboot e.g. node 1, replace cf0i0 by cf0i1.
  ipmitool -I lanplus -H cf0i0 -U ADMIN -P ADMIN power reset
  ```

- Inspecting network traffic in/out of node: `iftop`
- How to figure out which process is listening on a given TCP or UDP port: `sudo netstat -plntu4 | grep PORT`
