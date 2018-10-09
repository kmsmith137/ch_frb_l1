#include <Python.h>

#if PY_MAJOR_VERSION >= 3
#define IS_PY3K
#define PyInt_FromLong PyLong_FromLong
#endif

#include <numpy/arrayobject.h>

#include "simulate-l0.hpp"

#include <ch_frb_io.hpp>
using namespace ch_frb_io;




typedef struct {
    PyObject_HEAD
    /* Type-specific fields go here. */
    shared_ptr<ch_frb_io::assembled_chunk> chunk;
} chunk;

static void chunk_dealloc(chunk* self) {
    if (self)
        self->chunk.reset();
    Py_TYPE(self)->tp_free((PyObject*)self);
}

static PyObject* chunk_new(PyTypeObject *type, PyObject *args, PyObject *kwds) {
    chunk *self;
    self = (chunk*)type->tp_alloc(type, 0);
    if (self != NULL) {
        self->chunk = shared_ptr<ch_frb_io::assembled_chunk>();
    }
    return (PyObject*)self;
}

/*
 assembled_chunk(beam_id, fpga_counts_per_sample,
                 data, scale, offset, rfi)

 data: (constants::nfreq_coarse * nupfreq, constants::nt_per_assembled_Chunk)
 offsets, scales: (constants:nfreq_coarse, nt_coarse)
 rfi: (nrfifreq, nt_per_assembled_chunk) <--- not bit-packed
 */
