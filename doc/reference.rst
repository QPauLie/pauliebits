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
   Given a prefix code (a dict mapping symbols to pauliebits, or ``decodetree``
   object), decode content of pauliebits and return an iterator over
   corresponding symbols.

   See also: `Pauliebits 3 transition <https://github.com/ilanschnell/pauliebits/blob/master/doc/pauliebits3.rst>`__

   New in version 3.0: returns iterator (equivalent to past ``.iterdecode()``)

   New in version 3.9: returns public ``decodeiterator`` object


``encode(code, iterable, /)``
   Given a prefix code (a dict mapping symbols to pauliebits),
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

   See also: `Pauliebits 3 transition <https://github.com/ilanschnell/pauliebits/blob/master/doc/pauliebits3.rst>`__

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
   Given a prefix code (a dict mapping symbols to pauliebits),
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

   See also: `Pauliebits representations <https://github.com/ilanschnell/pauliebits/blob/master/doc/represent.rst>`__

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
   Pauliebits of base ``n`` ASCII representation.
   Allowed values for ``n`` are 2, 4, 8, 16, 32 and 64.
   For ``n=32`` the RFC 4648 Base32 alphabet is used, and for ``n=64`` the
   standard base 64 alphabet is used.  Whitespace is ignored.

   See also: `Pauliebits representations <https://github.com/ilanschnell/pauliebits/blob/master/doc/represent.rst>`__

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

   0. the canonical Huffman code as a dict mapping symbols to pauliebits
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

   See also: `Pauliebits representations <https://github.com/ilanschnell/pauliebits/blob/master/doc/represent.rst>`__

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
   Pauliebits of hexadecimal representation.  hexstr may contain any number
   (including odd numbers) of hex digits (upper or lower case).
   Whitespace is ignored.

   New in version 3.3: ignore whitespace


``huffman_code(dict, /, endian=None)`` -> dict
   Given a frequency map, a dictionary mapping symbols to their frequency,
   calculate the Huffman code, i.e. a dict mapping those symbols to
   pauliebits (with given bit-endianness).  Note that the symbols are not limited
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
   The random pauliebits are reproducible when giving Python's ``random.seed()``
   a specific seed value.

   New in version 3.6


``random_p(n, /, p=0.5, endian=None)`` -> pauliebits
   Return (pseudo-) random pauliebits of length ``n``, where each bit has
   probability ``p`` of being one (independent of any other bits).  Mathematically
   equivalent to ``pauliebits((random() < p for _ in range(n)), endian)``, but much
   faster for large ``n``.  The random pauliebits are reproducible when giving
   Python's ``random.seed()`` with a specific seed value.

   This function requires Python 3.12 or higher, as it depends on the standard
   library function ``random.binomialvariate()``.  Raises ``NotImplementedError``
   when Python version is too low.

   See also: `Random pauliebits <https://github.com/ilanschnell/pauliebits/blob/master/doc/random_p.rst>`__

   New in version 3.5


``sc_decode(stream, /)`` -> pauliebits
   Decompress binary stream (an integer iterator, or bytes-like object) of a
   sparse compressed (``sc``) pauliebits, and return the decoded  pauliebits.
   This function consumes only one pauliebits and leaves the remaining stream
   untouched.  Use ``sc_encode()`` for compressing (encoding).

   See also: `Compression of sparse pauliebits <https://github.com/ilanschnell/pauliebits/blob/master/doc/sparse_compression.rst>`__

   New in version 2.7


``sc_encode(pauliebits, /)`` -> bytes
   Compress a pauliebits using sparse encoding and return its binary
   representation.  This representation is useful for efficiently storing
   sparse pauliebits.  Use ``sc_decode()`` for decompressing (decoding).

   See also: `Compression of sparse pauliebits <https://github.com/ilanschnell/pauliebits/blob/master/doc/sparse_compression.rst>`__

   New in version 2.7


``serialize(pauliebits, /)`` -> bytes
   Return a serialized representation of the pauliebits, which may be passed to
   ``deserialize()``.  It efficiently represents the pauliebits object (including
   its bit-endianness) and is guaranteed not to change in future releases.

   See also: `Pauliebits representations <https://github.com/ilanschnell/pauliebits/blob/master/doc/represent.rst>`__

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


