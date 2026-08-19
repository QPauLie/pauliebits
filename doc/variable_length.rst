Variable length pauliebits format
===============================

In some cases, it is useful to represent pauliebits in a binary format that
is "self-terminating" (in the same way that C strings are NUL terminated).
That is, when an encoded pauliebits of unknown length is encountered in a
stream of binary data, the format lets us know when the end of the encoded
pauliebits is reached.
Such a "variable-length format" (most memory-efficient for small pauliebits)
is implemented in ``vl_encode()`` and ``vl_decode()``:

.. code-block:: python

    >>> from pauliebits import pauliebits
    >>> from pauliebits.util import vl_encode, vl_decode
    >>> a = pauliebits('0110001111')
    >>> b = pauliebits('001')
    >>> data = vl_encode(a) + vl_encode(b) + b'other stuff'
    >>> data
    b'\x96\x1e\x12other stuff'
    >>> stream = iter(data)
    >>> vl_decode(stream)      # the remaining stream is untouched
    pauliebits('0110001111')
    >>> vl_decode(stream)
    pauliebits('001')
    >>> bytes(stream)
    b'other stuff'

The variable-length format is similar to LEB128.  A single byte can store
pauliebits up to 4 elements; every additional byte stores up to 7 more elements.
The most significant bit of each byte indicates whether more bytes follow.
In addition, the first byte contains 3 bits which indicate the number of
padding bits at the end of the stream.  Here is an example of
encoding ``pauliebits('01010110111001110')``:

.. code-block::

        01010110111001110        raw pauliebits
        0101  0110111  001110    grouped (4, 7, 7, ...)
        0101  0110111  0011100   pad last group with zeros
     0010101  0110111  0011100   add number of pad bits (1) to front (001)
    10010101 10110111 00011100   add high bits (1, except 0 for last group)
        0x95     0xb7     0x1c   in hexadecimal - output stream

.. code-block:: python

    >>> vl_encode(pauliebits('01010110111001110'))
    b'\x95\xb7\x1c'
