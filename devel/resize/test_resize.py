import unittest

from pauliebits import pauliebits


PATTERN = [0, 1, 4, 8, 16, 24, 32, 40, 48, 56, 64, 76, 88, 100, 112, 124, 136]

def get_alloc(a):
    info = a.buffer_info()
    return info.alloc

def resize(a, n):
    increase = n - len(a)
    if increase > 0:
        a.extend(pauliebits(increase))
    elif increase < 0:
        del a[n:]

def show(a):
    info = a.buffer_info()
    print('%d  %d' % (info.nbytes, info.alloc))


class ResizeTests(unittest.TestCase):

    def test_pattern(self):
        pat = []
        a = pauliebits()
        prev = -1
        while len(a) < 1000:
            alloc = get_alloc(a)
            if prev != alloc:
                pat.append(alloc)
            prev = alloc
            a.append(0)
        self.assertEqual(pat, PATTERN)

    def test_increase(self):
        # make sure sequence of appends will always increase allocated size
        a = pauliebits()
        prev = -1
        while len(a) < 100_000:
            alloc = get_alloc(a)
            self.assertTrue(prev <= alloc)
            prev = alloc
            a.append(1)

    def test_decrease(self):
        # ensure that when we start from a large array and delete part, we
        # always get a decreasing allocation
        a = pauliebits(10_000_000)
        prev = get_alloc(a)
        while a:
            del a[-100_000:]
            alloc = get_alloc(a)
            self.assertTrue(alloc <= prev)
            prev = alloc

    def test_no_overalloc(self):
        # initalizing a pauliebits does not overallocate
        for n in range(1000):
            blob = n * b'A'
            for a in [
                    pauliebits(8 * n),
                    pauliebits(8 * n * [1]),
                    pauliebits(pauliebits(8 * n)),
                    pauliebits(n * "00001111"),
                    pauliebits(blob),
                    pauliebits(bytearray(blob)),
            ]:
                self.assertEqual(len(a), 8 * n)
                self.assertEqual(get_alloc(a), n)

    def test_no_overalloc_large(self):
        # starting from a large pauliebits, make we sure we don't realloc each
        # time we extend
        a = pauliebits(1_000_000)  # no overallocation
        self.assertEqual(get_alloc(a), 125_000)
        a.extend(pauliebits(8))  # overallocation happens here
        alloc = get_alloc(a)
        for _ in range(1000):
            a.extend(pauliebits(8))
            self.assertEqual(get_alloc(a), alloc)

if __name__ == '__main__':
    unittest.main()
