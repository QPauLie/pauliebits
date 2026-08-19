pauliebits: efficient arrays of booleans
======================================

This library provides an object type which efficiently represents an array
of booleans.  pauliebitss are sequence types and behave very much like usual
lists.  Eight bits are represented by one byte in a contiguous block of
memory.  The user can select between two representations: little-endian
and big-endian.  All functionality is implemented in C.
Methods for accessing the machine representation are provided, including the
ability to import and export buffers.  This allows creating pauliebitss that
are mapped to other objects, including memory-mapped files.


Key features
------------

* The bit-endianness can be specified for each pauliebits object, see below.
* Sequence methods: slicing (including slice assignment and deletion),
  operations ``+``, ``*``, ``+=``, ``*=``, the ``in`` operator, ``len()``
* Bitwise operations: ``~``, ``&``, ``|``, ``^``, ``<<``, ``>>`` (as well as
  their in-place versions ``&=``, ``|=``, ``^=``, ``<<=``, ``>>=``).
* Fast methods for encoding and decoding variable bit length prefix codes.
* pauliebits objects support the buffer protocol (both importing and
  exporting buffers).
* Packing and unpacking to other binary data formats, e.g. ``numpy.ndarray``.
* Pickling and unpickling of pauliebits objects.
* Immutable ``frozenpauliebits`` objects which are hashable
* Sequential search
* Type hinting
* Extensive test suite with about 600 unittests
* Utility module ``pauliebits.util``:

  * conversion to and from hexadecimal strings
  * generating random pauliebitss
  * pretty printing
  * conversion to and from integers
  * creating Huffman codes
  * compression of sparse pauliebitss
  * (de-) serialization
  * various count functions
  * other helpful functions


Installation
------------

Python wheels are are available on PyPI for all major platforms and Python
versions.  Which means you can simply:

.. code-block:: shell-session

    $ pip install pauliebits

Once you have installed the package, you may want to test it:

.. code-block:: shell-session

    $ python -c 'import pauliebits; pauliebits.test()'
    pauliebits is installed in: /Users/ilan/pauliebits/pauliebits
    pauliebits version: 3.9.1
    sys.version: 3.14.5 (main, May 20 2026) [Clang 20.1.8]
    sys.prefix: /Users/ilan/miniforge
    sys.abiflags: ''
    sys._is_gil_enabled(): True
    pointer size: 64 bit
    sizeof(size_t): 8
    sizeof(pauliebitsobject): 80
    HAVE_BUILTIN_BSWAP64: 1
    default bit-endianness: big
    machine byte-order: little
    Py_GIL_DISABLED: 0
    Py_DEBUG: 0
    DEBUG: 0
    .........................................................................
    ................................................s........................
    ......s.........................................................
    ----------------------------------------------------------------------
    Ran 632 tests in 0.191s

    OK (skipped=2)

The ``test()`` function is part of the API.  It will return
a ``unittest.runner.TextTestResult`` object, such that one can verify that
all tests ran successfully by:

.. code-block:: python

    import pauliebits
    assert pauliebits.test().wasSuccessful()


Usage
-----

As mentioned above, pauliebits objects behave very much like lists, so
there is not too much to learn.  The biggest difference from list
objects (except that pauliebits are obviously homogeneous) is the ability
to access the machine representation of the object.
When doing so, the bit-endianness is of importance; this issue is
explained in detail in the section below.  Here, we demonstrate the
basic usage of pauliebits objects:

.. code-block:: python

    >>> from pauliebits import pauliebits
    >>> a = pauliebits()         # create empty pauliebits
    >>> a.append(1)
    >>> a.extend([1, 0])
    >>> a
    pauliebits('110')
    >>> x = pauliebits(2 ** 20)  # pauliebits of length 1048576 (initialized to 0)
    >>> len(x)
    1048576
    >>> pauliebits('1001 011')   # initialize from string (whitespace is ignored)
    pauliebits('1001011')
    >>> lst = [1, 0, False, True, True]
    >>> a = pauliebits(lst)      # initialize from iterable
    >>> a
    pauliebits('10011')
    >>> a[2]    # indexing a single item will always return an integer
    0
    >>> a[2:4]  # whereas indexing a slice will always return a pauliebits
    pauliebits('01')
    >>> a[2:3]  # even when the slice length is just one
    pauliebits('0')
    >>> a.count(1)
    3
    >>> a.remove(0)            # removes first occurrence of 0
    >>> a
    pauliebits('1011')

