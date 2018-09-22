#include <Python.h>

#if PY_MAJOR_VERSION >= 3
#define IS_PY3K
#define PyInt_FromLong PyLong_FromLong
#endif

#include <numpy/arrayobject.h>

#include "simulate-l0.hpp"

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

static PyMethodDef l0sim_methods[] = {
    /*
     {"set_name", (PyCFunction)l0sim_set_name, METH_VARARGS,
     "Sets the Kd-Tree's name to the given string",
     },
     */
    {"send_chunk_file", (PyCFunction)l0sim_send_chunk_file, METH_VARARGS,
     "Sends the given assembled-chunk msgpack file"},
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

static PyMethodDef moduleMethods[] = {
    /*
     { "match", spherematch_match, METH_VARARGS,
     "find matching data points" },
     */
    {NULL, NULL, 0, NULL}
};

#if defined(IS_PY3K)

static struct PyModuleDef spherematch_module = {
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

    m = PyModule_Create(&spherematch_module);
    if (m == NULL)
        return NULL;

    Py_INCREF((PyObject*)&L0SimType);
    PyModule_AddObject(m, "l0sim", (PyObject*)&L0SimType);

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

    m = Py_InitModule3("simulate_l0", moduleMethods,
                       "simulate_l0 provides python bindings for simulating the L0 system");

    Py_INCREF((PyObject*)&L0SimType);
    PyModule_AddObject(m, "l0sim", (PyObject*)&L0SimType);
}

#endif

