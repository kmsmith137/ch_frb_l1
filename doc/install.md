### INSTALLATION INSTRUCTIONS FOR L1 PIPELINE

Here are the "long-form" instructions for compiling the CHIMEFRB L1 code.

If you're compiling on one of the CHIME machines, then you can use the
"quick-start" installation instructions instead:

  - [Installing from scratch on frb1.physics.mcgill.ca](./quick_install_frb1.md)
  - [Installing from scratch on an frb-compute-X compute node](./quick_install_l1_node.md)

Warning: the following instructions may be out-of-date or incomplete.
If you run into problems or have suggestions, let me know!

### LIST OF EXTERNAL DEPENDENCIES

  - A gcc which is recent enough that C++11 and AVX2 are supported.  I know that gcc 4.8.1 works, and that 4.4.7 is too old.
  - A very recent cython.  I know that cython 0.24 works, and cython 0.20 is too old.  (Edit: this used to be true, but I
    suspect older versions of cython will work now.)
  - numpy/scipy/matplotlib
  - hdf5 version 1.8.11 or later, **but not version 1.10.x**
  - h5py version 2.4.0 or later
  - FFTW3 (http://fftw.org)
  - The 'PIL' python imaging library.  If you need to install it, I recommend the Pillow variant ('pip install Pillow')
  - lz4
  - msgpack
  - zeromq
  - cppzmq (C++ bindings for zeromq, header-only, https://github.com/zeromq)
  - pyzmq
  - jsoncpp
  - yaml-cpp

### INSTALLING EXTERNAL DEPENDENCIES

- Python-related things which can be installed painlessly with `pip`:
  ```
  pip install numpy
  pip install scipy
  pip install matplotlib
  pip install Pillow
  pip install h5py
  pip install Cython
  pip install zmq
  pip install msgpack-python
  pip install pyyaml
  ```
  To install without root privileges, do `pip install --user`.
  To upgrade a previous pip install, do `pip install --upgrade`.

  You might need to upgrade your cython, since bonsai currently requires a very
  recent cython (I know that cython 0.24 works, and cython 0.20 is too old).

- libhdf5. **Currently, the pipeline requires HDF5 version 1.8.11 or later,
  but does not work with version 1.10.x.  This will be fixed eventually!**

  Given these version constraints, I needed to build libhdf5 from source (note that
  yum and homebrew want to install a version which is too old).  The following 
  worked for me (assuming no root privileges):
  ```
  wget https://support.hdfgroup.org/ftp/HDF5/current18/src/hdf5-1.8.19.tar.gz
  tar zxvf hdf5-1.8.19.tar.gz
  cd hdf5-1.8.19
  ./configure --prefix=$HOME --enable-cxx
  make
  make install
  ```

- lz4.  

   - osx one-liner: `brew install lz4`

   - centos one-liner: `sudo yum install lz4-devel`

   - ubuntu one-liner: `sudo apt-get install liblz4-dev`

- msgpack.  

   - osx one-liner: `brew install msgpack`

   - centos one-liner: `sudo yum install msgpack-devel.x86_64`

   - ubuntu: don't install the apt-get version, it is too old!  (e.g. it is missing /usr/include/msgpack/fbuffer.hpp)

   - Building from scratch.  This is easy because we only use the msgpack headers, not the compiled library.
       
     Kendrick's procedure:
     ```
     git clone https://github.com/msgpack/msgpack-c
     sudo cp -r msgpack-c/include/* /usr/local/include
     ```

     Dustin's procedure: download the source package from, eg,
     https://github.com/msgpack/msgpack-c/releases/download/cpp-2.1.0/msgpack-2.1.0.tar.gz
     and then extract it and add the "msgpack-2.1.0/include" into the
     include path.

- zeromq and cppzmq.  

   - centos one-liner: `sudo yum install cppzmq-devel.x86_64`

   - ubuntu: don't use the apt-get packages, they are too old!.  You'll need to build both zeromq and cppzmq from scratch, see below.

   - osx: zeromq can be installed with `brew install zeromq`, but you'll need to build cppzmq from scratch, see below.
   
   - Building zmq from scratch: download from zeromq.org, and then do:
     ```
     ./configure --prefix=/usr/local
     make
     sudo make install
     ```

   - Building cppzmq from scratch: since it's a header-only library with two source files, I just ignored the build system and did:
     ```
     git clone https://github.com/zeromq/cppzmq.git
     cd cppzmq
     sudo cp zmq.hpp zmq_addon.hpp /usr/local/include
     ```

- jsoncpp (https://github.com/open-source-parsers/jsoncpp)

    - osx one-liner: `brew install jsoncpp`

    - centos one-liner: `sudo yum install jsoncpp-devel`

    - ubuntu one-liner: `sudo apt-get install libjsoncpp-dev`

    - Building jsoncpp from scratch is a pain, but the following procedure worked for me:
      ```
      git clone https://github.com/open-source-parsers/jsoncpp
      mkdir -p build/debug
      cd build/debug
      cmake -DCMAKE_INSTALL_PREFIX:PATH=$HOME -DCMAKE_CXX_FLAGS=-fPIC -DCMAKE_C_FLAGS=-fPIC -DCMAKE_BUILD_TYPE=debug -G "Unix Makefiles" ../..
      make install
      ```

 - yaml-cpp (https://github.com/jbeder/yaml-cpp)

    - osx one-liner: `brew install yaml-cpp`.

    - centos one-liner: `sudo yum install yaml-cpp-devel`.

    - ubuntu two-liner: 
      ```
      sudo apt-get install libboost-all-dev    # overkill?
      sudo apt-get install libyaml-cpp-dev
      ```
      Note: if only libyaml-cpp-dev is installed, then some necessary boost libraries will be missing.
      Installing libboost-all-dev fixes this, but also installs around 200MB of software!  I didn't
      bother trying to figure out exactly which boost libraries were needed.

### INSTALLING BITSHUFFLE (optional but recommended)

  Clone this repo: https://github.com/kiyo-masui/bitshuffle

  You'll need this if you want to read or write bitshuffle-compressed files with ch_frb_io
  (note that CHIME pathfinder data is generally bitshuffle-compresed).

  The following recipe worked for me:
  ```
  git clone https://github.com/kiyo-masui/bitshuffle.git
  cd bitshuffle/

  # The HDF5 library can dynamically load the bitshuffle plugin, i.e. you don't need
  # to link the bitshuffle library when you compile ch_frb_io, but you need to set this
  # environment variable to tell libhdf5 where to look.  Suggest adding this to .bashrc!

  export HDF5_PLUGIN_PATH=$HOME/lib/hdf5_plugins

  # If you have root privileges and want to install "system-wide", omit the --user flag
  # The --h5plugin* flags will build/install the plugin needed to use bitshuffle from C++

  python setup.py install --user --h5plugin --h5plugin-dir=$HOME/lib/hdf5_plugins
  ```
  If you run into trouble, you'll want to refer to the installation instructions in the bitshuffle repo.


### INSTALLING THE CORE PIPELINE PACKAGES

These instructions apply to the following github repos:

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
  - [mrafieir/ch_frb_rfi](https://github.com/mrafieir/ch_frb_rfi):
    scritping framework for RFI removal and offline L1 analysis.  **(frb1 only)**
  - [kmsmith137/ch_frb_l1](https://github.com/kmsmith137/ch_frb_l1):
    toplevel repo, whose README you're reading right now.

They use a klunky build procedure which we should improve some day!
Roughly, it works like this.  For each package, in the order above,
you'll need to do the following:

   - Create a file Makefile.local in the toplevel directory which defines
     a bunch of machine-dependent variables, such as compiler flags, install directories,
     and boolean flags indicating which optional dependencies are available.  

     The variables which need to be defined are slightly different for each of the 
     packages above, and are listed in the Makefile.  However, it's easiest to
     start with one of the template Makefile.locals in the `site/` subdirectory of
     the toplevel directory, and either modify it, or just copy/symlink it to the
     toplevel directory if it doesn't need modification.
     
   - Type 'make all install'

   - Some of these packages have unit tests which you may want to run; see the 
     per-package README file for details.

Some more notes on writing Makefile.local files:

  - The bonsai package has an optional dependency on libpng which you'll want to enable for CHIMEFRB.
    Therefore, your Makefile.local should contain the line
    ```
    HAVE_PNG=y
    ```

  - The rf_pipelines package has the following optional dependencies which you'll want to enable.
    ```
    HAVE_BONSAI=y
    HAVE_CH_FRB_IO=y
    HAVE_SIMPULSE=y
    HAVE_HDF5=y
    HAVE_PNG=y
    ```
    (There is also an optional dependency on psrfits which isn't important for CHIMEFRB.)

  - Some of the packages need to include header files from your python installation.
    This is the case if the example Makefile.locals contain lines like these:
    ```
    # This directory should contain e.g. Python.h
    PYTHON_INCDIR=/usr/include/python2.7

    # This directory should contain e.g. numpy/arrayobject.h
    NUMPY_INCDIR=/usr/lib64/python2.7/site-packages/numpy/core/include

    CPP=g++ -I$(PYTHON_INCDIR) -I$(NUMPY_INCDIR) ...
    ```
    It's important that these directories correspond to the versions of python/numpy
    that you're actually using!  (There may some confusion if more than one python interpreter
    is installed on your machine.)  The safest thing to do is to determine these directions
    from within the python interpreter itself, as follows:
    ```
    import distutils.sysconfig
    print distutils.sysconfig.get_python_inc()   # prints PYTHON_INCDIR

    import numpy
    print numpy.get_include()    # prints NUMPY_INCDIR
    ```

  - Each package also defines some installation directories, e.g. Makefile.local will contain something like this:
    ```
    # Directory where executables will be installed
    BINDIR=$(HOME)/bin

    # Directory where C++ libraries will be installed
    LIBDIR=$(HOME)/lib

    # Directory where C++ header files will be installed
    INCDIR=$(HOME)/include

    # Directory where python and cython modules will be installed
    PYDIR=$(HOME)/lib/python2.7/site-packages
    ```
    You'll want to make sure that your PATH, PYTHONPATH, and LD_LIBRARY_PATH environment variables
    contain the BINDIR, PYDIR, and LIBDIR directories from the Makefile.local.  For example, given the
    Makefile.local above, your `$HOME/.bashrc` should contain something like this:
    ```
    export PATH=$HOME/bin:$PATH
    export PYTHONPATH=$HOME/lib/python2.7/site-packages:$PYTHONPATH
    export LD_LIBRARY_PATH=$HOME/lib:$LD_LIBRARY_PATH
    ```
    (Note: on osx, you should use DYLD_LIBRARY_PATH environment variable instead of LD_LIBRARY_PATH.)