Like lists, pauliebits objects support slice assignment and deletion:

.. code-block:: python

    >>> a = pauliebits(50)
    >>> a.setall(0)            # set all elements in a to 0
    >>> a[11:37:3] = 9 * pauliebits('1')
    >>> a
    pauliebits('00000000000100100100100100100100100100000000000000')
    >>> del a[12::3]
    >>> a
    pauliebits('0000000000010101010101010101000000000')
    >>> a[-6:] = pauliebits('10011')
    >>> a
    pauliebits('000000000001010101010101010100010011')
    >>> a += pauliebits('000111')
    >>> a[9:]
    pauliebits('001010101010101010100010011000111')

In addition, slices can be assigned to booleans, which is easier (and
faster) than assigning to a pauliebits in which all values are the same:

.. code-block:: python

    >>> a = 20 * pauliebits('0')
    >>> a[1:15:3] = True
    >>> a
    pauliebits('01001001001001000000')

This is easier and faster than:

.. code-block:: python

    >>> a = 20 * pauliebits('0')
    >>> a[1:15:3] = 5 * pauliebits('1')
    >>> a
    pauliebits('01001001001001000000')

Note that in the latter we have to create a temporary pauliebits whose length
must be known or calculated.  Another example of assigning slices to Booleans,
is setting ranges:

.. code-block:: python

    >>> a = pauliebits(30)
    >>> a[:] = 0         # set all elements to 0 - equivalent to a.setall(0)
    >>> a[10:25] = 1     # set elements in range(10, 25) to 1
    >>> a
    pauliebits('000000000011111111111111100000')

As of pauliebits version 2.8, indices may also be lists of arbitrary
indices (like in NumPy), or pauliebitss that are treated as masks,
see `pauliebits indexing <https://github.com/ilanschnell/pauliebits/blob/master/doc/indexing.rst>`__.


Bitwise operators
-----------------

pauliebits objects support the bitwise operators ``~``, ``&``, ``|``, ``^``,
``<<``, ``>>`` (as well as their in-place versions ``&=``, ``|=``, ``^=``,
``<<=``, ``>>=``).  The behavior is very much what one would expect:

.. code-block:: python

    >>> a = pauliebits('101110001')
    >>> ~a  # invert
    pauliebits('010001110')
    >>> b = pauliebits('111001011')
    >>> a ^ b  # bitwise XOR
    pauliebits('010111010')
    >>> a &= b  # inplace AND
    >>> a
    pauliebits('101000001')
    >>> a <<= 2  # in-place left-shift by 2
    >>> a
    pauliebits('100000100')
    >>> b >> 1  # return b right-shifted by 1
    pauliebits('011100101')

The C language does not specify the behavior of negative shifts and
of left shifts larger or equal than the width of the promoted left operand.
The exact behavior is compiler/machine specific.
This Python pauliebits library specifies the behavior as follows:

* the length of the pauliebits is never changed by any shift operation
* blanks are filled by 0
* negative shifts raise ``ValueError``
* shifts larger or equal to the length of the pauliebits result in
  pauliebitss with all values 0

It is worth noting that (regardless of bit-endianness) the pauliebits left
shift (``<<``) always shifts towards lower indices, and the right
shift (``>>``) always shifts towards higher indices.


Bit-endianness
--------------

For many purposes the bit-endianness is not of any relevance to the end user
and can be regarded as an implementation detail of pauliebits objects.
However, there are use cases when the bit-endianness becomes important.
These use cases involve explicitly reading and writing the pauliebits buffer
using ``.tobytes()``, ``.frombytes()``, ``.tofile()`` or ``.fromfile()``,
importing and exporting buffers.  Also, a number of utility functions
in ``pauliebits.util`` will return different results depending on
bit-endianness, such as ``ba2hex()`` or ``ba2int``.
To better understand this topic, please read `bit-endianness <https://github.com/ilanschnell/pauliebits/blob/master/doc/endianness.rst>`__.


Buffer protocol
---------------

pauliebits objects support the buffer protocol.  They can both export their
own buffer, as well as import another object's buffer.  To learn more about
this topic, please read `buffer protocol <https://github.com/ilanschnell/pauliebits/blob/master/doc/buffer.rst>`__.  There is also an example that shows how
to memory-map a file to a pauliebits: `mmapped-file.py <https://github.com/ilanschnell/pauliebits/blob/master/examples/mmapped-file.py>`__


