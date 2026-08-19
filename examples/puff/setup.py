from os.path import dirname

try:
    from setuptools import setup, Extension
except ImportError:
    from distutils.core import setup, Extension

import pauliebits


setup(
    name = "puff",
    ext_modules = [Extension(
        name = "_puff",
        sources = ["_puff.c"],
        include_dirs = [dirname(pauliebits.__file__)],
    )],
)
