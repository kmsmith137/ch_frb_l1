#include <Python.h>

#include <vector>
#include <string>

#if PY_MAJOR_VERSION >= 3
#define IS_PY3K
#define PyInt_FromLong PyLong_FromLong
#endif

//#include <numpy/arrayobject.h>

#include "ch_frb_l1.hpp"

#include <ch_frb_io.hpp>
using namespace ch_frb_io;

////// Python wrapper for ch_frb_l1::l1_server ////

typedef struct {
    PyObject_HEAD
    /* Type-specific fields go here. */
    std::shared_ptr<ch_frb_l1::l1_server> l1;
} l1_server_py;

static void l1_server_dealloc(l1_server_py* self) {
    //self->l1. ...;
    Py_TYPE(self)->tp_free((PyObject*)self);
}

static PyObject* l1_server_new(PyTypeObject *type, PyObject *args, PyObject *kwds) {
    l1_server_py *self;
    self = (l1_server_py*)type->tp_alloc(type, 0);
    if (self != NULL) {
    }
    return (PyObject*)self;
}

static int l1_server_init(l1_server_py *self, PyObject *args, PyObject *keywords) {
    if (PySequence_Check(args) == 0) {
        PyErr_SetString(PyExc_ValueError, "l1_server: expect list of bytes");
        return -1;
    }
    std::vector<std::string> stringargs;
    Py_ssize_t n = PySequence_Size(args);
    for (Py_ssize_t i=0; i<n; i++) {
        PyObject* s = PySequence_GetItem(args, i);
        if (PyBytes_Check(s) == 0) {
            PyErr_SetString(PyExc_ValueError, "l1_server: expect list of bytes");
            return -1;
        }
        char* cstr = PyBytes_AsString(s);
        // makes a copy
        stringargs.push_back(std::string(cstr));
    }
    std::vector<const char*> cptrs;
    cptrs.push_back("ch-frb-l1");
    for (size_t i=0; i<stringargs.size(); i++)
        cptrs.push_back(stringargs[i].c_str());
    int argc = cptrs.size();
    const char** argv = cptrs.data();
    self->l1 = std::make_shared<ch_frb_l1::l1_server>(argc, argv);
    return 0;
}

static PyMethodDef l1_server_methods[] = {
    {NULL}
};

static PyGetSetDef l1_server_getseters[] = {
    {NULL}
};

static PyTypeObject L1_Server_Type = {
    PyVarObject_HEAD_INIT(NULL, 0)
    "l1_server",      /* tp_name */
    sizeof(l1_server_py),          /* tp_basicsize */
    0,                         /* tp_itemsize */
    (destructor)l1_server_dealloc, /* tp_dealloc */
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
    "l1_server object",           /* tp_doc */
    0,                         /* tp_traverse */
    0,                         /* tp_clear */
    0,                         /* tp_richcompare */
    0,                         /* tp_weaklistoffset */
    0,                         /* tp_iter */
    0,                         /* tp_iternext */
    l1_server_methods,            /* tp_methods */
    0, //Noddy_members,             /* tp_members */
    l1_server_getseters,          /* tp_getset */
    0,                         /* tp_base */
    0,                         /* tp_dict */
    0,                         /* tp_descr_get */
    0,                         /* tp_descr_set */
    0,                         /* tp_dictoffset */
    (initproc)l1_server_init,     /* tp_init */
    0,                         /* tp_alloc */
    l1_server_new,                /* tp_new */
};


static PyMethodDef moduleMethods[] = {
    {NULL, NULL, 0, NULL}
};



#if defined(IS_PY3K)

static struct PyModuleDef l1_server_module = {
    PyModuleDef_HEAD_INIT,
    "l1_server",
    NULL,
    0,
    moduleMethods,
    NULL,
    NULL,
    NULL,
    NULL
};

PyMODINIT_FUNC
PyInit_l1_server(void) {
    PyObject *m;
    //import_array();

    L1_Server_Type.tp_new = PyType_GenericNew;
    if (PyType_Ready(&L1_Server_Type) < 0)
        return NULL;

    m = PyModule_Create(&l1_server_module);
    if (m == NULL)
        return NULL;

    Py_INCREF((PyObject*)&L1_Server_Type);
    PyModule_AddObject(m, "l1_server", (PyObject*)&L1_Server_Type);

    return m;
}

#else

PyMODINIT_FUNC
initl1_server(void) {
    PyObject* m;
    //import_array();

    L1_Server_Type.tp_new = PyType_GenericNew;
    if (PyType_Ready(&L1_Server_Type) < 0)
        return;

    m = Py_InitModule3("l1_server", moduleMethods,
                       "l1_server provides python bindings for the L1 server process");

    Py_INCREF((PyObject*)&L1_Server_Type);
    PyModule_AddObject(m, "l1_server", (PyObject*)&L1_Server_Type);
}

#endif

