from time import perf_counter

from pauliebits import pauliebits
from pauliebits.util import urandom, ba2hex, hex2ba


# ----- conversion using prefix codes

CODEDICT = {'little': {}, 'big': {
    '0': pauliebits('0000'),    '1': pauliebits('0001'),
    '2': pauliebits('0010'),    '3': pauliebits('0011'),
    '4': pauliebits('0100'),    '5': pauliebits('0101'),
    '6': pauliebits('0110'),    '7': pauliebits('0111'),
    '8': pauliebits('1000'),    '9': pauliebits('1001'),
    'a': pauliebits('1010'),    'b': pauliebits('1011'),
    'c': pauliebits('1100'),    'd': pauliebits('1101'),
    'e': pauliebits('1110'),    'f': pauliebits('1111'),
}}
for k, v in CODEDICT['big'].items(): # type: ignore
    CODEDICT['little'][k] = v[::-1]  # type: ignore

def prefix_ba2hex(a):
    return ''.join(a.decode(CODEDICT[a.endian]))

def prefix_hex2ba(s, endian=None):
    a = pauliebits(0, endian)
    a.encode(CODEDICT[a.endian], s)
    return a

# ----- test

def test_round(f, g, n, endian):
    # f: function which takes pauliebits and returns hexstr
    # g: function which takes hexstr and returns pauliebits
    # n: size of random pauliebits
    a = urandom(n, endian)
    t0 = perf_counter()
    s = f(a)
    print('%s:  %6.3f ms' % (f.__name__, 1000.0 * (perf_counter() - t0)))
    t0 = perf_counter()
    b = g(s, endian)
    print('%s:  %6.3f ms' % (g.__name__, 1000.0 * (perf_counter() - t0)))
    assert b == a

if __name__ == '__main__':
    n = 100_000_004
    for endian in 'little', 'big':
        print('%s-endian:' % endian)
        for f in ba2hex, prefix_ba2hex:
            for g in hex2ba, prefix_hex2ba:
                test_round(f, g, n, endian)
        print()