static int chunk_init(chunk *self, PyObject *args, PyObject *keywords) {
    Py_ssize_t n;
    int beam_id;
    int fpga_counts_per_sample;
    int ichunk;
    Py_ssize_t nf, nt;

    int nupfreq;
    int nt_coarse;
    int nrfifreq;
    
    int d1,d2;
    
    PyObject* py_data;
    PyObject* py_scale;
    PyObject* py_offset;
    PyObject* py_rfi;
    bool have_rfi;

    PyArray_Descr* ftype = PyArray_DescrFromType(NPY_FLOAT);
    PyArray_Descr* btype = PyArray_DescrFromType(NPY_BOOL);
    PyArray_Descr* utype = PyArray_DescrFromType(NPY_UINT8);
    int req = NPY_ARRAY_C_CONTIGUOUS | NPY_ARRAY_ALIGNED | NPY_ARRAY_NOTSWAPPED | NPY_ARRAY_ELEMENTSTRIDES;

    n = PyTuple_Size(args);
    if (n != 7) {
        PyErr_SetString(PyExc_ValueError, "need 7 args: (beam_id, fpga_counts_per_sample, ichunk, data, scale, offset, rfi)");
        return -1;
    }
    // Try parsing as an array.
    if (!PyArg_ParseTuple(args, "iiiO!O!O!O",
                          &beam_id, &fpga_counts_per_sample, &ichunk,
                          &PyArray_Type, &py_data,
                          &PyArray_Type, &py_scale,
                          &PyArray_Type, &py_offset,
                          &py_rfi)) {
        PyErr_SetString(PyExc_ValueError, "Failed to parse args: need: (int beam_id, int fpga_counts_per_sample, array data, array scale, array offset, array rfi)");
        return -1;
    }

    have_rfi = (py_rfi != Py_None);
    
    Py_INCREF(utype);
    py_data = PyArray_FromAny(py_data, utype, 2, 2, req, NULL);
    if (!py_data) {
        PyErr_SetString(PyExc_ValueError, "Failed to convert 'data' array");
        return -1;
    }
    Py_INCREF(ftype);
    py_offset = PyArray_FromAny(py_offset, ftype, 2, 2, req, NULL);
    if (!py_offset) {
        PyErr_SetString(PyExc_ValueError, "Failed to convert 'offset' array");
        return -1;
    }
    Py_INCREF(ftype);
    py_scale = PyArray_FromAny(py_scale, ftype, 2, 2, req, NULL);
    if (!py_scale) {
        PyErr_SetString(PyExc_ValueError, "Failed to convert 'scale' array");
        return -1;
    }

    if (have_rfi) {
        Py_INCREF(btype);
        py_rfi = PyArray_FromAny(py_rfi, btype, 2, 2, req, NULL);
        if (!py_rfi) {
            PyErr_SetString(PyExc_ValueError, "Failed to convert 'rfi' array");
            return -1;
        }
    }

    if (PyArray_NDIM(py_data) != 2) {
        PyErr_SetString(PyExc_ValueError, "data array must be two-dimensional");
        return -1;
    }
    if (PyArray_TYPE(py_data) != NPY_UINT8) {
        PyErr_SetString(PyExc_ValueError, "data array must contain uint8 values");
        return -1;
    }
    nf = PyArray_DIM(py_data, 0);
    nt = PyArray_DIM(py_data, 1);

    nupfreq = nf / ch_frb_io::constants::nfreq_coarse_tot;
    
    if (nt != ch_frb_io::constants::nt_per_assembled_chunk) {
        PyErr_SetString(PyExc_ValueError, "data array must have nt_per_assembled_chunk time samples!");
        return -1;
    }

    if (PyArray_NDIM(py_scale) != 2) {
        PyErr_SetString(PyExc_ValueError, "scale array must be two-dimensional");
        return -1;
    }
    if (PyArray_TYPE(py_scale) != NPY_FLOAT) {
        PyErr_SetString(PyExc_ValueError, "scale array must contain floats");
        return -1;
    }
    d1 = (int)PyArray_DIM(py_scale, 0);
    nt_coarse = (int)PyArray_DIM(py_scale, 1);

    if (d1 != ch_frb_io::constants::nfreq_coarse_tot) {
        PyErr_SetString(PyExc_ValueError, "scale array must have nfreq_coarse samples!");
        return -1;
    }

    if (PyArray_NDIM(py_offset) != 2) {
        PyErr_SetString(PyExc_ValueError, "offset array must be two-dimensional");
        return -1;
    }
    if (PyArray_TYPE(py_offset) != NPY_FLOAT) {
        PyErr_SetString(PyExc_ValueError, "offset array must contain floats");
        return -1;
    }
    d1 = (int)PyArray_DIM(py_offset, 0);
    d2 = (int)PyArray_DIM(py_offset, 1);

    if ((d1 != ch_frb_io::constants::nfreq_coarse_tot) ||
        (d2 != nt_coarse)) {
        PyErr_SetString(PyExc_ValueError, "offset array must have nfreq_coarse x nt_coarse samples!");
        return -1;
    }

    if (have_rfi) {
        if (PyArray_NDIM(py_rfi) != 2) {
            PyErr_SetString(PyExc_ValueError, "rfi array must be two-dimensional");
            return -1;
        }
        if (PyArray_TYPE(py_rfi) != NPY_BOOL) {
            PyErr_SetString(PyExc_ValueError, "rfi array must contain booleans");
            return -1;
        }
        d1 = (int)PyArray_DIM(py_rfi, 0);
        d2 = (int)PyArray_DIM(py_rfi, 1);
        if (d2 != ch_frb_io::constants::nt_per_assembled_chunk) {
            PyErr_SetString(PyExc_ValueError, "rfi array must have nt_per_assembled_chunk samples!");
            return -1;
        }
        nrfifreq = d1;

    } else {
        nrfifreq = 0;
    }

    /////////////////////////////////
    ch_frb_io::assembled_chunk::initializer ini;
    ini.beam_id = beam_id;
    ini.nupfreq = nupfreq;
    ini.nrfifreq = nrfifreq;
    ini.nt_per_packet = constants::nt_per_assembled_chunk / nt_coarse;
    ini.fpga_counts_per_sample = fpga_counts_per_sample;
    ini.ichunk = ichunk;
    self->chunk = ch_frb_io::assembled_chunk::make(ini);

    uint8_t* du;
    du = (uint8_t*)PyArray_DATA(py_data);
    memcpy(self->chunk->data, du, self->chunk->ndata);
    float* f;
    f = (float*)PyArray_DATA(py_offset);
    memcpy(self->chunk->offsets, f, self->chunk->nscales * sizeof(float));
    //for (int i=0; i<self->chunk->nscales; i++) {
    //self->chunk->offsets[i] = (uint8_t)(d[i]);
    //}
    f = (float*)PyArray_DATA(py_scale);
    memcpy(self->chunk->scales, f, self->chunk->nscales * sizeof(float));
    //for (int i=0; i<self->chunk->nscales; i++) {
    //self->chunk->scales[i] = (uint8_t)(d[i]);
    //}
    if (have_rfi) {
        bool* b = (bool*)PyArray_DATA(py_rfi);
        for (int i=0; i<self->chunk->nrfimaskbytes/8; i++) {
            uint8_t bval = 0;
            for (int j=0; j<8; j++)
                bval |= (b[i] * (1<<j));
            self->chunk->rfi_mask[i] = bval;
        }
        self->chunk->has_rfi_mask = true;
    }

    Py_DECREF(py_data);
    Py_DECREF(py_offset);
    Py_DECREF(py_scale);
    Py_DECREF(py_rfi);
    Py_INCREF(ftype);
    Py_INCREF(btype);
    Py_INCREF(utype);
    return 0;
}

