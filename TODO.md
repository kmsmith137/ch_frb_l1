### Two-node to do list.

  - We'll try to demonstrate both 8-beam and 16-beam options, but we may end up
    only getting the 8-beam option working for now, and leaving the 16-beam option
    for the next few months

  - Simulation: Test whether current L0 simulator can write packets at full rate.
    (It's OK to replay a short loop.)  If not, then we'll need to improve this code.
    "Full rate" = 16 beams.

  - Monitoring: extend "status" RPC to include fields which are important for
    monitoring performance, such as thread load fractions, and total allocated
    assembled chunks.  Need some bare-bones tool to send a status RPC every
    second and display result.

  - Agree with the collaboration on what the "heavyweight" RPC's are.
    Depending on the outcome of this discussion, we can make a plan and prioritize.
  
  - In the meantime, we can implement one heavyweight RPC, for example writing
    an assembled to disk, and test NFS performance.

  - Test write performance of the SSD, make sure it can keep up with real-time.

  - Forming the slow pulsar timestream and streaming it to NFS.

  - Assembly language kernelizing the RFI excision and detrending transforms.

  - Improving the bonsai code, so that in particular the output is in "sigmas".

  - rf_pipelines internal changes, to allow efficient transform chains at downsampled resolution.

  - Write code to 16K-upchannelize data.

  - Make a plan for collecting more data of various types.

  - Propose RFI excision code.
    
  - Touch base with L2 group on division of web-based code.

  - Vaguely defined: develop rf_weblines a little bit more, as needed
    for tuning our RFI excision.

  - Optional: demonstrate a "semi-debug" mode where we stream data to the SSD
    and simultaneously dedisperse in real time.

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

### 128-node priority

  - Slow pulsar search related RPC's and master search code.

  - Use SSD as destination for writes if NFS server is not available.

  - What to do about bright RFI removal?
