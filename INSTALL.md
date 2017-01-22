Here are some toplevel instructions for building the CHIMEFRB pipeline.

Warning: the following instructions may be out-of-date or incomplete.
If you run into problems or have suggestions, let me know!

### INSTALLING EXTERNAL DEPENDENCIES

- Python-related things which can be installed painlessly with `pip`:
  ```
  pip install numpy
  pip install scipy
  pip install matplotlib
  pip install Pillow
  pip install h5py
  pip install Cython
  ```
  To install without root privileges, do `pip install --user`.
  To upgrade a previous pip install, do `pip install --upgrade`.

  You might need to upgrade your cython, since bonsai currently requires a very
  recent cython (I know that cython 0.24 works, and cython 0.20 is too old).

- libhdf5.  I had to build this from source, since bitshuffle requires 1.8.11
  or later, and the package managers (yum, brew) wanted to install an earlier
  version.  The following worked for me (assuming no root privileges):
  ```
  wget https://support.hdfgroup.org/ftp/HDF5/current/src/hdf5-1.8.17.tar.gz
  tar zxvf hdf5-1.8.17.tar.gz
  cd hdf5-1.8.17
  ./configure --prefix=$HOME
  make
  make install
  ```

- lz4.  On CentOS this is a one-liner: `sudo yum install lz4-devel`.
  (TODO: osx install instructions here.)

- msgpack.  On CentOS this is a one-liner: `sudo yum install cppzmq-devel.x86_64`.
  (TODO: osx install instructions here.)

- jsoncpp (https://github.com/open-source-parsers/jsoncpp)

  On osx, you can probably install with: `brew install jsoncpp`

  In linux, you can probably install with `yum install jsoncpp-devel`

  Building jsoncpp from scratch is a pain, but the following procedure worked for me:
  ```
  git clone https://github.com/open-source-parsers/jsoncpp
  mkdir -p build/debug
  cd build/debug
  cmake -DCMAKE_INSTALL_PREFIX:PATH=$HOME -DCMAKE_CXX_FLAGS=-fPIC -DCMAKE_C_FLAGS=-fPIC -DCMAKE_BUILD_TYPE=debug -G "Unix Makefiles" ../..
  make install
  ```

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

   - https://github.com/kmsmith137/simd_helpers
   - https://github.com/kmsmith137/simpulse
   - https://github.com/CHIMEFRB/ch_frb_io
   - https://github.com/CHIMEFRB/bonsai
   - https://github.com/kmsmith137/rf_pipelines
   - https://github.com/kmsmith137/ch_frb_l1 (whose INSTALL.md you're reading right now)

They use a klunky build procedure which we should improve some day!

For each package, in the order above, do the following:

   - You'll need to create a file Makefile.local in the toplevel directory which defines
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

  - The rf_pipelines package has optional dependencies on bonsai and ch_frb_io which you'll want to enable.
    There is also an optional dependency on psrfits which isn't important for CHIMEFRB.
    Thus your Makefile.local should contain the lines
    ```
    HAVE_BONSAI=y
    HAVE_CH_FRB_IO=y
    HAVE_PSRFITS=n
    ```

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