static PyObject* chunk_write(chunk* self, PyObject* args) {
    char* fn;
#if defined(IS_PY3K)
    PyObject *fnbytes = NULL;
    if (!PyArg_ParseTuple(args, "O&", PyUnicode_FSConverter, &fnbytes)) {
        PyErr_SetString(PyExc_ValueError, "need filename (string)");
        return NULL;
    }
    if (fnbytes == NULL)
        return NULL;
    fn = PyBytes_AsString(fnbytes);
#else
    if (!PyArg_ParseTuple(args, "s", &fn)) {
        PyErr_SetString(PyExc_ValueError, "need filename (string)");
        return NULL;
    }
#endif

    bool compress = false;
    self->chunk->write_msgpack_file(string(fn), compress);
    
#if defined(IS_PY3K)
    Py_DECREF(fnbytes);
#endif
    Py_RETURN_NONE;
}

static PyMethodDef chunk_methods[] = {
    {"write", (PyCFunction)chunk_write, METH_VARARGS,
     "Writes the given chunk to a msgpack file"},
    {NULL}
};

static PyGetSetDef chunk_getseters[] = {
    /*
     {"n",
     (getter)chunk_n, NULL, "number of data items in kd-tree",
     NULL},
     */
    {NULL}
};

static PyTypeObject ChunkType = {
    PyVarObject_HEAD_INIT(NULL, 0)
    "assembled_chunk",      /* tp_name */
    sizeof(chunk),          /* tp_basicsize */
    0,                         /* tp_itemsize */
    (destructor)chunk_dealloc, /* tp_dealloc */
    0,                         /* tp_print */
    0,                         /* tp_getattr */
    0,                         /* tp_setattr */
    0,                         /* tp_reserved */
    0,                         /* tp_repr */
    0,                         /* tp_as_number */
    0,                         /* tp_as_sequence */
    0,                         /* tp_as_mapping */
    0,                         /* tp_hash  */
    0,                         /* tp_call */
    0,                         /* tp_str */
    0,                         /* tp_getattro */
    0,                         /* tp_setattro */
    0,                         /* tp_as_buffer */
    Py_TPFLAGS_DEFAULT,        /* tp_flags */
    "chunk object",           /* tp_doc */
    0,                         /* tp_traverse */
    0,                         /* tp_clear */
    0,                         /* tp_richcompare */
    0,                         /* tp_weaklistoffset */
    0,                         /* tp_iter */
    0,                         /* tp_iternext */
    chunk_methods,            /* tp_methods */
    0, //Noddy_members,             /* tp_members */
    chunk_getseters,          /* tp_getset */
    0,                         /* tp_base */
    0,                         /* tp_dict */
    0,                         /* tp_descr_get */
    0,                         /* tp_descr_set */
    0,                         /* tp_dictoffset */
    (initproc)chunk_init,     /* tp_init */
    0,                         /* tp_alloc */
    chunk_new,                /* tp_new */
};



