import re
import sys
import platform


if "test" in sys.argv:
    import pauliebits
    # when test was successful, return 0 (hence not)
    sys.exit(not pauliebits.test().wasSuccessful())

try:
    from setuptools import setup, Extension
except ImportError:
    from distutils.core import setup, Extension


kwds = {}
kwds['long_description'] = open('README.rst').read()

# Read version from pauliebits/pauliebits.h
pat = re.compile(r'#define\s+PAULIEBITS_VERSION\s+"(\S+)"', re.M)
data = open('pauliebits/pauliebits.h').read()
kwds['version'] = pat.search(data).group(1)

macros = []
if platform.python_implementation() == 'PyPy':
    macros.append(("PY_LITTLE_ENDIAN", str(int(sys.byteorder == 'little'))))
    macros.append(("PY_BIG_ENDIAN", str(int(sys.byteorder == 'big'))))

setup(
    name = "pauliebits",
    author = "Ilan Schnell",
    author_email = "ilanschnell@gmail.com",
    url = "https://github.com/ilanschnell/pauliebits",
    license = "PSF-2.0",
    python_requires = ">=3.7",
    classifiers = [
        "Development Status :: 6 - Mature",
        "Intended Audience :: Developers",
        "Operating System :: OS Independent",
        "Programming Language :: C",
        "Programming Language :: Python :: 3",
        "Programming Language :: Python :: 3.7",
        "Programming Language :: Python :: 3.8",
        "Programming Language :: Python :: 3.9",
        "Programming Language :: Python :: 3.10",
        "Programming Language :: Python :: 3.11",
        "Programming Language :: Python :: 3.12",
        "Programming Language :: Python :: 3.13",
        "Programming Language :: Python :: 3.14",
        "Programming Language :: Python :: Free Threading :: 2 - Beta",
        "Topic :: Utilities",
    ],
    description = "efficient arrays of booleans -- C extension",
    packages = ["pauliebits"],
    package_data = {"pauliebits": ["*.h", "*.pickle",
                                 "py.typed",  # see PEP 561
                                 "*.pyi"]},
    ext_modules = [Extension(name = "pauliebits.pauliebits",
                             define_macros = macros,
                             sources = ["pauliebits/_pauliebits.c"]),
                   Extension(name = "pauliebits._util",
                             sources = ["pauliebits/_util.c"])],
    zip_safe = False,
    **kwds
)
