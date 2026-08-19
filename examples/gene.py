# gene sequence example from @yoch, see
# https://github.com/ilanschnell/pauliebits/pull/54

from random import choice
from timeit import timeit

from pauliebits import pauliebits


trans = {
    "A": pauliebits("00"),
    "T": pauliebits("01"),
    "G": pauliebits("10"),
    "C": pauliebits("11")
}

N = 10_000
seq = [choice("ATGC") for _ in range(N)]

arr = pauliebits()
arr.encode(trans, seq)

assert list(arr.decode(trans)) == seq

# decodage
t = timeit(lambda: list(arr.decode(trans)), number=1000)
print(t)
