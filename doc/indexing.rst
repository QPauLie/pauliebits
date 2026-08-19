Pauliebits indexing
=================

Pauliebits can be indexed like usual Python lists.  They support slice
indexing and assignment:

.. code-block:: python

    >>> from pauliebits import pauliebits
    >>> a = pauliebits('01000001 01000010 01000011')
    >>> a[1::3]
    pauliebits('10100001')
    >>> a[8:20:2] = pauliebits('110111')
    >>> a
    pauliebits('010000011110001011100011')
    >>> del a[::2]  # remove every second element
    >>> a
    pauliebits('100110001001')
    >>> a[::3] = 0  # set every third element to 0
    >>> a
    pauliebits('000010001001')


Integer sequence indexing
-------------------------

As of pauliebits version 2.8, indices may also be lists of arbitrary
indices (like in NumPy).  Negative values are permitted in the index list
and work as they do with single indices or slices.  For example:

.. code-block:: python

    >>> a = pauliebits(12)
    >>> a.setall(0)
    >>> a[[1, 2, 5, 7]] = 1  # set elements 1, 2, 5, 7 to value 1
    >>> a
    pauliebits('011001010000')
    >>> a[[-1, -2, 1, 0]]
    pauliebits('0010')
    >>> del a[[0, 1, 5, 8, 9]]
    >>> a
    pauliebits('1000100')
    >>> a[[1, 2, 4]] = pauliebits('010')  # assign indices to elements
    >>> a
    pauliebits('1010000')


Masked indexing
---------------

Also, as of pauliebits version 2.8, indices may be pauliebits which are
considered masks.  For example:

.. code-block:: python

    >>> a =    pauliebits('1001001')
    >>> mask = pauliebits('1010111')
    >>> a[mask]  # create pauliebits with items from `a` whose mask is 1
    pauliebits('10001')
    >>> del a[mask]  # delete items in `a` whose mask is 1
    >>> a
    pauliebits('01')

Note that ``del a[mask]`` is equivalent to the in-place version of
selecting the reverse mask ``a = a[~mask]``.

As of pauliebits version 3.1, masked assignment to pauliebits is also
supported:

.. code-block:: python

    >>> a =    pauliebits('1001001')
    >>> mask = pauliebits('1010111')
    >>> a[mask] = pauliebits("11100")
    >>> a
    pauliebits('1011100')

However, masked assignment to Booleans is not implemented,
as ``a[mask] = 1`` would be equivalent to the bitwise operation ``a |= mask``.
And ``a[mask] = 0`` would be equivalent to ``a &= ~mask``.