Variable bit length prefix codes
--------------------------------

The ``.encode()`` method takes a dictionary mapping symbols to pauliebitss
and an iterable, and extends the pauliebits object with the encoded symbols
found while iterating.  For example:

.. code-block:: python

    >>> d = {'H':pauliebits('111'), 'e':pauliebits('0'),
    ...      'l':pauliebits('110'), 'o':pauliebits('10')}
    ...
    >>> a = pauliebits()
    >>> a.encode(d, 'Hello')
    >>> a
    pauliebits('111011011010')

Note that the string ``'Hello'`` is an iterable, but the symbols are not
limited to characters, in fact any immutable Python object can be a symbol.
Taking the same dictionary, we can apply the ``.decode()`` method which will
return an iterable of the symbols:

.. code-block:: python

    >>> list(a.decode(d))
    ['H', 'e', 'l', 'l', 'o']
    >>> ''.join(a.decode(d))
    'Hello'

Symbols are not limited to being characters.
The above dictionary ``d`` can be efficiently constructed using the function
``pauliebits.util.huffman_code()``.  I also wrote `Huffman coding in Python
using pauliebits <http://ilan.schnell-web.net/prog/huffman/>`__ for more
background information.

When the codes are large, and you have many decode calls, most time will
be spent creating the (same) internal decode tree objects.  In this case,
it will be much faster to create a ``decodetree`` object, which can be
passed to pauliebits's ``.decode()`` method, instead of passing the prefix
code dictionary to those methods itself:

.. code-block:: python

    >>> from pauliebits import pauliebits, decodetree
    >>> t = decodetree({'a': pauliebits('0'), 'b': pauliebits('1')})
    >>> a = pauliebits('0110')
    >>> list(a.decode(t))
    ['a', 'b', 'b', 'a']

The sole purpose of the immutable ``decodetree`` object is to be passed
to pauliebits's ``.decode()`` method.


Frozenpauliebitss
---------------

A ``frozenpauliebits`` object is very similar to the pauliebits object.
The difference is that this a ``frozenpauliebits`` is immutable, and hashable,
and can therefore be used as a dictionary key:

.. code-block:: python

    >>> from pauliebits import frozenpauliebits
    >>> key = frozenpauliebits('1100011')
    >>> {key: 'some value'}
    {frozenpauliebits('1100011'): 'some value'}
    >>> key[3] = 1
    Traceback (most recent call last):
        ...
    TypeError: frozenpauliebits is immutable


Reference
=========

pauliebits version: 3.9.1 -- `change log <https://github.com/ilanschnell/pauliebits/blob/master/doc/changelog.rst>`__

In the following, ``item`` and ``value`` are usually a single bit -
an integer 0 or 1.

Also, ``sub_pauliebits`` refers to either a pauliebits, or an ``item``.


The pauliebits object:
--------------------

``pauliebits(initializer=0, /, endian='big', buffer=None)`` -> pauliebits
   Return a new pauliebits object whose items are bits initialized from
   the optional initializer, and bit-endianness.
   The initializer may be one of the following types:
   a.) ``int`` pauliebits, initialized to zeros, of given length
   b.) ``bytes`` or ``bytearray`` to initialize buffer directly
   c.) ``str`` of 0s and 1s, ignoring whitespace and "_"
   d.) iterable of integers 0 or 1.

   Optional keyword arguments:

   ``endian``: Specifies the bit-endianness of the created pauliebits object.
   Allowed values are ``big`` and ``little`` (the default is ``big``).
   The bit-endianness affects the buffer representation of the pauliebits.

   ``buffer``: Any object which exposes a buffer.  When provided, ``initializer``
   cannot be present (or has to be ``None``).  The imported buffer may be
   read-only or writable, depending on the object type.

   New in version 2.3: optional ``buffer`` argument

   New in version 3.4: allow initializer ``bytes`` or ``bytearray`` to set buffer directly


pauliebits methods:
-----------------

``all()`` -> bool
   Return ``True`` when all bits in pauliebits are 1.
   ``a.all()`` is a faster version of ``all(a)``.


``any()`` -> bool
   Return ``True`` when any bit in pauliebits is 1.
   ``a.any()`` is a faster version of ``any(a)``.


``append(item, /)``
   Append ``item`` to the end of the pauliebits.