static PyMethodDef moduleMethods[] = {
    /*
     { "match", spherematch_match, METH_VARARGS,
     "find matching data points" },
     */
    {NULL, NULL, 0, NULL}
};




typedef struct {
    PyObject_HEAD
    /* Type-specific fields go here. */
    struct l0_params l0;
} l0sim;

static void l0sim_dealloc(l0sim* self) {
    self->l0.streams.clear();
    Py_TYPE(self)->tp_free((PyObject*)self);
}

static PyObject* l0sim_new(PyTypeObject *type, PyObject *args, PyObject *kwds) {
    l0sim *self;
    self = (l0sim*)type->tp_alloc(type, 0);
    if (self != NULL) {
    }
    return (PyObject*)self;
}

static int l0sim_init(l0sim *self, PyObject *args, PyObject *keywords) {
    Py_ssize_t n;
    PyObject *fnbytes = NULL;
    char* filename = NULL;
    double gbps;

    n = PyTuple_Size(args);
    if (n != 2) {
        PyErr_SetString(PyExc_ValueError, "need two args: string filename, double gbps");
        return -1;
    }
#if defined(IS_PY3K)
    if (!PyArg_ParseTuple(args, "O&d", PyUnicode_FSConverter, &fnbytes, &gbps))
        return -1;
    if (fnbytes == NULL)
        return -1;
    filename = PyBytes_AsString(fnbytes);
    Py_DECREF(fnbytes);
#else
    if (!PyArg_ParseTuple(args, "sd", &filename, &gbps))
        return -1;
#endif
    self->l0 = l0_params(string(filename), gbps);
    return 0;
}

static PyObject* l0sim_send_chunk_file(l0sim* self, PyObject* args) {
    char* fn;
    int istream;
#if defined(IS_PY3K)
    PyObject *fnbytes = NULL;
    if (!PyArg_ParseTuple(args, "iO&", &istream,
                          PyUnicode_FSConverter, &fnbytes)) {
        return NULL;
    }
    if (fnbytes == NULL)
        return NULL;
    fn = PyBytes_AsString(fnbytes);
#else
    if (!PyArg_ParseTuple(args, "is", &istream, &fn)) {
        PyErr_SetString(PyExc_ValueError, "need two args: stream (int), filename (string)");
        return NULL;
    }
#endif

    std::vector<std::string> filenames;
    string sfn(fn);
    filenames.push_back(sfn);
    cout << "istream " << istream << ", filename " << sfn << endl;
    self->l0.send_chunk_files(istream, filenames);

    while (true) {
        if (!self->l0.streams[istream]->is_sending())
            break;
        usleep(1000000);
    }
    
#if defined(IS_PY3K)
    Py_DECREF(fnbytes);
#endif
    Py_RETURN_NONE;
}

static PyObject* l0sim_send_chunk(l0sim* self, PyObject* args) {
    int istream;
    //ChunkType pychunk;
    chunk* pychunk;

    if (!PyArg_ParseTuple(args, "iO!", &istream, &ChunkType, &pychunk)) {
        return NULL;
    }

    std::vector<std::shared_ptr<ch_frb_io::assembled_chunk> > chunks;
    chunks.push_back(pychunk->chunk);
    self->l0.send_chunks(istream, chunks);

    while (true) {
        if (!self->l0.streams[istream]->is_sending())
            break;
        usleep(1000000);
    }
    Py_RETURN_NONE;
}

