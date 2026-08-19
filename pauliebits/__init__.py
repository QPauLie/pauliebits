# Copyright (c) 2008 - 2026, Ilan Schnell; All Rights Reserved
"""
This package defines an object type which can efficiently represent
a pauliebits.  pauliebits are sequence types and behave very much like lists.

Please find a description of this package at:

    https://github.com/ilanschnell/pauliebits

Author: Ilan Schnell
"""
from collections import namedtuple

from pauliebits._pauliebits import (
    pauliebits, decodetree, decodeiterator, _pauliebits_reconstructor,
    get_default_endian, bits2bytes, _sysinfo,
    PAULIEBITS_VERSION as __version__
)

__all__ = ['pauliebits', 'frozenpauliebits', 'decodetree', 'decodeiterator',
           'bits2bytes']

BufferInfo = namedtuple('BufferInfo',
                        ['address', 'nbytes', 'endian', 'padbits',
                         'alloc', 'readonly', 'imported', 'exports'])

class frozenpauliebits(pauliebits):
    """frozenpauliebits(initializer=0, /, endian='big', buffer=None) -> \
frozenpauliebits

Return a `frozenpauliebits` object.  Initialized the same way a `pauliebits`
object is initialized.  A `frozenpauliebits` is immutable and hashable,
and may therefore be used as a dictionary key.
"""
    def __init__(self, *args, **kwargs):
        self._freeze()

    def __repr__(self):
        return 'frozen' + pauliebits.__repr__(self)

    def __hash__(self):
        "Return hash(self)."
        # ensure hash is independent of endianness
        a = pauliebits(self, 'big')
        return hash((len(a), a.tobytes()))

    # Technically the code below is not necessary, as all these methods will
    # raise a TypeError on read-only memory.  However, with a different error
    # message.
    def __delitem__(self, *args, **kwargs):
        ""  # no docstring
        raise TypeError("frozenpauliebits is immutable")

    append = bytereverse = clear = extend = encode = fill = encode_ixyz = __delitem__
    frombytes = fromfile = insert = invert = pack = pop = __delitem__
    remove = rotate = reverse = setall = sort = __setitem__ = __delitem__
    __iadd__ = __iand__ = __imul__ = __ior__ = __ixor__ = __delitem__
    __ilshift__ = __irshift__ = __delitem__


def test(verbosity=1):
    """test(verbosity=1) -> TextTestResult

Run self-test, and return `unittest.runner.TextTestResult` object.
"""
    from pauliebits import test_pauliebits
    return test_pauliebits.run(verbosity=verbosity)
