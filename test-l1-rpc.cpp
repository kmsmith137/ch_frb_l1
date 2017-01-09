#include <unistd.h>
#include <iostream>
#include <ch_frb_io.hpp>
#include <l1-rpc.hpp>

using namespace std;
using namespace ch_frb_io;

int main() {
    int beam = 77;

    intensity_network_stream::initializer ini;
    ini.beam_ids.push_back(beam);
    //ini.mandate_fast_kernels = HAVE_AVX2;

    shared_ptr<intensity_network_stream> stream = intensity_network_stream::make(ini);
    stream->start_stream();

    // listen on localhost only, for local-machine testing (trying to
    // listen on other ports triggers GUI window popup warnings on Mac
    // OSX)
    l1_rpc_server_start(stream, "tcp://127.0.0.1:5555");

    int nupfreq = 4;
    int nt_per = 16;
    int fpga_per = 400;

    assembled_chunk* ch;

    std::random_device rd;
    std::mt19937 rng(rd());
    rng.seed(42);
    std::uniform_int_distribution<> rando(0,1);

    for (int i=0; i<100; i++) {
        ch = new assembled_chunk(beam, nupfreq, nt_per, fpga_per, i);
        cout << "Pushing " << i << endl;
        stream->inject_assembled_chunk(ch);
        cout << "Pushed " << i << endl;

        // downstream thread consumes with a lag of 2...
        if (i >= 2) {
            // Randomly consume 0 to 2 chunks
            if (rando(rng)) {
                cout << "Downstream consumes a chunk" << endl;
                stream->get_assembled_chunk(0, false);
            }
            if (rando(rng)) {
                cout << "Downstream consumes a chunk" << endl;
                stream->get_assembled_chunk(0, false);
            }
        }
    }

    cout << "End state:" << endl;
    //rb->print();
    //cout << endl;

    vector<vector<shared_ptr<assembled_chunk> > > chunks;
    cout << "Retrieving chunks..." << endl;
    //rb->retrieve(30000000, 50000000, chunks);
    vector<uint64_t> beams;
    beams.push_back(beam);
    chunks = stream->get_ringbuf_snapshots(beams);
    cout << "Got " << chunks.size() << " beams, with number of chunks:";
    for (auto it = chunks.begin(); it != chunks.end(); it++) {
        cout << " " << it->size();
    }
    cout << endl;

    usleep(30 * 1000000);
}


/*
int main() {
    cout << "Creating ringbuf..." << endl;
    Ringbuf<int> rb(4);

    int a = 42;
    int b = 43;
    int c = 44;

    cout << "Pushing" << endl;
    rb.push(&a);
    cout << "Pushing" << endl;
    rb.push(&b);
    cout << "Pushing" << endl;
    rb.push(&c);

    cout << "Popping" << endl;
    shared_ptr<int> p1 = rb.pop();
    cout << "Popping" << endl;
    shared_ptr<int> p2 = rb.pop();
    cout << "Dropping" << endl;
    p1.reset();
    cout << endl;

    int d = 45;
    int e = 46;
    int f = 47;
    int g = 48;

    cout << "Pushing d..." << endl;
    shared_ptr<int> pd = rb.push(&d);

    cout << endl;
    cout << "Pushing e..." << endl;
    shared_ptr<int> pe = rb.push(&e);

    cout << endl;
    cout << "Pushing f..." << endl;
    shared_ptr<int> pf = rb.push(&f);

    cout << endl;
    cout << "Pushing g..." << endl;
    rb.push(&g);

    cout << "Done" << endl;

}
 */