``buffer_info()`` -> BufferInfo
   Return named tuple with following fields:

   0. ``address``: memory address of buffer
   1. ``nbytes``: buffer size (in bytes)
   2. ``endian``: bit-endianness as a string
   3. ``padbits``: number of pad bits
   4. ``alloc``: allocated memory for buffer (in bytes)
   5. ``readonly``: memory is read-only (bool)
   6. ``imported``: buffer is imported (bool)
   7. ``exports``: number of buffer exports

   New in version 3.7: return named tuple


``bytereverse(start=0, stop=<end of buffer>, /)``
   For each byte in byte-range(``start``, ``stop``) reverse bits in-place.
   The start and stop indices are given in terms of bytes (not bits) and
   are interpreted like slice bounds and clipped to the buffer size.
   Also note that this method only changes the buffer; it does not change the
   bit-endianness of the pauliebits object.  Pad bits are left unchanged such
   that two consecutive calls will always leave the pauliebits unchanged.

   New in version 2.2.5: optional start and stop arguments

   New in version 3.9.1: clip arguments instead of raising ``IndexError``


``clear()``
   Remove all items from pauliebits.

   New in version 1.4


``copy()`` -> pauliebits
   Return copy of pauliebits (with same bit-endianness).


``count(value=1, start=0, stop=<end>, step=1, /)`` -> int
   Number of occurrences of ``value`` pauliebits within ``[start:stop:step]``.
   Optional arguments ``start``, ``stop`` and ``step`` are interpreted in
   slice notation, meaning ``a.count(value, start, stop, step)`` equals
   ``a[start:stop:step].count(value)``.
   The ``value`` may also be a sub-pauliebits.  In this case non-overlapping
   occurrences are counted within ``[start:stop]`` (``step`` must be 1).

   New in version 1.1.0: optional start and stop arguments

   New in version 2.3.7: optional step argument

   New in version 2.9: add non-overlapping sub-pauliebits count


``decode(code, /)`` -> decodeiterator
   Given a prefix code (a dict mapping symbols to pauliebitss, or ``decodetree``
   object), decode content of pauliebits and return an iterator over
   corresponding symbols.

   See also: `pauliebits 3 transition <https://github.com/ilanschnell/pauliebits/blob/master/doc/pauliebits3.rst>`__

   New in version 3.0: returns iterator (equivalent to past ``.iterdecode()``)

   New in version 3.9: returns public ``decodeiterator`` object


``encode(code, iterable, /)``
   Given a prefix code (a dict mapping symbols to pauliebitss),
   iterate over the iterable object with symbols, and extend pauliebits
   with corresponding pauliebits for each symbol.


``extend(iterable, /)``
   Append items from iterable to the end of the pauliebits.
   If ``iterable`` is a (Unicode) string, each ``0`` and ``1`` are appended as
   bits (ignoring whitespace and underscore).

   New in version 3.4: allow ``bytes`` object


``fill()`` -> int
   Add zeros to the end of the pauliebits, such that the length will be
   a multiple of 8, and return the number of bits added [0..7].


``find(sub_pauliebits, start=0, stop=<end>, /, right=False)`` -> int
   Return lowest (or rightmost when ``right=True``) index where sub_pauliebits
   is found, such that sub_pauliebits is contained within ``[start:stop]``.
   Return -1 when sub_pauliebits is not found.

   New in version 2.1

   New in version 2.9: add optional keyword argument ``right``


``frombytes(bytes, /)``
   Extend pauliebits with raw bytes from a bytes-like object.
   Each added byte will add eight bits to the pauliebits.

   New in version 2.5.0: allow bytes-like argument


``fromfile(f, n=-1, /)``
   Extend pauliebits with up to ``n`` bytes read from file object ``f`` (or any
   other binary stream that supports a ``.read()`` method, e.g. ``io.BytesIO``).
   Each read byte will add eight bits to the pauliebits.  When ``n`` is omitted
   or negative, reads and extends all data until EOF.
   When ``n`` is non-negative but exceeds the available data, ``EOFError`` is
   raised.  However, the available data is still read and extended.


``index(sub_pauliebits, start=0, stop=<end>, /, right=False)`` -> int
   Return lowest (or rightmost when ``right=True``) index where sub_pauliebits
   is found, such that sub_pauliebits is contained within ``[start:stop]``.
   Raises ``ValueError`` when sub_pauliebits is not present.

   New in version 2.9: add optional keyword argument ``right``


