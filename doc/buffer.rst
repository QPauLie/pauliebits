Buffer protocol
===============

Pauliebits objects support the buffer protocol.  They can both export their
own buffer, as well as import another object's buffer.


Exporting buffers
-----------------

Here is an example where the pauliebits's buffer is exported:

.. code-block:: python

    >>> from pauliebits import pauliebits
    >>> a = pauliebits('01000001 01000010 01000011', endian='big')
    >>> v = memoryview(a)
    >>> v.tobytes()
    b'ABC'
    >>> v[1] = 255
    >>> a
    pauliebits('010000011111111101000011')

Note that it is possible to change the shared buffer from both ``a`` and ``v``:

.. code-block:: python

    >>> a[6] = 1
    >>> v.tobytes()
    b'C\xffC'

However, as ``a``'s buffer is shared, it is not possible to resize it:

.. code-block:: python

    >>> a.append(0)
    Traceback (most recent call last):
        ...
    BufferError: cannot resize pauliebits that is exporting buffers

When exporting the buffer of a ``frozenpauliebits``, it is not possible to
change its ``memoryview`` either:

.. code-block:: python

    >>> from pauliebits import frozenpauliebits
    >>> a = frozenpauliebits('01000001 01000010')
    >>> v = memoryview(a)
    >>> v.readonly
    True
    >>> v[0] = 15
    Traceback (most recent call last):
        ...
    TypeError: cannot modify read-only memory


Importing buffers
-----------------

As of pauliebits version 2.3, it is also possible to import the buffer
from an object that exposes its buffer.  Here a ``bytearray`` object:

.. code-block:: python

    >>> c = bytearray([0x41, 0xff, 0x01])
    >>> a = pauliebits(buffer=c, endian='big')
    >>> a
    pauliebits('010000011111111100000001')
    >>> a <<= 3  # shift all bits by 3 to the left
    >>> c
    bytearray(b'\x0f\xf8\x08')
    >>> a[20:] = 1
    >>> a
    pauliebits('000011111111100000001111')

Again, the shared buffer can be represented and modified by either object
``a`` or ``c``.  When importing a buffer into a pauliebits, the length of the
pauliebits will always be a multiple of 8 bits, as buffers are based on bytes.
Also, we may specify the endianness of the pauliebits:

.. code-block:: python

   >>> b = pauliebits(buffer=c, endian='little')
   >>> b
   pauliebits('111100000001111111110000')

The bytearray ``c`` is now exporting its buffer twice:
to big-endian pauliebits ``a``, and a little-endian pauliebits ``b``.
At this point all three objects ``a``, ``b`` and ``c`` share the same buffer.
Using the ``.buffer_info()`` method, we can actually verify that the
pauliebits ``a`` and ``b`` point to the same address:

.. code-block:: python

    >>> def address(a):
    ...     info = a.buffer_info()
    ...     return info[0]  # using pauliebits 3.7, we can also: info.address
    >>> assert address(a) == address(b)

As pauliebits expose their buffer, we can also directly create a pauliebits
which imports the buffer from another pauliebits:

.. code-block:: python

    >>> a = pauliebits(32)
    >>> b = pauliebits(buffer=a)
    >>> # the buffer address is the same
    >>> assert address(a) == address(b)
    >>> a.setall(0)
    >>> assert a == b
    >>> b[::7] = 1
    >>> assert a == b
    >>> a
    pauliebits('10000001000000100000010000001000')

We can also create pauliebits which share part of the buffer.  Let's create
a large pauliebits ``a``, and then have ``b`` and ``c`` share different portions
of ``a``'s buffer:

.. code-block:: python

    >>> a = pauliebits(1 << 23)
    >>> a.setall(0)
    >>> b = pauliebits(buffer=memoryview(a)[0x10000:0x30000])
    >>> assert address(a) + 0x10000 == address(b)
    >>> c = pauliebits(buffer=memoryview(a)[0x20000:0x50000])
    >>> assert address(a) + 0x20000 == address(c)
    >>> c[0] = 1
    >>> assert b[8 * 0x10000] == 1
    >>> assert a[8 * 0x20000] == 1

Finally, importing buffers allows creating pauliebits that are memory mapped
to a file.  Please see the `mmapped-file.py <../examples/mmapped-file.py>`__
example.
