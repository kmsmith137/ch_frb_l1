from __future__ import print_function
from distutils.core import setup, Extension
import numpy
import os.path

from numpy.distutils.misc_util import get_numpy_include_dirs
numpy_inc = get_numpy_include_dirs()

def strlist(s, split=' '):
    lst = s.split(split)
    lst = [i.strip() for i in lst]
    lst = [i for i in lst if len(i)]
    return lst

link = strlist(os.environ.get('LINKFLAGS', ''))
cppflags = strlist(os.environ.get('CPPFLAGS', ''))
cpp = strlist(os.environ.get('CPP', ''))
objs = strlist(os.environ.get('OBJS', ''))

inc = strlist(' '.join(cppflags), split='-I')
if len(cpp) > 0 and cpp[0] == 'clang++':
    cpp = cpp[1:]
if len(cpp) > 0 and cpp[0] == 'g++':
    cpp = cpp[1:]
cppflags = cpp

print(('link:', link))
print(('inc:', inc))
print(('cppflags:', cppflags))

import distutils.core
print('distutils.core:', distutils.core.__file__)

c_module = Extension('simulate_l0',
                     sources = ['simulate_l0_py.cpp'],
                     include_dirs = numpy_inc + inc,
                     extra_compile_args = cppflags,
                     extra_objects = objs,
                     extra_link_args=link,
                     language='c++',
    )

setup(name = 'simulate_l0 in Python',
      version = '1.0',
      # py_modules = ['simulate_l0'],
      ext_modules = [c_module])

