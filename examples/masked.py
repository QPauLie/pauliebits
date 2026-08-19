# example to illustrate masked indexing
from pauliebits import pauliebits


a = pauliebits('1110000')
b = pauliebits('1100110')
# select bits from a where b is 1
assert a[b] == pauliebits('1100')

# set bits in a where b is 1
a[b] = pauliebits('1010')
assert a == pauliebits('1010100')

# delete bits in a where b is 1
del a[b]
assert a == pauliebits('100')
print("Ok")
