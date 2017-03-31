#define PY_SSIZE_T_CLEAN 1
#include "Python.h"

#include <stdio.h>
#include <assert.h>
#include <bitshuffle.h>

static PyObject* bitshuffle_decompress(PyObject* self, PyObject* args) {
    Py_ssize_t cbytes;
    const char* cdata;
    int ndecomp;
    char* data;

    // Arguments: string of compressed data, int of decompressed size.
    if (!PyArg_ParseTuple(args, "s#i", &cdata, &cbytes, &ndecomp))
        return NULL;

    //printf("Got %i bytes to decompress into %i bytes\n", (int)cbytes, ndecomp);
    data = malloc(ndecomp);
    if (!data) {
        PyErr_SetString(PyExc_MemoryError, "Failed to allocate space for bitshuffle-uncompressed data");
        return NULL;
    }

    int64_t n = bshuf_decompress_lz4((const void*)cdata, (void*)data, (size_t)ndecomp, 1, 0);
    //printf("Decompressed: read %i\n", (int)n);

    if (n != cbytes) {
        PyErr_SetString(PyExc_ValueError, "Bitshuffle-decompressed data was not the size expected!");
        free(data);
        return NULL;
    }
    return PyByteArray_FromStringAndSize(data, ndecomp);
}

static PyMethodDef bitshuffleMethods[] = {
    { "decompress", bitshuffle_decompress, METH_VARARGS,
      "decompress bitshuffled data" },
    {NULL, NULL, 0, NULL}
};

PyMODINIT_FUNC
initpybitshuffle(void) {
    Py_InitModule("pybitshuffle", bitshuffleMethods);
}

