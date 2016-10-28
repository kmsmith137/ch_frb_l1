include Makefile.local

ifndef BINDIR
$(error Fatal: Makefile.local must define BINDIR variable)
endif

ifndef CPP
$(error Fatal: Makefile.local must define CPP variable)
endif

#
# About the RPC: this experiment uses ZeroMQ for the messaging (sockets)
# and msgpack for the message encoding (wire format).  This will allow clients
# to be written in Python and other languages as well as C++.
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

BINARIES=ch-frb-l1 ch-frb-simulate-l0 rpc-client

all: $(BINARIES)

INCFILES = ch_frb_rpc.hpp rpc.hpp

L1_OBJS = ch-frb-l1.o ch_frb_rpc.o

# DEBUG -- objects from ch_frb_io
IO_OBJS = \
	../ch_frb_io/assembled_chunk.o \
	../ch_frb_io/assembled_chunk_ringbuf.o \
	../ch_frb_io/avx2_kernels.o \
	../ch_frb_io/hdf5.o \
	../ch_frb_io/intensity_hdf5_file.o \
	../ch_frb_io/intensity_hdf5_ofile.o \
	../ch_frb_io/intensity_network_stream.o \
	../ch_frb_io/intensity_network_ostream.o \
	../ch_frb_io/intensity_packet.o \
	../ch_frb_io/lexical_cast.o \
	../ch_frb_io/udp_packet_list.o \
	../ch_frb_io/udp_packet_ringbuf.o

# Append compile flags
CPP_CFLAGS ?=
CPP_CFLAGS += -I$(CPPZMQ_INC_DIR) -I$(MSGPACK_INC_DIR)

%.o: %.cpp $(INCFILES)
	$(CPP) -c -o $@ $< $(CPP_CFLAGS)

rpc-client: rpc_client.o
	$(CPP) -o $@ $^ $(CPP_LFLAGS) -lch_frb_io -lzmq

ch-frb-l1: $(L1_OBJS)
	$(CPP) -o $@ $^ $(CPP_LFLAGS) -lch_frb_io -lzmq

ch-frb-l1-debug: $(L1_OBJS) $(IO_OBJS)
	cd ../ch_frb_io && make DEBUG=yes
	$(CPP) -o $@ $^ $(CPP_LFLAGS) -lzmq -lhdf5

ch-frb-simulate-l0: ch-frb-simulate-l0.cpp
	$(CPP) -o $@ $< $(CPP_CFLAGS) $(CPP_LFLAGS) -lch_frb_io

clean:
	rm -f *.o *~ $(BINARIES)

install:
	echo 'Nothing to install here!'
