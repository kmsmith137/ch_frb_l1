### TO DO

  - Testbed testing

     - Test dedispersion over network at full rate

     - Dedispersion + SSD

     - Dedispersion + NFS

     - Dedispersion + RPC

     - Multiples (e.g. dedispersion + SSD + NFS + RPC)

     - Test IPMI on the compute node.

     - Do some testing of NFS throughput, but this will have to wait until we have real file servers.

  - ch-frb-l1 

     - Decide how many RPC worker threads we need and what they should do.

     - Alex Josephy's L1B code is not yet integrated.

     - Distributed logging needs to be integrated in a lot of places.

     - We need more visualization and administration tools (our plans here are not defined very well).

  - ch_frb_io

     - Monitoring: extend "status" RPC to include fields which are important for
       monitoring performance, such as thread load fractions, and total allocated
       assembled chunks.  Need some bare-bones tool to send a status RPC every
       second and display result.

     - Agree with the collaboration on what the "heavyweight" RPC's are.
       Depending on the outcome of this discussion, we can make a plan and prioritize.

  - rf_pipelines

     - Needs some fairly large internal changes, to allow subchains at downsampled resolution
       and more general types of transforms.  This is the biggest loose end in L1!

     - RFI removal code is converging, but will continue to evolve.

  - bonsai

     - Serialization for coarse-grained trigger arrays (via msgpack?), so that we can pass them to 
       L1B processes over a local socket.

     - Coarse-grained trigger array ring buffer, retrievable by RPC.
