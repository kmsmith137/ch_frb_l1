### ch_frb_l1 

Toplevel repository for the CHIMEFRB L1 code, containing master executables, bonsai configs, etc.

The L1 code consists of the following git repositories.
Depending on what you're doing, you may only need a subset of these!
In particular, the modules marked "frb1 only" include hardcoded pathnames on
frb1.physics.mcgill.ca, and probably won't be useful on other machines.
For a dependency graph, see [doc/dependencies.png](./doc/dependencies.png).

  - [kiyo-masui/bitshuffle](https://github.com/kiyo-masui/bitshuffle):
    "bitwise" compression algorithm used throughout CHIME.
  - [kmsmith137/simd_helpers](https://github.com/kmsmith137/simd_helpers):
    header-only library for writing x86 assembly language kernels.
  - [kmsmith137/pyclops](https://github.com/kmsmith137/pyclops):
    some hacks for writing hybrid C++/python code.
  - [kmsmith137/rf_kernels](https://github.com/kmsmith137/rf_kernels):
    fast C++/assembly kernels for RFI removal and related tasks.
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
    (Note: this repo now includes the web viewer code which was previously
    in [mburhanpurkar/web_viewer](https://github.com/mburhanpurkar/web_viewer).)
  - [mrafieir/ch_frb_rfi](https://github.com/mrafieir/ch_frb_rfi):
    scritping framework for RFI removal and offline L1 analysis.  **(frb1 only)**
  - [kmsmith137/ch_frb_l1](https://github.com/kmsmith137/ch_frb_l1):
    toplevel repo, whose README you're reading right now.

**Warning:** one problem with our current build system is that it doesn't track dependencies
between repositories.  So for example, if you update bonsai and do `make install`, you 
should rebuild everything which depends on bonsai (for example rf_pipelines) from scratch.
Otherwise you can get unpredictable results, such as segfaults.

**Important:** In the previous paragraph, "rebuild from scratch" means `make clean; make all install`.
Rebuilding with `make all install` wouldn't be enough, since dependencies aren't tracked between repositories!

For this reason, it's easier to end up with a broken pipeline than you might think.  If you get into trouble,
the following instructions are a cut-and-paste "reset switch" which will rebuild the entire pipeline consistently
from scratch:

   - [Rebuilding the pipeline](./doc/rebuilding_pipeline.md)

For a manual-in-progress, see [MANUAL.md](./MANUAL.md).

For an "operations manual" explaining how to run the L1 server on-site, see [OPERATIONS_MANUAL.md](./OPERATIONS_MANUAL.md).
