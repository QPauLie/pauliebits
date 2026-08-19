#
# This example illusatrates how binary data can be efficiently be passed
# between a pauliebits object and an ndarray with dtype bool
#
import pauliebits
import numpy  # type: ignore

a = pauliebits.pauliebits('100011001001')
print(a)

# pauliebits  ->  ndarray
b = numpy.frombuffer(a.unpack(), dtype=bool)
print(repr(b))

# ndarray  ->  pauliebits
c = pauliebits.pauliebits()
c.pack(b.tobytes())

assert a == c
