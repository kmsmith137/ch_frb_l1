include Makefile.local

ifndef BINDIR
$(error Fatal: Makefile.local must define BINDIR variable)
endif

ifndef CPP
$(error Fatal: Makefile.local must define CPP variable)
endif

ifndef CC_PYMODULE
$(error Fatal: Makefile.local must define CC_PYMODULE variable)
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

BINARIES := ch-frb-l1 ch-frb-simulate-l0 rpc-client test-l1-rpc sim-l0-set hdf5-stream terminus-l1

all: $(BINARIES)
.PHONY: all

INCFILES := ch_frb_l1.hpp l0-sim.hpp l1-parts.hpp l1-rpc.hpp rpc.hpp

L1_OBJS := l1-rpc.o

# Append compile flags
CPP_CFLAGS ?=
CPP_CFLAGS += -I$(CPPZMQ_INC_DIR) -I$(MSGPACK_INC_DIR)

dependencies.png: dependencies.dot
	dot -Tpng $< -o $@

%.o: %.cpp $(INCFILES)
	$(CPP) -c -o $@ $< $(CPP_CFLAGS)

rpc-client: rpc_client.o
	$(CPP) -o $@ $^ $(CPP_LFLAGS) -lch_frb_io -lzmq

ch-frb-l1: ch-frb-l1.o yaml_paramfile.o $(L1_OBJS)
	$(CPP) -o $@ $^ $(CPP_LFLAGS) -lrf_pipelines -lbonsai -lch_frb_io -lzmq -lyaml-cpp -ljsoncpp

sim-l0-set: sim-l0-set.cpp l0-sim.cpp
	$(CPP) -o $@ $^ $(CPP_CFLAGS) $(CPP_LFLAGS) -lch_frb_io

ch-frb-simulate-l0: ch-frb-simulate-l0.o l0-sim.o yaml_paramfile.o
	$(CPP) -o $@ $^ $(CPP_CFLAGS) $(CPP_LFLAGS) -lch_frb_io -lyaml-cpp

ch-frb-test: ch-frb-test.cpp $(L1_OBJS)
	$(CPP) -o $@ $^ $(CPP_CFLAGS) $(CPP_LFLAGS) -lch_frb_io -lzmq -lhdf5

ch-frb-test-debug: ch-frb-test.cpp $(L1_OBJS) $(IO_OBJS)
	$(CPP) -o $@ $^ $(CPP_CFLAGS) $(CPP_LFLAGS) -lzmq -lhdf5 -llz4

test-l1-rpc: test-l1-rpc.cpp $(L1_OBJS)
	$(CPP) $(CPP_CFLAGS) $(CPP_LFLAGS) -o $@ $^ -lzmq -lhdf5 -llz4 -lch_frb_io

hdf5-stream: hdf5-stream.cpp
	$(CPP) $(CPP_CFLAGS) $(CPP_LFLAGS) -o $@ $< -lrf_pipelines -lch_frb_io $(LIBS)

terminus-l1: terminus-l1.o l1-parts.o $(L1_OBJS)
	$(CPP) $(CPP_CFLAGS) $(CPP_LFLAGS) -o $@ $^ -lrf_pipelines -lbonsai -lch_frb_io $(LIBS) -lsimpulse -lzmq

clean:
	rm -f *.o *~ $(BINARIES)

install:
	echo 'Nothing to install here!'

uninstall:
	echo 'Nothing to uninstall here!'


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
