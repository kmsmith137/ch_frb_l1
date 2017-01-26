### Two-node to do list.

  - Plan

     - Test dedispersion over network at full rate

     - Dedispersion + SSD

     - Dedispersion + NFS

     - Dedispersion + RPC

     - Multiples (e.g. dedispersion + SSD + NFS + RPC)

     - Test IPMI on the compute node.

  - L0 simulator

     - Should send uniform RNG's.  [ Dustin ]

     - Tie FPGA counts to system time.  [ Kendrick ]
     
     - Work on ch-frb-simulate-l0 executable, and script to start 8 processes
       with appropriate bindings to network interfaces.  [ Dustin ]

  - ch-frb-l1 executable

     - Add RPC server.  [ Dustin ]
     
     - Add RFI removal and dedispersion. [ Kendrick ]

     - Work on ch-frb-l1 executable, and script to start 2 processes
       with appropriate bindings to network interfaces and cores.  [ Dustin ]

     - Either implement two network threads talking to one assembler thread,
       or modify the network thread to read from two sockets.
       (Alterative: link bonding?)  [ Kendrick ]

     - Decide how many RPC worker threads we need and what they should do.  [ Dustin ]

  - Optional

     - Monitoring: extend "status" RPC to include fields which are important for
       monitoring performance, such as thread load fractions, and total allocated
       assembled chunks.  Need some bare-bones tool to send a status RPC every
       second and display result.  [ Dustin ]

     - Agree with the collaboration on what the "heavyweight" RPC's are.
       Depending on the outcome of this discussion, we can make a plan and prioritize.

- In progress

  - Improving the bonsai code, so that in particular the output is in "sigmas".

  - rf_pipelines internal changes, to allow efficient transform chains at downsampled resolution.

  - Write code to 16K-upchannelize data.

  - Make a plan for collecting more data of various types.

  - Propose RFI excision code.
    
  - Touch base with L2 group on division of web-based code.

  - Vaguely defined: develop rf_weblines a little bit more, as needed
    for tuning our RFI excision.

### Eight-node priority

  - Figure out whether eight nodes can write to a single NFS server efficiently.
    If not, then we have some work to do!

  - Figure out exactly how we're integrating our status RPC with Prometheus.

  - Figure out how we're doing logging.

  - Telescoping ring buffer.

  - Bonsai RPC's:

      - C++ server-side RPC's for retrieving ring-buffered coarse-grained triggers.

      - C++ client-side RPC's for sending a stream of coarse-grained triggers to the L1B process.

      - Python server-side RPC's for receiving coarse-grained triggers at L1B.
  
  - Polished version of rf_weblines that can be used over the internet.

  - Forming the slow pulsar timestream and streaming it to NFS.

### 128-node priority

  - Slow pulsar search related RPC's and master search code.

  - Use SSD as destination for writes if NFS server is not available.

  - What to do about bright RFI removal?