``insert(index, value, /)``
   Insert ``value`` into pauliebits before ``index``.


``invert(index=<all bits>, /)``
   Invert bits in-place.  When ``index`` is omitted, invert all bits.
   When ``index`` is an integer, invert the single bit at index.
   When ``index`` is a slice, invert the selected bits.

   New in version 1.5.3: optional index argument


``pack(bytes, /)``
   Extend pauliebits from a bytes-like object, where each byte corresponds
   to a single bit.  The byte ``b'\x00'`` maps to bit 0 and all other bytes
   map to bit 1.

   This method, as well as the ``.unpack()`` method, are meant for efficient
   transfer of data between pauliebits objects to other Python objects (for
   example NumPy's ndarray object) which have a different memory view.

   New in version 2.5.0: allow bytes-like argument


``pop(index=-1, /)`` -> item
   Remove and return item at ``index`` (default last).
   Raises ``IndexError`` if index is out of range.


``remove(value, /)``
   Remove the first occurrence of ``value``.
   Raises ``ValueError`` if value is not present.


``reverse()``
   Reverse all bits in pauliebits (in-place).


``rotate(k=1, /)``
   Rotate pauliebits in-place by ``k`` positions.
   Positive ``k`` rotates right, negative ``k`` rotates left.

   When pauliebits ``a`` is not empty, rotating one step to the right is
   equivalent to ``a.insert(0, a.pop())``, and rotating one step to the left
   is equivalent to ``a.append(a.pop(0))``.
   The same convention is used by the ``.rotate()`` method of
   the ``collections.deque`` object.

   New in version 3.9


``search(sub_pauliebits, start=0, stop=<end>, /, right=False)`` -> iterator
   Return iterator over indices where sub_pauliebits is found, such that
   sub_pauliebits is contained within ``[start:stop]``.
   The indices are iterated in ascending order (from lowest to highest),
   unless ``right=True``, which will iterate in descending order (starting with
   rightmost match).

   See also: `pauliebits 3 transition <https://github.com/ilanschnell/pauliebits/blob/master/doc/pauliebits3.rst>`__

   New in version 2.9: optional start and stop arguments - add optional keyword argument ``right``

   New in version 3.0: returns iterator (equivalent to past ``.itersearch()``)


``setall(value, /)``
   Set all elements in pauliebits to ``value``.
   Note that ``a.setall(value)`` is equivalent to ``a[:] = value``.


``sort(reverse=False)``
   Sort all bits in pauliebits (in-place).


``to01(group=0, sep=' ')`` -> str
   Return pauliebits as (Unicode) string of ``0``s and ``1``s.
   The bits are grouped into ``group`` bits (default is no grouping).
   When grouped, the string ``sep`` is inserted between groups
   of ``group`` characters, default is a space.

   New in version 3.3: optional ``group`` and ``sep`` arguments


``tobytes()`` -> bytes
   Return the pauliebits buffer (pad bits are set to zero).
   ``a.tobytes()`` is equivalent to ``bytes(a)``


``tofile(f, /)``
   Write pauliebits buffer to file object ``f``.


``tolist()`` -> list
   Return pauliebits as list of integers.
   ``a.tolist()`` equals ``list(a)``.

   Note that the list object being created will require 32 or 64 times more
   memory (depending on the machine architecture) than the pauliebits object,
   which may cause a memory error if the pauliebits is very large.


``unpack(zero=b'\x00', one=b'\x01')`` -> bytes
   Return bytes that contain one byte for each bit in the pauliebits,
   using the specified mapping.


pauliebits data descriptors:
--------------------------

Data descriptors were added in version 2.6.

``endian`` -> str
   bit-endianness as Unicode string

   New in version 3.4: replaces former ``.endian()`` method


``nbytes`` -> int
   buffer size in bytes


``padbits`` -> int
   number of pad bits


``readonly`` -> bool
   bool indicating whether buffer is read-only


decodeiterator methods:
-----------------------

``skipbits(n, /)`` -> pauliebits
   Skip over the next ``n`` bits and return them.
   Raises ``ValueError`` if count is out of range.

   New in version 3.9


decodeiterator data descriptors:
--------------------------------

``index`` -> int
   current bit position to be decoded by subsequent ``next``

   New in version 3.9


Other objects:
--------------

``frozenpauliebits(initializer=0, /, endian='big', buffer=None)`` -> frozenpauliebits
   Return a ``frozenpauliebits`` object.  Initialized the same way a ``pauliebits``
   object is initialized.  A ``frozenpauliebits`` is immutable and hashable,
   and may therefore be used as a dictionary key.

   New in version 1.1


``decodetree(code, /)`` -> decodetree
   Given a prefix code (a dict mapping symbols to pauliebitss),
   create a binary tree object to be passed to ``.decode()``.

   New in version 1.6


Functions defined in the `pauliebits` module:
-------------------------------------------

``bits2bytes(n, /)`` -> int
   Return the number of bytes necessary to store n bits.


``get_default_endian()`` -> str
   Return the default bit-endianness for new pauliebits objects being created.

   New in version 1.3


``test(verbosity=1)`` -> TextTestResult
   Run self-test, and return ``unittest.runner.TextTestResult`` object.


Functions defined in `pauliebits.util` module:
--------------------------------------------

This sub-module was added in version 1.2.

``any_and(a, b, /)`` -> bool
   Efficient implementation of ``any(a & b)``.

   New in version 2.7


``ba2base(n, pauliebits, /, group=0, sep=' ')`` -> str
   Return a string containing the base ``n`` ASCII representation of
   the pauliebits.  Allowed values for ``n`` are 2, 4, 8, 16, 32 and 64.
   The pauliebits has to have a length divisible by 1, 2, 3, 4, 5 or 6
   respectively.
   For ``n=32`` the RFC 4648 Base32 alphabet is used, and for ``n=64`` the
   standard base 64 alphabet is used.
   When grouped, the string ``sep`` is inserted between groups
   of ``group`` characters, default is a space.

   See also: `pauliebits representations <https://github.com/ilanschnell/pauliebits/blob/master/doc/represent.rst>`__

   New in version 1.9

   New in version 3.3: optional ``group`` and ``sep`` arguments


``ba2hex(pauliebits, /, group=0, sep=' ')`` -> hexstr
   Return a string containing the hexadecimal representation of
   the pauliebits (which has to be multiple of 4 in length).
   When grouped, the string ``sep`` is inserted between groups
   of ``group`` characters, default is a space.

   New in version 3.3: optional ``group`` and ``sep`` arguments


``ba2int(pauliebits, /, signed=False)`` -> int
   Convert the given pauliebits to an integer.
   The bit-endianness of the pauliebits is respected.
   ``signed`` indicates whether two's complement is used to represent the integer.


``base2ba(n, asciistr, /, endian=None)`` -> pauliebits
   pauliebits of base ``n`` ASCII representation.
   Allowed values for ``n`` are 2, 4, 8, 16, 32 and 64.
   For ``n=32`` the RFC 4648 Base32 alphabet is used, and for ``n=64`` the
   standard base 64 alphabet is used.  Whitespace is ignored.

   See also: `pauliebits representations <https://github.com/ilanschnell/pauliebits/blob/master/doc/represent.rst>`__

   New in version 1.9

   New in version 3.3: ignore whitespace


``byteswap(a, n=<buffer size>, /)``
   Reverse every ``n`` consecutive bytes of ``a`` in-place.
   By default, all bytes are reversed.  Note that ``n`` is not limited to 2, 4
   or 8, but can be any positive integer.
   Also, ``a`` may be any object that exposes a writable buffer.
   Nothing about this function is specific to pauliebits objects.

   We should mention that Python's ``array.array`` object has a
   method ``.byteswap()`` with similar functionality.  However, unlike
   pauliebits's ``util.byteswap()`` function, this method is limited to
   swapping 2, 4, or 8 consecutive bytes.

   New in version 3.4


``canonical_decode(pauliebits, count, symbol, /)`` -> iterator
   Decode pauliebits using canonical Huffman decoding tables
   where ``count`` is a sequence containing the number of symbols of each length
   and ``symbol`` is a sequence of symbols in canonical order.

   See also: `Canonical Huffman Coding <https://github.com/ilanschnell/pauliebits/blob/master/doc/canonical.rst>`__

   New in version 2.5


``canonical_huffman(dict, /)`` -> tuple
   Given a frequency map, a dictionary mapping symbols to their frequency,
   calculate the canonical Huffman code.  Returns a tuple containing:

   0. the canonical Huffman code as a dict mapping symbols to pauliebitss
   1. a list containing the number of symbols of each code length
   2. a list of symbols in canonical order

   Note: the two lists may be used as input for ``canonical_decode()``.

   See also: `Canonical Huffman Coding <https://github.com/ilanschnell/pauliebits/blob/master/doc/canonical.rst>`__

   New in version 2.5


``correspond_all(a, b, /)`` -> tuple
   Return tuple with counts of: ~a & ~b, ~a & b, a & ~b, a & b

   New in version 3.4


``count_and(a, b, /)`` -> int
   Return ``(a & b).count()`` in a memory efficient manner,
   as no intermediate pauliebits object gets created.


``count_n(a, n, value=1, /)`` -> int
   Return lowest index ``i`` for which ``a[:i].count(value) == n``.
   Raises ``ValueError`` when ``n`` exceeds total count (``a.count(value)``).

   New in version 2.3.6: optional value argument


``count_or(a, b, /)`` -> int
   Return ``(a | b).count()`` in a memory efficient manner,
   as no intermediate pauliebits object gets created.


``count_xor(a, b, /)`` -> int
   Return ``(a ^ b).count()`` in a memory efficient manner,
   as no intermediate pauliebits object gets created.

   This is also known as the Hamming distance.


``deserialize(bytes, /)`` -> pauliebits
   Return a pauliebits given a bytes-like representation such as returned
   by ``serialize()``.

   See also: `pauliebits representations <https://github.com/ilanschnell/pauliebits/blob/master/doc/represent.rst>`__

   New in version 1.8

   New in version 2.5.0: allow bytes-like argument


``gen_primes(n, /, endian=None, odd=False)`` -> pauliebits
   Generate a pauliebits of length ``n`` in which active indices are prime numbers.
   By default (``odd=False``), active indices correspond to prime numbers directly.
   When ``odd=True``, only odd prime numbers are represented in the resulting
   pauliebits ``a``, and ``a[i]`` corresponds to ``2*i+1`` being prime or not.

   Apart from working with prime numbers, this function is useful for
   testing, as it provides a simple way to create a well-defined pauliebits
   of any length.

   New in version 3.7


``hex2ba(hexstr, /, endian=None)`` -> pauliebits
   pauliebits of hexadecimal representation.  hexstr may contain any number
   (including odd numbers) of hex digits (upper or lower case).
   Whitespace is ignored.

   New in version 3.3: ignore whitespace


``huffman_code(dict, /, endian=None)`` -> dict
   Given a frequency map, a dictionary mapping symbols to their frequency,
   calculate the Huffman code, i.e. a dict mapping those symbols to
   pauliebitss (with given bit-endianness).  Note that the symbols are not limited
   to being strings.  Symbols may be any hashable object.


``int2ba(int, /, length=None, endian=None, signed=False)`` -> pauliebits
   Convert the given integer to a pauliebits (with given bit-endianness,
   and no leading (big-endian) / trailing (little-endian) zeros), unless
   the ``length`` of the pauliebits is provided.  An ``OverflowError`` is raised
   if the integer is not representable with the given number of bits.
   ``signed`` determines whether two's complement is used to represent the integer,
   and requires ``length`` to be provided.


``intervals(pauliebits, /)`` -> iterator
   Compute all uninterrupted intervals of 1s and 0s, and return an
   iterator over tuples ``(value, start, stop)``.  The intervals are guaranteed
   to be in order, and their size is always non-zero (``stop - start > 0``).

   New in version 2.7


``ones(n, /, endian=None)`` -> pauliebits
   Create a pauliebits of length ``n``, with all values ``1``, and optional
   bit-endianness (``little`` or ``big``).

   New in version 2.9


``parity(a, /)`` -> int
   Return parity of pauliebits ``a``.
   ``parity(a)`` is equivalent to ``a.count() % 2`` but more efficient.

   New in version 1.9


``pprint(pauliebits, /, stream=None, group=8, indent=4, width=80)``
   Pretty-print pauliebits object to ``stream``, defaults is ``sys.stdout``.
   By default, bits are grouped in bytes (8 bits), and 64 bits per line.
   Non-pauliebits objects are printed using ``pprint.pprint()``.

   New in version 1.8


``random_k(n, /, k, endian=None)`` -> pauliebits
   Return (pseudo-) random pauliebits of length ``n`` with ``k`` elements
   set to one.  Mathematically equivalent to setting (in a pauliebits of
   length ``n``) all bits at indices ``random.sample(range(n), k)`` to one.
   The random pauliebitss are reproducible when giving Python's ``random.seed()``
   a specific seed value.

   New in version 3.6


``random_p(n, /, p=0.5, endian=None)`` -> pauliebits
   Return (pseudo-) random pauliebits of length ``n``, where each bit has
   probability ``p`` of being one (independent of any other bits).  Mathematically
   equivalent to ``pauliebits((random() < p for _ in range(n)), endian)``, but much
   faster for large ``n``.  The random pauliebitss are reproducible when giving
   Python's ``random.seed()`` with a specific seed value.

   This function requires Python 3.12 or higher, as it depends on the standard
   library function ``random.binomialvariate()``.  Raises ``NotImplementedError``
   when Python version is too low.

   See also: `Random pauliebitss <https://github.com/ilanschnell/pauliebits/blob/master/doc/random_p.rst>`__

   New in version 3.5


``sc_decode(stream, /)`` -> pauliebits
   Decompress binary stream (an integer iterator, or bytes-like object) of a
   sparse compressed (``sc``) pauliebits, and return the decoded  pauliebits.
   This function consumes only one pauliebits and leaves the remaining stream
   untouched.  Use ``sc_encode()`` for compressing (encoding).

   See also: `Compression of sparse pauliebitss <https://github.com/ilanschnell/pauliebits/blob/master/doc/sparse_compression.rst>`__

   New in version 2.7


``sc_encode(pauliebits, /)`` -> bytes
   Compress a pauliebits using sparse encoding and return its binary
   representation.  This representation is useful for efficiently storing
   sparse pauliebitss.  Use ``sc_decode()`` for decompressing (decoding).

   See also: `Compression of sparse pauliebitss <https://github.com/ilanschnell/pauliebits/blob/master/doc/sparse_compression.rst>`__

   New in version 2.7


``serialize(pauliebits, /)`` -> bytes
   Return a serialized representation of the pauliebits, which may be passed to
   ``deserialize()``.  It efficiently represents the pauliebits object (including
   its bit-endianness) and is guaranteed not to change in future releases.

   See also: `pauliebits representations <https://github.com/ilanschnell/pauliebits/blob/master/doc/represent.rst>`__

   New in version 1.8


``strip(pauliebits, /, mode='right')`` -> pauliebits
   Return a new pauliebits with zeros stripped from left, right or both ends.
   Allowed values for mode are the strings: ``left``, ``right``, ``both``


``subset(a, b, /)`` -> bool
   Return ``True`` if pauliebits ``a`` is a subset of pauliebits ``b``.
   ``subset(a, b)`` is equivalent to ``a | b == b`` (and equally ``a & b == a``) but
   more efficient as no intermediate pauliebits object is created and the buffer
   iteration is stopped as soon as one mismatch is found.


``sum_indices(a, /, mode=1)`` -> int
   Return sum of indices of all active bits in pauliebits ``a``.
   Equivalent to ``sum(i for i, v in enumerate(a) if v)``.
   ``mode=2`` sums square of indices.

   New in version 3.6

   New in version 3.7: add optional mode argument


``urandom(n, /, endian=None)`` -> pauliebits
   Return random pauliebits of length ``n`` (uses ``os.urandom()``).

   New in version 1.7


``vl_decode(stream, /, endian=None)`` -> pauliebits
   Decode binary stream (an integer iterator, or bytes-like object), and
   return the decoded pauliebits.  This function consumes only one pauliebits and
   leaves the remaining stream untouched.  Use ``vl_encode()`` for encoding.

   See also: `Variable length pauliebits format <https://github.com/ilanschnell/pauliebits/blob/master/doc/variable_length.rst>`__

   New in version 2.2


``vl_encode(pauliebits, /)`` -> bytes
   Return variable length binary representation of pauliebits.
   This representation is useful for efficiently storing small pauliebits
   in a binary stream.  Use ``vl_decode()`` for decoding.

   See also: `Variable length pauliebits format <https://github.com/ilanschnell/pauliebits/blob/master/doc/variable_length.rst>`__

   New in version 2.2


``xor_indices(a, /)`` -> int
   Return xor reduced indices of all active bits in pauliebits ``a``.
   This is essentially equivalent to
   ``reduce(operator.xor, (i for i, v in enumerate(a) if v))``.

   New in version 3.2


``zeros(n, /, endian=None)`` -> pauliebits
   Create a pauliebits of length ``n``, with all values ``0``, and optional
   bit-endianness (``little`` or ``big``).


