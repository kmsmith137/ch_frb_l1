#!/usr/bin/env python
# -*- coding: utf-8 -*-

from __future__ import print_function
from __future__ import division
from __future__ import absolute_import
from setuptools import setup
from codecs import open
from os import path

here = path.abspath(path.dirname(__file__))

with open(path.join(here, 'requirements.txt')) as f:
    requirements = f.readlines()

setup(
    name='frb_l1_wview',
    version='0.1.0',
    description='Web Application to operate the FRB L1 system.'
,

    author='Dustin Lang, Chitrang Patel',
    author_email='chitrang.patel@mail.mcgill.ca',
    url="https://github.com/kmsmith137/ch_frb_l1/",

    py_modules=['webapp', 'chime_frb_operations', 'chlog_database', 'cnc_ssh', 'cnc_client'],
    install_requires=requirements,
)
