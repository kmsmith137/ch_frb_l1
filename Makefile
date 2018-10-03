include Makefile.local

ifndef BINDIR
$(error Fatal: Makefile.local must define BINDIR variable)
endif

ifndef CPP
$(error Fatal: Makefile.local must define CPP variable)
endif

# CC is used to compile the only non-C++ source file, civetweb/civetweb.c
ifndef CC
$(error Fatal: Makefile.local must define CC variable)
endif

#
# About the RPC subsystem: we're using ZeroMQ for the messaging
# (sockets) and msgpack for the message encoding (wire format).  This
# allows clients to be written in Python and other languages as well
# as C++.
#
# ZeroMQ is available in homebrew;
#
#    brew install zeromq
#
# We use C++ bindings for zeromq, available in the separate project cppzqm:
#
#    git clone https://github.com/zeromq/cppzmq.git
#
# (it is a header-only library)
#
# For msgpack,
#
#    brew install msgpack
#
#

SCRIPTS := ch-frb-make-acq-inventory

# Is this the correct split into installed/non-installed?
INSTALLED_BINARIES := ch-frb-l1 ch-frb-simulate-l0
NON_INSTALLED_BINARIES := rpc-client test-l1-rpc sim-l0-set test-packet-rates

all: $(INSTALLED_BINARIES) $(NON_INSTALLED_BINARIES)

.PHONY: all install uninstall

INCFILES := ch_frb_l1.hpp l0-sim.hpp l1-rpc.hpp rpc.hpp

L1_OBJS := l1-rpc.o

# Append compile flags
CPP_CFLAGS ?=
CPP_CFLAGS += -I$(CPPZMQ_INC_DIR) -I$(MSGPACK_INC_DIR)

CIVET_OBJS := l1-prometheus.o civetweb/CivetServer.o civetweb/civetweb.o
CPP_CFLAGS += -Icivetweb

doc/dependencies.png: doc/dependencies.dot
	dot -Tpng $< -o $@

civetweb/civetweb.o: civetweb/civetweb.c
	$(CC) -Icivetweb -c -o $@ $<

%.o: %.cpp $(INCFILES)
	$(CPP) -c -o $@ $< $(CPP_CFLAGS)

rpc-client: rpc_client.o
	$(CPP) -o $@ $^ $(CPP_LFLAGS) -lch_frb_io -lzmq

ch-frb-l1: ch-frb-l1.o file_utils.o yaml_paramfile.o $(L1_OBJS) $(CIVET_OBJS)
	$(CPP) -o $@ $^ $(CPP_LFLAGS) -lrf_pipelines -lbonsai -lch_frb_io -lrf_kernels -lzmq -lyaml-cpp -ljsoncpp -ldl -lcurl

sim-l0-set: sim-l0-set.cpp l0-sim.cpp
	$(CPP) -o $@ $^ $(CPP_CFLAGS) $(CPP_LFLAGS) -lch_frb_io

ch-frb-simulate-l0: ch-frb-simulate-l0.o l0-sim.o file_utils.o yaml_paramfile.o
	$(CPP) -o $@ $^ $(CPP_CFLAGS) $(CPP_LFLAGS) -lch_frb_io -lyaml-cpp

ch-frb-test: ch-frb-test.cpp $(L1_OBJS)
	$(CPP) -o $@ $^ $(CPP_CFLAGS) $(CPP_LFLAGS) -lch_frb_io -lzmq -lhdf5

ch-frb-test-debug: ch-frb-test.cpp $(L1_OBJS) $(IO_OBJS)
	$(CPP) -o $@ $^ $(CPP_CFLAGS) $(CPP_LFLAGS) -lzmq -lhdf5 -llz4

test-l1-rpc: test-l1-rpc.cpp $(L1_OBJS) file_utils.o $(CIVET_OBJS)
	$(CPP) $(CPP_CFLAGS) $(CPP_LFLAGS) -o $@ $^ -lzmq -lhdf5 -llz4 -lch_frb_io -ldl

test-packet-rates: test-packet-rates.cpp $(L1_OBJS) file_utils.o $(CIVET_OBJS)
	$(CPP) $(CPP_CFLAGS) $(CPP_LFLAGS) -o $@ $^ -lzmq -lhdf5 -llz4 -lch_frb_io -ldl

clean:
	rm -f *.o *~ civetweb/*.o civetweb/*~ $(INSTALLED_BINARIES) $(NON_INSTALLED_BINARIES) terminus-l1 hdf5-stream

# Note that we clean up 'terminus-l1' (which has been phased out) in the targets below.

install: $(INSTALLED_BINARIES)
	mkdir -p $(BINDIR)
	rm -f $(BINDIR)/terminus-l1
	for f in $(SCRIPTS); do cp $$f $(BINDIR)/; done
	for f in $(INSTALLED_BINARIES); do cp $$f $(BINDIR)/; done

uninstall:
	for f in $(INSTALLED_BINARIES) $(SCRIPTS) terminus-l1; do rm -f $(BINDIR)/$$f; done


# These are files; don't apply implicit make rules
Makefile.local: ;
Makefile: ;
%.cpp: ;
%.hpp: ;

# Cancel stupid implicit rules.
%: %,v
%: RCS/%,v
%: RCS/%
%: s.%
%: SCCS/s.%
