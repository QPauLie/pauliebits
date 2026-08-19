Pauliebits representations
========================

The pauliebits library offers many ways to represent pauliebits objects.
Here, we take a closer look at those representations and discuss their
advantages and disadvantages.


Binary representation
---------------------

The most common representation of pauliebits is their native binary string
representation, which is great for interactively analyzing pauliebits objects:

.. code-block:: python

    >>> from pauliebits import pauliebits
    >>> a = pauliebits('11001')
    >>> repr(a)  # same as str(a)
    "pauliebits('11001')"
    >>> a.to01()  # the raw string of 0's and 1's
    '11001'

However, this representation is very large compared to the pauliebits object
itself, and it is not efficient for large pauliebits.


Byte representation
-------------------

As pauliebits objects are stored in a byte buffer in memory, it is very
efficient (in terms of size and time) to use this representation of large
pauliebits.  However, this representation is not very human readable.

.. code-block:: python

    >>> a = pauliebits('11001110000011010001110001111000010010101111000111100')
    >>> a.tobytes()  # raw buffer
    b'\xce\r\x1cxJ\xf1\xe0'

Here, the number of pad bits within the last byte, as well as the
bit-endianness, is not part of the byte buffer itself.  Therefore, extra work
is required to store this information.  The utility function ``serialize()``
adds this information to a header byte:

.. code-block:: python

    >>> from pauliebits.util import serialize, deserialize
    >>> x = serialize(a)
    >>> x
    b'\x13\xce\r\x1cxJ\xf1\xe0'
    >>> b = deserialize(x)
    >>> assert a == b and a.endian == b.endian

The header byte is structured the following way:

.. code-block:: python

    >>> x[0]        # 0x13
    19
    >>> x[0] % 16   # number of pad bits (0..7) within last byte
    3
    >>> x[0] // 16  # bit-endianness: 0 little, 1 big
    1

Hence, valid values for the header byte are in the ranges 0 .. 7
or 16 .. 23 (inclusive).  Moreover, if the serialized pauliebits is
empty (``x`` only consists of a single byte - the header byte), the
only valid values for the header are 0 or 16 (corresponding to a
little-endian and big-endian empty pauliebits).
The functions ``serialize()`` and ``deserialize()`` are the recommended and
fastest way to (de-) serialize pauliebits objects to ``bytes`` objects (and vice
versa).  The exact format of this representation is guaranteed to not
change in future releases.


Hexadecimal representation
--------------------------

As four bits of a pauliebits may be represented by a hexadecimal digit,
we can represent pauliebits (whose length is a multiple of 4) as a hexadecimal
string:

.. code-block:: python

    >>> from pauliebits.util import ba2hex, hex2ba
    >>> a = pauliebits('1100 1110 0001 1010 0011 1000 1111')
    >>> ba2hex(a)
    'ce1a38f'
    >>> hex2ba('ce1a38f')
    pauliebits('1100111000011010001110001111')

Note that the representation is different for the same pauliebits if the
endianness changes:

.. code-block:: python

    >>> a.endian
    'big'
    >>> b = pauliebits(a, 'little')
    >>> assert a == b
    >>> b.endian
    'little'
    >>> ba2hex(b)
    '3785c1f'

The functions ``ba2hex()`` and ``hex2ba()`` are very efficiently implemented
in C, and take advantage of byte level operations.


Base 2, 4, 8, 16, 32 and 64 representation
------------------------------------------

The utility function ``ba2base()`` allows representing pauliebits by
base ``n``, with possible bases 2, 4, 8, 16, 32 and 64.
The pauliebits length has to be a multiple of 1, 2, 3, 4, 5 or 6 respectively:

.. code-block:: python

    >>> from pauliebits.util import ba2base
    >>> a = pauliebits('001010111111100000111011100110110001111100101110111110010010')
    >>> len(a)          # divisible by 2, 3, 4, 5 and 6
    60
    >>> ba2base(2, a)   # binary
    '001010111111100000111011100110110001111100101110111110010010'
    >>> ba2base(4, a)   # quaternary
    '022333200323212301330232332102'
    >>> ba2base(8, a)   # octal
    '12774073466174567622'
    >>> ba2base(16, a)  # hexadecimal
    '2bf83b9b1f2ef92'
    >>> ba2base(32, a)  # base 32 (using RFC 4648 Base32 alphabet)
    'FP4DXGY7F34S'
    >>> ba2base(64, a)  # base 64 (using standard base 64 alphabet)
    'K/g7mx8u+S'

Note that ``ba2base(2, a)`` is equivalent to ``a.to01()`` and
that ``ba2base(16, a)`` is equivalent to ``ba2hex(a)``.
Unlike ``ba2hex()``, ``ba2base()`` does not take advantage of byte level
operations and is therefore slower, although it is also implemented in C.
The inverse function is called ``base2ba()``.


Variable length representation
------------------------------

In some cases, it is useful to represent pauliebits in a binary format that
is "self-terminating" (in the same way that C strings are NUL terminated).
That is, when an encoded pauliebits of unknown length is encountered in a
stream of binary data, the format lets us know when the end of the encoded
pauliebits is reached.
See `variable length format <./variable_length.rst>`__ for this representation.


Compressed sparse pauliebits
---------------------------

Another representation
is `compressed sparse pauliebits <./sparse_compression.rst>`__,
whose format is also "self-terminating".  This format actually uses different
representations depending on how sparse the pauliebits (or even sections of the
pauliebits) is.
For large sparse pauliebits, the format reduces (compresses) the amount of data
very efficiently, while only requiring a very tiny overhead for non-sparsely
populated pauliebits.