static PyMethodDef l0sim_methods[] = {
    {"send_chunk_file", (PyCFunction)l0sim_send_chunk_file, METH_VARARGS,
     "Sends the given assembled-chunk msgpack file"},
    {"send_chunk", (PyCFunction)l0sim_send_chunk, METH_VARARGS,
     "Sends the given assembled-chunk object"},
    {NULL}
};

static PyGetSetDef l0sim_getseters[] = {
    /*
     {"n",
     (getter)l0sim_n, NULL, "number of data items in kd-tree",
     NULL},
     */
    {NULL}
};

static PyTypeObject L0SimType = {
    PyVarObject_HEAD_INIT(NULL, 0)
    "l0sim",      /* tp_name */
    sizeof(l0sim),          /* tp_basicsize */
    0,                         /* tp_itemsize */
    (destructor)l0sim_dealloc, /* tp_dealloc */
    0,                         /* tp_print */
    0,                         /* tp_getattr */
    0,                         /* tp_setattr */
    0,                         /* tp_reserved */
    0,                         /* tp_repr */
    0,                         /* tp_as_number */
    0,                         /* tp_as_sequence */
    0,                         /* tp_as_mapping */
    0,                         /* tp_hash  */
    0,                         /* tp_call */
    0,                         /* tp_str */
    0,                         /* tp_getattro */
    0,                         /* tp_setattro */
    0,                         /* tp_as_buffer */
    Py_TPFLAGS_DEFAULT,        /* tp_flags */
    "l0sim object",           /* tp_doc */
    0,                         /* tp_traverse */
    0,                         /* tp_clear */
    0,                         /* tp_richcompare */
    0,                         /* tp_weaklistoffset */
    0,                         /* tp_iter */
    0,                         /* tp_iternext */
    l0sim_methods,            /* tp_methods */
    0, //Noddy_members,             /* tp_members */
    l0sim_getseters,          /* tp_getset */
    0,                         /* tp_base */
    0,                         /* tp_dict */
    0,                         /* tp_descr_get */
    0,                         /* tp_descr_set */
    0,                         /* tp_dictoffset */
    (initproc)l0sim_init,     /* tp_init */
    0,                         /* tp_alloc */
    l0sim_new,                /* tp_new */
};





#if defined(IS_PY3K)

static struct PyModuleDef simulate_l0_module = {
    PyModuleDef_HEAD_INIT,
    "simulate_l0",
    NULL,
    0,
    moduleMethods,
    NULL,
    NULL,
    NULL,
    NULL
};

PyMODINIT_FUNC
PyInit_simulate_l0(void) {
    PyObject *m;
    import_array();

    L0SimType.tp_new = PyType_GenericNew;
    if (PyType_Ready(&L0SimType) < 0)
        return NULL;

    ChunkType.tp_new = PyType_GenericNew;
    if (PyType_Ready(&ChunkType) < 0)
        return NULL;
    
    m = PyModule_Create(&simulate_l0_module);
    if (m == NULL)
        return NULL;

    Py_INCREF((PyObject*)&L0SimType);
    PyModule_AddObject(m, "l0sim", (PyObject*)&L0SimType);
    Py_INCREF((PyObject*)&ChunkType);
    PyModule_AddObject(m, "assembled_chunk", (PyObject*)&ChunkType);

    return m;
}

#else

PyMODINIT_FUNC
init_simulate_l0(void) {
    PyObject* m;
    import_array();

    L0SimType.tp_new = PyType_GenericNew;
    if (PyType_Ready(&L0SimType) < 0)
        return;

    ChunkType.tp_new = PyType_GenericNew;
    if (PyType_Ready(&ChunkType) < 0)
        return;
    
    m = Py_InitModule3("simulate_l0", moduleMethods,
                       "simulate_l0 provides python bindings for simulating the L0 system");

    Py_INCREF((PyObject*)&L0SimType);
    PyModule_AddObject(m, "l0sim", (PyObject*)&L0SimType);
    Py_INCREF((PyObject*)&ChunkType);
    PyModule_AddObject(m, "assembled_chunk", (PyObject*)&ChunkType);
}

#endif

