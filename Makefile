include Makefile.local

ifndef BINDIR
$(error Fatal: Makefile.local must define BINDIR variable)
endif

ifndef CPP
$(error Fatal: Makefile.local must define CPP variable)
endif

BINARIES=ch-frb-l1 ch-frb-simulate-l0

all: $(BINARIES)

ch-frb-l1: ch-frb-l1.cpp
	$(CPP) -o $@ $< $(CPP_LFLAGS) -lch_frb_io

ch-frb-simulate-l0: ch-frb-simulate-l0.cpp
	$(CPP) -o $@ $< $(CPP_LFLAGS) -lch_frb_io

clean:
	rm -f *.o *~ $(BINARIES)
