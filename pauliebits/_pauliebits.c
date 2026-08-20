/*
   Copyright (c) 2008 - 2026, Ilan Schnell; All Rights Reserved
   pauliebits is published under the PSF license.

   This file is the C part of the pauliebits package.
   All functionality of the pauliebits object is implemented here.

   Author: Ilan Schnell
*/

#define PY_SSIZE_T_CLEAN
#include "Python.h"
#include "pythoncapi_compat.h"
#include "structmember.h"
#include "pauliebits.h"
#include <string.h>  // Для memcpy

/* size used when reading / writing blocks from files (in bytes) */
#define BLOCKSIZE  65536

/* translation table - setup during module initialization */
static char reverse_trans[256];

static PyTypeObject Pauliebits_Type;

#define pauliebits_Check(obj)  PyObject_TypeCheck((obj), &Pauliebits_Type)


static size_t
new_allocation(size_t size, size_t allocated, size_t newsize)
{
    assert(allocated >= size);
    assert(newsize > 0);

    if (allocated >= newsize) {
        /* current buffer is large enough to host the requested size */
        if (newsize >= allocated / 2)
            return allocated;  /* minor downsize - keep current allocation */

        return newsize;  /* major downsize - shrink to exact size */
    }
    else {
          /* need to grow buffer */
          size_t new_alloc = newsize;
          /* overallocate when previous size isn't zero and when growth
             is moderate */
          if (size != 0 && newsize / 2 <= allocated) {
              /* overallocate proportional to the pauliebits size and
                 add padding to make the allocated size multiple of 4 */
              new_alloc += (newsize >> 4) + ((newsize < 8) ? 3 : 7);
              new_alloc &= ~(size_t) 3;
          }
          return new_alloc;
    }
}

static int
resize(pauliebitsobject *self, Py_ssize_t nbits)
{
    const size_t size = Py_SIZE(self);
    const size_t allocated = self->allocated;
    const size_t newsize = BYTES((size_t) nbits);
    size_t new_allocated;

    if (self->ob_exports > 0) {
        PyErr_SetString(PyExc_BufferError,
                        "cannot resize pauliebits that is exporting buffers");
        return -1;
    }

    if (self->buffer) {
        PyErr_SetString(PyExc_BufferError, "cannot resize imported buffer");
        return -1;
    }

    if (nbits < 0) {
        PyErr_Format(PyExc_OverflowError, "pauliebits resize %zd", nbits);
        return -1;
    }

    assert(allocated >= size && size == BYTES((size_t) self->nbits));
    /* ob_item == NULL implies ob_size == allocated == 0 */
    assert(self->ob_item != NULL || (size == 0 && allocated == 0));
    /* resize() is never called on read-only memory */
    assert(self->readonly == 0);

    /* bypass everything when buffer size hasn't changed */
    if (newsize == size) {
        self->nbits = nbits;
        return 0;
    }

    if (newsize == 0) {
        PyMem_Free(self->ob_item);
        self->ob_item = NULL;
        Py_SET_SIZE(self, 0);
        self->allocated = 0;
        self->nbits = 0;
        return 0;
    }

    new_allocated = new_allocation(size, allocated, newsize);

    if (new_allocated == allocated) {
        /* bypass reallocation */
        Py_SET_SIZE(self, newsize);
        self->nbits = nbits;
        return 0;
    }

    assert(new_allocated >= newsize);
    self->ob_item = PyMem_Realloc(self->ob_item, new_allocated);
    if (self->ob_item == NULL) {
        Py_SET_SIZE(self, 0);
        self->allocated = 0;
        self->nbits = 0;
        PyErr_NoMemory();
        return -1;
    }
    Py_SET_SIZE(self, newsize);
    self->allocated = new_allocated;
    self->nbits = nbits;
    return 0;
}

/* create new pauliebits object without initialization of buffer */
static pauliebitsobject *
newpauliebitsobject(PyTypeObject *type, Py_ssize_t nbits, int endian)
{
    const size_t nbytes = BYTES((size_t) nbits);
    pauliebitsobject *obj;

    assert(nbits >= 0);

    obj = (pauliebitsobject *) type->tp_alloc(type, 0);
    if (obj == NULL)
        return NULL;

    if (nbytes == 0) {
        obj->ob_item = NULL;
    }
    else {
        /* allocate exact size */
        obj->ob_item = (char *) PyMem_Malloc(nbytes);
        if (obj->ob_item == NULL) {
            PyObject_Del(obj);
            PyErr_NoMemory();
            return NULL;
        }
    }
    Py_SET_SIZE(obj, nbytes);
    obj->allocated = nbytes;  /* no overallocation */
    obj->nbits = nbits;
    obj->endian = endian;
    obj->ob_exports = 0;
    obj->weakreflist = NULL;
    obj->buffer = NULL;
    obj->readonly = 0;
    return obj;
}

/* return new copy of pauliebits object self */
static pauliebitsobject *
pauliebits_cp(pauliebitsobject *self)
{
    pauliebitsobject *res;

    res = newpauliebitsobject(Py_TYPE(self), self->nbits, self->endian);
    if (res == NULL)
        return NULL;
    if (Py_SIZE(self))
        memcpy(res->ob_item, self->ob_item, (size_t) Py_SIZE(self));
    return res;
}

static void
pauliebits_dealloc(pauliebitsobject *self)
{
    if (self->weakreflist)
        PyObject_ClearWeakRefs((PyObject *) self);

    if (self->buffer) {
        PyBuffer_Release(self->buffer);
        PyMem_Free(self->buffer);
    }
    else if (self->ob_item) {
        /* only free object's buffer - imported buffers cannot be freed */
        assert(self->buffer == NULL);
        PyMem_Free((void *) self->ob_item);
    }

    Py_TYPE(self)->tp_free((PyObject *) self);
}

/* return 1 when buffers overlap, 0 otherwise */
static int
buffers_overlap(pauliebitsobject *self, pauliebitsobject *other)
{
    if (Py_SIZE(self) == 0 || Py_SIZE(other) == 0)
        return 0;

/* is pointer ptr in buffer of pauliebits a ? */
#define PIB(a, ptr)  (a->ob_item <= ptr && ptr < a->ob_item + Py_SIZE(a))
    return PIB(self, other->ob_item) || PIB(other, self->ob_item);
#undef PIB
}

/* reverse bits in first n characters of p */
static void
bytereverse(char *p, Py_ssize_t n)
{
    assert(n >= 0);
    while (n--) {
        *p = reverse_trans[(unsigned char) *p];
        p++;
    }
}

/* The following two functions operate on first n bytes in buffer.
   Within this region, they shift all bits by k positions to right,
   i.e. towards higher addresses.
   They operate on little-endian and big-endian pauliebits respectively.
   As we shift right, we need to start with the highest address and loop
   downwards such that lower bytes are still unaltered.
   See also devel/shift_r8.c
*/
static void
shift_r8le(unsigned char *buff, Py_ssize_t n, int k)
{
    Py_ssize_t w = 0;

#if HAVE_BUILTIN_BSWAP64 || PY_LITTLE_ENDIAN   /* use shift word */
    w = n / 8;                    /* number of words used for shifting */
    n %= 8;                       /* number of remaining bytes */
#endif
    while (n--) {                 /* shift in byte-range(8 * w, n) */
        Py_ssize_t i = n + 8 * w;
        buff[i] <<= k;            /* shift byte */
        if (n || w)               /* add shifted next lower byte */
            buff[i] |= buff[i - 1] >> (8 - k);
    }
    assert(w == 0 || ((uintptr_t) buff) % 4 == 0);
    while (w--) {                 /* shift in word-range(0, w) */
        uint64_t *p = ((uint64_t *) buff) + w;
#if HAVE_BUILTIN_BSWAP64 && PY_BIG_ENDIAN
        *p = builtin_bswap64(*p);
        *p <<= k;
        *p = builtin_bswap64(*p);
#else
        *p <<= k;
#endif
        if (w)                    /* add shifted byte from next lower word */
            buff[8 * w] |= buff[8 * w - 1] >> (8 - k);
    }
}

static void
shift_r8be(unsigned char *buff, Py_ssize_t n, int k)
{
    Py_ssize_t w = 0;

#if HAVE_BUILTIN_BSWAP64 || PY_BIG_ENDIAN      /* use shift word */
    w = n / 8;                    /* number of words used for shifting */
    n %= 8;                       /* number of remaining bytes */
#endif
    while (n--) {                 /* shift in byte-range(8 * w, n) */
        Py_ssize_t i = n + 8 * w;
        buff[i] >>= k;            /* shift byte */
        if (n || w)               /* add shifted next lower byte */
            buff[i] |= buff[i - 1] << (8 - k);
    }
    assert(w == 0 || ((uintptr_t) buff) % 4 == 0);
    while (w--) {                 /* shift in word-range(0, w) */
        uint64_t *p = ((uint64_t *) buff) + w;
#if HAVE_BUILTIN_BSWAP64 && PY_LITTLE_ENDIAN
        *p = builtin_bswap64(*p);
        *p >>= k;
        *p = builtin_bswap64(*p);
#else
        *p >>= k;
#endif
        if (w)                    /* add shifted byte from next lower word */
            buff[8 * w] |= buff[8 * w - 1] << (8 - k);
    }
}

/* shift bits in byte-range(a, b) by k bits to right */
static void
shift_r8(pauliebitsobject *self, Py_ssize_t a, Py_ssize_t b, int k)
{
    unsigned char *buff = (unsigned char *) self->ob_item + a;
    Py_ssize_t n = b - a;       /* number of bytes to be shifted */
    Py_ssize_t s = 0;           /* distance to next aligned pointer */

    assert(0 <= k && k < 8);
    assert(0 <= a && a <= Py_SIZE(self));
    assert(0 <= b && b <= Py_SIZE(self));
    assert(self->readonly == 0);
    if (k == 0 || n <= 0)
        return;

    if (n >= 8) {
        s = to_aligned((void *) buff);
        buff += s;  /* align pointer for casting to (uint64_t *) */
        n -= s;
    }

    if (IS_LE(self)) {          /* little endian */
        shift_r8le(buff, n, k);
        if (s) {
            buff[0] |= buff[-1] >> (8 - k);
            shift_r8le(buff - s, s, k);
        }
    }
    else {                      /* big endian */
        shift_r8be(buff, n, k);
        if (s) {
            buff[0] |= buff[-1] << (8 - k);
            shift_r8be(buff - s, s, k);
        }
    }
}

/* Copy n bits from other (starting at b) onto self (starting at a).
   Please see devel/copy_n.py for more details.

   Notes:
     - self and other may have opposite bit-endianness
     - other may equal self - copy a section of self onto itself
     - when other and self are distinct objects, their buffers
       may not overlap
*/
static void
copy_n(pauliebitsobject *self, Py_ssize_t a,
       pauliebitsobject *other, Py_ssize_t b, Py_ssize_t n)
{
    Py_ssize_t p3 = b / 8, i;
    int sa = a % 8, sb = -(b % 8);
    char t3 = 0;  /* silence uninitialized warning on some compilers */

    assert(0 <= n && n <= self->nbits && n <= other->nbits);
    assert(0 <= a && a <= self->nbits - n);
    assert(0 <= b && b <= other->nbits - n);
    assert(self == other || !buffers_overlap(self, other));
    assert(self->readonly == 0);
    if (n == 0 || (self == other && a == b))
        return;

    if (sa + sb < 0) {
        t3 = other->ob_item[p3++];
        sb += 8;
    }
    if (n > sb) {
        const Py_ssize_t p1 = a / 8, p2 = (a + n - 1) / 8, m = BYTES(n - sb);
        const char *table = ones_table[IS_BE(self)];
        char *cp1 = self->ob_item + p1, m1 = table[sa];
        char *cp2 = self->ob_item + p2, m2 = table[(a + n) % 8];
        char t1 = *cp1, t2 = *cp2;

        assert(p1 + m <= Py_SIZE(self) && p3 + m <= Py_SIZE(other));
        memmove(cp1, other->ob_item + p3, (size_t) m);
        if (self->endian != other->endian)
            bytereverse(cp1, m);

        shift_r8(self, p1, p2 + 1, sa + sb);
        if (m1)
            *cp1 = (*cp1 & ~m1) | (t1 & m1);     /* restore bits at p1 */
        if (m2)
            *cp2 = (*cp2 & m2) | (t2 & ~m2);     /* restore bits at p2 */
    }
    for (i = 0; i < sb && i < n; i++)            /* copy first sb bits */
        setbit(self, i + a, t3 & BITMASK(other, i + b));
}

/* starting at start, delete n bits from self */
static int
delete_n(pauliebitsobject *self, Py_ssize_t start, Py_ssize_t n)
{
    const Py_ssize_t nbits = self->nbits;

    assert(0 <= start && start <= nbits);
    assert(0 <= n && n <= nbits - start);
    /* start == nbits implies n == 0 */
    assert(start != nbits || n == 0);

    copy_n(self, start, self, start + n, nbits - start - n);
    return resize(self, nbits - n);
}

/* starting at start, insert n (uninitialized) bits into self */
static int
insert_n(pauliebitsobject *self, Py_ssize_t start, Py_ssize_t n)
{
    const Py_ssize_t nbits = self->nbits;

    assert(0 <= start && start <= nbits);
    assert(n >= 0);

    if (resize(self, nbits + n) < 0)
        return -1;
    copy_n(self, start + n, self, start, nbits - start);
    return 0;
}

/* repeat self m times (negative m is treated as 0) */
static int
repeat(pauliebitsobject *self, Py_ssize_t m)
{
    Py_ssize_t q, k = self->nbits;

    assert(self->readonly == 0);
    if (k == 0 || m == 1)       /* nothing to do */
        return 0;

    if (m <= 0)                 /* clear */
        return resize(self, 0);

    assert(m > 1 && k > 0);
    if (k >= PY_SSIZE_T_MAX / m) {
        PyErr_Format(PyExc_OverflowError,
                     "cannot repeat pauliebits (of size %zd) %zd times", k, m);
        return -1;
    }
    q = k * m;  /* number of resulting bits */
    if (resize(self, q) < 0)
        return -1;

    /* k: number of bits which have been copied so far */
    while (k <= q / 2) {        /* double copies */
        copy_n(self, k, self, 0, k);
        k *= 2;
    }
    assert(q / 2 < k && k <= q);

    copy_n(self, k, self, 0, q - k);  /* copy remaining bits */
    return 0;
}

/* the following functions xyz_span, xyz_range operate on pauliebits items:
     - xyz_span: contiguous bits - self[a:b] (step=1)
     - xyz_range: self[start:stop:step]      (step > 0 is required)
 */

/* invert bits self[a:b] in-place */
static void
invert_span(pauliebitsobject *self, Py_ssize_t a, Py_ssize_t b)
{
    const Py_ssize_t n = b - a;  /* number of bits to invert */
    Py_ssize_t i;

    assert(0 <= a && a <= self->nbits);
    assert(0 <= b && b <= self->nbits);
    assert(self->readonly == 0);

    if (n >= 64) {
        const Py_ssize_t wa = (a + 63) / 64;  /* word-range(wa, wb) */
        const Py_ssize_t wb = b / 64;
        uint64_t *wbuff = WBUFF(self);

        invert_span(self, a, 64 * wa);
        for (i = wa; i < wb; i++)
            wbuff[i] = ~wbuff[i];
        invert_span(self, 64 * wb, b);
    }
    else if (n >= 8) {
        const Py_ssize_t ca = BYTES(a);       /* char-range(ca, cb) */
        const Py_ssize_t cb = b / 8;
        char *buff = self->ob_item;

        invert_span(self, a, 8 * ca);
        for (i = ca; i < cb; i++)
            buff[i] = ~buff[i];
        invert_span(self, 8 * cb, b);
    }
    else {                                    /* (bit-) range(a, b) */
        for (i = a; i < b; i++)
            self->ob_item[i / 8] ^= BITMASK(self, i);
    }
}

/* invert bits self[start:stop:step] in-place */
static void
invert_range(pauliebitsobject *self,
             Py_ssize_t start, Py_ssize_t stop, Py_ssize_t step)
{
    assert(step > 0);

    if (step == 1) {
        invert_span(self, start, stop);
    }
    else {
        const char *table = bitmask_table[IS_BE(self)];
        char *buff = self->ob_item;
        Py_ssize_t i;

        for (i = start; i < stop; i += step)
            buff[i >> 3] ^= table[i & 7];
    }
}

/* set bits self[a:b] to vi */
static void
set_span(pauliebitsobject *self, Py_ssize_t a, Py_ssize_t b, int vi)
{
    assert(0 <= a && a <= self->nbits);
    assert(0 <= b && b <= self->nbits);
    assert(self->readonly == 0);

    if (b >= a + 8) {
        const Py_ssize_t ca = BYTES(a);  /* char-range(ca, cb) */
        const Py_ssize_t cb = b / 8;

        assert(a + 8 > 8 * ca && 8 * cb + 8 > b);

        set_span(self, a, 8 * ca, vi);
        memset(self->ob_item + ca, vi ? 0xff : 0x00, (size_t) (cb - ca));
        set_span(self, 8 * cb, b, vi);
    }
    else {                               /* (bit-) range(a, b) */
        while (a < b)
            setbit(self, a++, vi);
    }
}

/* set bits self[start:stop:step] to vi */
static void
set_range(pauliebitsobject *self,
          Py_ssize_t start, Py_ssize_t stop, Py_ssize_t step, int vi)
{
    assert(step > 0);

    if (step == 1) {
        set_span(self, start, stop, vi);
    }
    else {
        const char *table = bitmask_table[IS_BE(self)];
        char *buff = self->ob_item;
        Py_ssize_t i;

        if (vi) {
            for (i = start; i < stop; i += step)
                buff[i >> 3] |= table[i & 7];
        }
        else {
            for (i = start; i < stop; i += step)
                buff[i >> 3] &= ~table[i & 7];
        }
    }
}

/* Return number of 1 bits in self[a:b]; cannot fail. */
static Py_ssize_t
count_span(pauliebitsobject *self, Py_ssize_t a, Py_ssize_t b)
{
    const Py_ssize_t n = b - a;
    Py_ssize_t cnt = 0;

    assert(0 <= a && a <= self->nbits);
    assert(0 <= b && b <= self->nbits);

    if (n >= 64) {
        Py_ssize_t p = BYTES(a), w;  /* first full byte */
        p += to_aligned((void *) (self->ob_item + p));  /* align pointer */
        w = (b / 8 - p) / 8;         /* number of (full) words to count */

        assert(8 * p - a < 64 && b - (8 * (p + 8 * w)) < 64 && w >= 0);

        cnt += count_span(self, a, 8 * p);
        cnt += popcnt_words((uint64_t *) (self->ob_item + p), w);
        cnt += count_span(self, 8 * (p + 8 * w), b);
    }
    else if (n >= 8) {
        const Py_ssize_t ca = BYTES(a);   /* char-range(ca, cb) */
        const Py_ssize_t cb = b / 8, m = cb - ca;

        assert(8 * ca - a < 8 && b - 8 * cb < 8 && 0 <= m && m < 8);

        cnt += count_span(self, a, 8 * ca);
        if (m) {                /* starting at ca count in m bytes */
            uint64_t tmp = 0;
            /* copy bytes we want to count into tmp word */
            memcpy((char *) &tmp, self->ob_item + ca, (size_t) m);
            cnt += popcnt_64(tmp);
        }
        cnt += count_span(self, 8 * cb, b);
    }
    else {                                /* (bit-) range(a, b) */
        while (a < b)
            cnt += getbit(self, a++);
    }
    return cnt;
}

/* return number of 1 bits in self[start:stop:step] */
static Py_ssize_t
count_range(pauliebitsobject *self,
            Py_ssize_t start, Py_ssize_t stop, Py_ssize_t step)
{
    assert(step > 0);

    if (step == 1) {
        return count_span(self, start, stop);
    }
    else {
        Py_ssize_t cnt = 0, i;

        for (i = start; i < stop; i += step)
            cnt += getbit(self, i);
        return cnt;
    }
}

/* return first (or rightmost in case right=1) occurrence
   of vi in self[a:b], -1 when not found */
static Py_ssize_t
find_bit(pauliebitsobject *self, int vi, Py_ssize_t a, Py_ssize_t b, int right);

static Py_ssize_t
find_bit_words(pauliebitsobject *self, int vi,
               Py_ssize_t a, Py_ssize_t b, int right)
{
    const Py_ssize_t wa = (a + 63) / 64;  /* word-range(wa, wb) */
    const Py_ssize_t wb = b / 64;
    const uint64_t *wbuff = WBUFF(self);
    const uint64_t w = vi ? 0 : ~0;
    Py_ssize_t res, i;

    if (right) {
        if ((res = find_bit(self, vi, 64 * wb, b, 1)) >= 0)
            return res;

        for (i = wb - 1; i >= wa; i--) {  /* skip uint64 words */
            if (w ^ wbuff[i])
                return find_bit(self, vi, 64 * i, 64 * i + 64, 1);
        }
        return find_bit(self, vi, a, 64 * wa, 1);
    }
    else {
        if ((res = find_bit(self, vi, a, 64 * wa, 0)) >= 0)
            return res;

        for (i = wa; i < wb; i++) {       /* skip uint64 words */
            if (w ^ wbuff[i])
                return find_bit(self, vi, 64 * i, 64 * i + 64, 0);
        }
        return find_bit(self, vi, 64 * wb, b, 0);
    }
}

static Py_ssize_t
find_bit_bytes(pauliebitsobject *self, int vi,
               Py_ssize_t a, Py_ssize_t b, int right)
{
    const Py_ssize_t ca = BYTES(a);  /* char-range(ca, cb) */
    const Py_ssize_t cb = b / 8;
    const char *buff = self->ob_item;
    const char c = vi ? 0 : ~0;
    Py_ssize_t res, i;

    if (right) {
        if ((res = find_bit(self, vi, 8 * cb, b, 1)) >= 0)
            return res;

        for (i = cb - 1; i >= ca; i--) {  /* skip bytes */
            if (c ^ buff[i])
                return find_bit(self, vi, 8 * i, 8 * i + 8, 1);
        }
        return find_bit(self, vi, a, 8 * ca, 1);
    }
    else {
        if ((res = find_bit(self, vi, a, 8 * ca, 0)) >= 0)
            return res;

        for (i = ca; i < cb; i++) {       /* skip bytes */
            if (c ^ buff[i])
                return find_bit(self, vi, 8 * i, 8 * i + 8, 0);
        }
        return find_bit(self, vi, 8 * cb, b, 0);
    }
}

static Py_ssize_t
find_bit(pauliebitsobject *self, int vi, Py_ssize_t a, Py_ssize_t b, int right)
{
    const Py_ssize_t n = b - a;
    Py_ssize_t i;

    assert(0 <= a && a <= self->nbits);
    assert(0 <= b && b <= self->nbits);
    assert(0 <= vi && vi <= 1);
    if (n <= 0)
        return -1;

    /* When the search range is greater than 64 bits, we skip uint64 words.
       Note that we cannot check for n >= 64 here as the function could then
       go into an infinite recursive loop when a word is found. */
    if (n > 64)
        return find_bit_words(self, vi, a, b, right);

    /* For the same reason as above, we cannot check for n >= 8 here. */
    if (n > 8)
        return find_bit_bytes(self, vi, a, b, right);

    /* finally, search for the desired bit by stepping one-by-one */
    for (i = right ? b - 1 : a; a <= i && i < b; i += right ? -1 : 1)
        if (getbit(self, i) == vi)
            return i;

    return -1;
}

/* Return first/rightmost occurrence of sub-pauliebits (in self), such that
   sub is contained within self[start:stop], or -1 when sub is not found. */
static Py_ssize_t
find_sub(pauliebitsobject *self, pauliebitsobject *sub,
         Py_ssize_t start, Py_ssize_t stop, int right)
{
    const Py_ssize_t sbits = sub->nbits;
    const Py_ssize_t step = right ? -1 : 1;
    Py_ssize_t i, k;

    stop -= sbits - 1;
    i = right ? stop - 1 : start;

    while (start <= i && i < stop) {
        for (k = 0; k < sbits; k++)
            if (getbit(self, i + k) != getbit(sub, k))
                goto next;

        return i;
    next:
        i += step;
    }
    return -1;
}

/* return the number of non-overlapping occurrences of sub-pauliebits within
   self[start:stop] */
static Py_ssize_t
count_sub(pauliebitsobject *self, pauliebitsobject *sub,
          Py_ssize_t start, Py_ssize_t stop)
{
    const Py_ssize_t sbits = sub->nbits;
    Py_ssize_t pos, cnt = 0;

    assert(0 <= start && start <= self->nbits);
    assert(0 <= stop && stop <= self->nbits);

    if (sbits == 0)
        return (start <= stop) ? stop - start + 1 : 0;

    while ((pos = find_sub(self, sub, start, stop, 0)) >= 0) {
        start = pos + sbits;
        cnt++;
    }
    return cnt;
}

/* set item i in self to given value */
static int
set_item(pauliebitsobject *self, Py_ssize_t i, PyObject *value)
{
    int vi;

    if (!conv_pybit(value, &vi))
        return -1;

    setbit(self, i, vi);
    return 0;
}

static int
extend_pauliebits(pauliebitsobject *self, pauliebitsobject *other)
{
    /* We have to store the sizes before we resize, and since
       other may be self, we also need to store other->nbits. */
    const Py_ssize_t self_nbits = self->nbits;
    const Py_ssize_t other_nbits = other->nbits;

    if (resize(self, self_nbits + other_nbits) < 0)
        return -1;

    copy_n(self, self_nbits, other, 0, other_nbits);
    return 0;
}

static int
extend_iter(pauliebitsobject *self, PyObject *iter)
{
    const Py_ssize_t nbits = self->nbits;
    PyObject *item;

    assert(PyIter_Check(iter));
    while ((item = PyIter_Next(iter))) {
        if (resize(self, self->nbits + 1) < 0 ||
            set_item(self, self->nbits - 1, item) < 0)
        {
            Py_DECREF(item);
            /* ignore resize() return value as we fail anyhow */
            resize(self, nbits);
            return -1;
        }
        Py_DECREF(item);
    }
    if (PyErr_Occurred())
        return -1;

    return 0;
}

static int
extend_sequence(pauliebitsobject *self, PyObject *sequence)
{
    const Py_ssize_t nbits = self->nbits;
    Py_ssize_t n, i;

    if ((n = PySequence_Size(sequence)) < 0)
        return -1;

    if (resize(self, nbits + n) < 0)
        return -1;

    for (i = 0; i < n; i++) {
        PyObject *item = PySequence_GetItem(sequence, i);
        if (item == NULL || set_item(self, nbits + i, item) < 0) {
            Py_XDECREF(item);
            resize(self, nbits);
            return -1;
        }
        Py_DECREF(item);
    }
    return 0;
}

static int
extend_unicode01(pauliebitsobject *self, PyObject *unicode)
{
    const Py_ssize_t nbits = self->nbits;
    const Py_ssize_t length = PyUnicode_GET_LENGTH(unicode);
    Py_ssize_t i = nbits, j;  /* i is the current write index in self */

    if (resize(self, nbits + length) < 0)
        return -1;

    for (j = 0; j < length; j++) {
        Py_UCS4 ch = PyUnicode_READ_CHAR(unicode, j);

        switch (ch) {
        case '0':
        case '1':
            setbit(self, i++, ch - '0');
            continue;
        case '_':
            continue;
        }
        if (Py_UNICODE_ISSPACE(ch))
            continue;

        PyErr_Format(PyExc_ValueError, "expected '0' or '1' (or whitespace "
                     "or underscore), got '%c' (0x%02x)", ch, ch);
        resize(self, nbits);  /* no bits added on error */
        return -1;
    }
    return resize(self, i);  /* in case we ignored characters */
}

static int
extend_dispatch(pauliebitsobject *self, PyObject *obj)
{
    PyObject *iter;

    /* dispatch on type */
    if (pauliebits_Check(obj))                              /* pauliebits */
        return extend_pauliebits(self, (pauliebitsobject *) obj);

    if (PyUnicode_Check(obj))                       /* Unicode string */
        return extend_unicode01(self, obj);

    if (PySequence_Check(obj))                            /* sequence */
        return extend_sequence(self, obj);

    if (PyIter_Check(obj))                                    /* iter */
        return extend_iter(self, obj);

    /* finally, try to get the iterator of the object */
    if ((iter = PyObject_GetIter(obj))) {
        int res = extend_iter(self, iter);
        Py_DECREF(iter);
        return res;
    }

    PyErr_Format(PyExc_TypeError,
                 "'%s' object is not iterable", Py_TYPE(obj)->tp_name);
    return -1;
}

/**************************************************************************
                     Implementation of pauliebits methods
 **************************************************************************/

/*
   All methods which modify the buffer need to raise an exception when the
   buffer is read-only.  This is necessary because the buffer may be imported
   from another object which has a read-only buffer.

   We decided to do this check at the top level here, by adding the
   RAISE_IF_READONLY macro to all methods which modify the buffer.
   We could have done it at the low level (in setbit(), etc.), however as
   many of these functions have no return value we decided to do it here.

   The situation is different from how resize() raises an exception when
   called on an imported buffer.  There, it is easy to raise the exception
   in resize() itself, as there only one function which resizes the buffer,
   and this function (resize()) needs to report failures anyway.
*/

/* raise when buffer is readonly */
#define RAISE_IF_READONLY(self, ret_value)                                  \
    if (((pauliebitsobject *) self)->readonly) {                              \
        PyErr_SetString(PyExc_TypeError, "cannot modify read-only memory"); \
        return ret_value;                                                   \
    }

static PyObject *
pauliebits_all(pauliebitsobject *self)
{
    Py_ssize_t pos;

    Py_BEGIN_CRITICAL_SECTION(self);
    pos = find_bit(self, 0, 0, self->nbits, 0);
    Py_END_CRITICAL_SECTION();

    return PyBool_FromLong(pos == -1);
}

PyDoc_STRVAR(all_doc,
"all() -> bool\n\
\n\
Return `True` when all bits in pauliebits are 1.\n\
`a.all()` is a faster version of `all(a)`.");


static PyObject *
pauliebits_any(pauliebitsobject *self)
{
    Py_ssize_t pos;

    Py_BEGIN_CRITICAL_SECTION(self);
    pos = find_bit(self, 1, 0, self->nbits, 0);
    Py_END_CRITICAL_SECTION();

    return PyBool_FromLong(pos >= 0);
}

PyDoc_STRVAR(any_doc,
"any() -> bool\n\
\n\
Return `True` when any bit in pauliebits is 1.\n\
`a.any()` is a faster version of `any(a)`.");


static PyObject *
pauliebits_append(pauliebitsobject *self, PyObject *value)
{
    int ret, vi;

    RAISE_IF_READONLY(self, NULL);

    if (!conv_pybit(value, &vi))
        return NULL;

    Py_BEGIN_CRITICAL_SECTION(self);
    ret = resize(self, self->nbits + 1);
    if (ret == 0)
        setbit(self, self->nbits - 1, vi);
    Py_END_CRITICAL_SECTION();

    if (ret < 0)
        return NULL;
    Py_RETURN_NONE;
}

PyDoc_STRVAR(append_doc,
"append(item, /)\n\
\n\
Append `item` to the end of the pauliebits.");


static PyObject *
pauliebits_bytereverse(pauliebitsobject *self, PyObject *args)
{
    Py_ssize_t start = 0, stop = PY_SSIZE_T_MAX;

    RAISE_IF_READONLY(self, NULL);
    if (!PyArg_ParseTuple(args, "|nn:bytereverse", &start, &stop))
        return NULL;

    Py_BEGIN_CRITICAL_SECTION(self);
    if (stop == PY_SSIZE_T_MAX)
        stop = Py_SIZE(self);

    if (PySlice_AdjustIndices(Py_SIZE(self), &start, &stop, 1) > 0)
        bytereverse(self->ob_item + start, stop - start);

    Py_END_CRITICAL_SECTION();
    Py_RETURN_NONE;
}

PyDoc_STRVAR(bytereverse_doc,
"bytereverse(start=0, stop=<end of buffer>, /)\n\
\n\
For each byte in byte-range(`start`, `stop`) reverse bits in-place.\n\
The start and stop indices are given in terms of bytes (not bits) and\n\
are interpreted like slice bounds and clipped to the buffer size.\n\
Also note that this method only changes the buffer; it does not change the\n\
bit-endianness of the pauliebits object.  Pad bits are left unchanged such\n\
that two consecutive calls will always leave the pauliebits unchanged.");


static PyObject *
pauliebits_buffer_info(pauliebitsobject *self)
{
    PyObject *info, *res, *args, *address, *readonly, *imported;

    info = pauliebits_module_attr("BufferInfo");
    if (info == NULL)
        return NULL;

    address = PyLong_FromVoidPtr((void *) self->ob_item);
    readonly = PyBool_FromLong(self->readonly);
    imported = PyBool_FromLong(self->buffer ? 1 : 0);
    if (address == NULL || readonly == NULL || imported == NULL)
        goto error;

    args = Py_BuildValue("OnsnnOOi",
                         address,
                         Py_SIZE(self),
                         ENDIAN_STR(self->endian),
                         PADBITS(self),
                         self->allocated,
                         readonly,
                         imported,
                         self->ob_exports);
    if (args == NULL)
        goto error;

    Py_DECREF(address);
    Py_DECREF(readonly);
    Py_DECREF(imported);
    res = PyObject_CallObject(info, args);
    Py_DECREF(args);
    Py_DECREF(info);
    return res;

 error:
    Py_XDECREF(address);
    Py_XDECREF(readonly);
    Py_XDECREF(imported);
    Py_DECREF(info);
    return NULL;
}

PyDoc_STRVAR(buffer_info_doc,
"buffer_info() -> BufferInfo\n\
\n\
Return named tuple with following fields:\n\
\n\
0. `address`: memory address of buffer\n\
1. `nbytes`: buffer size (in bytes)\n\
2. `endian`: bit-endianness as a string\n\
3. `padbits`: number of pad bits\n\
4. `alloc`: allocated memory for buffer (in bytes)\n\
5. `readonly`: memory is read-only (bool)\n\
6. `imported`: buffer is imported (bool)\n\
7. `exports`: number of buffer exports");


static PyObject *
pauliebits_clear(pauliebitsobject *self)
{
    int ret;

    RAISE_IF_READONLY(self, NULL);

    Py_BEGIN_CRITICAL_SECTION(self);
    ret = resize(self, 0);
    Py_END_CRITICAL_SECTION();

    if (ret < 0)
        return NULL;
    Py_RETURN_NONE;
}

PyDoc_STRVAR(clear_doc,
"clear()\n\
\n\
Remove all items from pauliebits.");


/* Set readonly member to 1 if self is an instance of frozenpauliebits.
   Return PyObject of self.  On error, set exception and return NULL. */
static PyObject *
freeze_if_frozen(pauliebitsobject *self)
{
    PyObject *frozen;  /* frozenpauliebits class object */
    int is_frozen;

    assert(self->ob_exports == 0 && self->buffer == NULL);

    if (Py_TYPE(self) == &Pauliebits_Type)  /* shortcut for common case */
        return (PyObject *) self;

    frozen = pauliebits_module_attr("frozenpauliebits");
    if (frozen == NULL)
        return NULL;

    is_frozen = PyObject_IsInstance((PyObject *) self, frozen);
    Py_DECREF(frozen);
    if (is_frozen < 0)
        return NULL;

    if (is_frozen) {
        set_padbits(self);
        self->readonly = 1;
    }
    return (PyObject *) self;
}


static PyObject *
pauliebits_copy(pauliebitsobject *self)
{
    pauliebitsobject *res;

    Py_BEGIN_CRITICAL_SECTION(self);
    res = pauliebits_cp(self);
    Py_END_CRITICAL_SECTION();

    if (res == NULL)
        return NULL;

    return freeze_if_frozen(res);
}

PyDoc_STRVAR(copy_doc,
"copy() -> pauliebits\n\
\n\
Return copy of pauliebits (with same bit-endianness).");


static PyObject *
pauliebits_count(pauliebitsobject *self, PyObject *args)
{
    PyObject *sub = Py_None;
    Py_ssize_t start = 0, stop = PY_SSIZE_T_MAX, step = 1;
    Py_ssize_t cnt = 0;

    if (!PyArg_ParseTuple(args, "|Onnn:count",
                          &sub , &start, &stop, &step))
        return NULL;

    if (step == 0) {
        PyErr_SetString(PyExc_ValueError, "step cannot be zero");
        return NULL;
    }

    if (PyIndex_Check(sub) || sub == Py_None) {
        Py_ssize_t slicelength;
        int vi = 1;

        if (PyIndex_Check(sub) && !conv_pybit(sub, &vi))
            return NULL;

        Py_BEGIN_CRITICAL_SECTION(self);
        slicelength = PySlice_AdjustIndices(self->nbits, &start, &stop, step);
        adjust_step_positive(slicelength, &start, &stop, &step);
        cnt = count_range(self, start, stop, step);
        Py_END_CRITICAL_SECTION();
        return PyLong_FromSsize_t(vi ? cnt : slicelength - cnt);
    }

    if (pauliebits_Check(sub)) {   /* sub-pauliebits count */
        if (step != 1) {
            PyErr_SetString(PyExc_ValueError,
                            "step must be 1 for sub-pauliebits count");
            return NULL;
        }
        Py_BEGIN_CRITICAL_SECTION2(self, sub);
        if (start <= self->nbits) {
            PySlice_AdjustIndices(self->nbits, &start, &stop, 1);
            cnt = count_sub(self, (pauliebitsobject *) sub, start, stop);
        }
        Py_END_CRITICAL_SECTION2();
        return PyLong_FromSsize_t(cnt);
    }

    return PyErr_Format(PyExc_TypeError, "sub_pauliebits must be pauliebits or "
                        "int, not '%s'", Py_TYPE(sub)->tp_name);
}

PyDoc_STRVAR(count_doc,
"count(value=1, start=0, stop=<end>, step=1, /) -> int\n\
\n\
Number of occurrences of `value` pauliebits within `[start:stop:step]`.\n\
Optional arguments `start`, `stop` and `step` are interpreted in\n\
slice notation, meaning `a.count(value, start, stop, step)` equals\n\
`a[start:stop:step].count(value)`.\n\
The `value` may also be a sub-pauliebits.  In this case non-overlapping\n\
occurrences are counted within `[start:stop]` (`step` must be 1).");


/* Extend self without running arbitrary Python code while self is locked. */
static int
extend_thread_safe(pauliebitsobject *self, PyObject *obj)
{
#ifdef Py_GIL_DISABLED
    int res1, res2;
    pauliebitsobject *tmp;

    if (pauliebits_Check(obj)) {
        Py_BEGIN_CRITICAL_SECTION2(self, obj);
        res1 = extend_pauliebits(self, (pauliebitsobject *) obj);
        Py_END_CRITICAL_SECTION2();
        return res1;
    }

    /* Build input into a temporary pauliebits first, so arbitrary iteration
       and Python conversions happen outside self's critical section. */
    tmp = newpauliebitsobject(&Pauliebits_Type, 0, self->endian);
    if (tmp == NULL)
        return -1;
    /* Even on failure, tmp may contain successfully consumed prefix bits.
       Append them below to preserve partial-extension behavior. */
    res1 = extend_dispatch(tmp, obj);

    Py_BEGIN_CRITICAL_SECTION(self);
    /* Append accumulated bits to self in one protected operation. */
    res2 = extend_pauliebits(self, tmp);
    Py_END_CRITICAL_SECTION();
    Py_DECREF(tmp);
    return (res1 < 0 || res2 < 0) ? -1 : 0;
#else
    return extend_dispatch(self, obj);
#endif
}

static PyObject *
pauliebits_extend(pauliebitsobject *self, PyObject *obj)
{
    RAISE_IF_READONLY(self, NULL);
    if (extend_thread_safe(self, obj) < 0)
        return NULL;
    Py_RETURN_NONE;
}

PyDoc_STRVAR(extend_doc,
"extend(iterable, /)\n\
\n\
Append items from iterable to the end of the pauliebits.\n\
If `iterable` is a (Unicode) string, each `0` and `1` are appended as\n\
bits (ignoring whitespace and underscore).");


static PyObject *
pauliebits_fill(pauliebitsobject *self)
{
    Py_ssize_t p;

    RAISE_IF_READONLY(self, NULL);
    Py_BEGIN_CRITICAL_SECTION(self);
    p = PADBITS(self);  /* number of pad bits */
    set_padbits(self);
    /* there is no reason to call resize() - .fill() will not raise
       BufferError when buffer is imported or exported */
    self->nbits += p;
    Py_END_CRITICAL_SECTION();

    return PyLong_FromSsize_t(p);
}

PyDoc_STRVAR(fill_doc,
"fill() -> int\n\
\n\
Add zeros to the end of the pauliebits, such that the length will be\n\
a multiple of 8, and return the number of bits added [0..7].");


static PyObject *
pauliebits_find(pauliebitsobject *self, PyObject *args, PyObject *kwds)
{
    static char *kwlist[] = {"", "", "", "right", NULL};
    Py_ssize_t start = 0, stop = PY_SSIZE_T_MAX, pos = -1;
    int right = 0;
    PyObject *sub;

    if (!PyArg_ParseTupleAndKeywords(args, kwds, "O|nni", kwlist,
                                     &sub, &start, &stop, &right))
        return NULL;

    if (PyIndex_Check(sub)) {
        int vi;

        if (!conv_pybit(sub, &vi))
            return NULL;

        Py_BEGIN_CRITICAL_SECTION(self);
        PySlice_AdjustIndices(self->nbits, &start, &stop, 1);
        pos = find_bit(self, vi, start, stop, right);
        Py_END_CRITICAL_SECTION();
        return PyLong_FromSsize_t(pos);
    }

    if (pauliebits_Check(sub)) {   /* find sub-pauliebits */
        Py_BEGIN_CRITICAL_SECTION2(self, sub);
        if (start <= self->nbits) {
            PySlice_AdjustIndices(self->nbits, &start, &stop, 1);
            pos = find_sub(self, (pauliebitsobject *) sub, start, stop, right);
        }
        Py_END_CRITICAL_SECTION2();
        return PyLong_FromSsize_t(pos);
    }

    return PyErr_Format(PyExc_TypeError, "sub_pauliebits must be pauliebits or "
                        "int, not '%s'", Py_TYPE(sub)->tp_name);
}

PyDoc_STRVAR(find_doc,
"find(sub_pauliebits, start=0, stop=<end>, /, right=False) -> int\n\
\n\
Return lowest (or rightmost when `right=True`) index where sub_pauliebits\n\
is found, such that sub_pauliebits is contained within `[start:stop]`.\n\
Return -1 when sub_pauliebits is not found.");


static PyObject *
pauliebits_index(pauliebitsobject *self, PyObject *args, PyObject *kwds)
{
    PyObject *result;

    result = pauliebits_find(self, args, kwds);
    if (result == NULL)
        return NULL;

    assert(PyLong_Check(result));
    if (PyLong_AsSsize_t(result) < 0) {
        Py_DECREF(result);
        return PyErr_Format(PyExc_ValueError, "%A not in pauliebits",
                            PyTuple_GET_ITEM(args, 0));
    }
    return result;
}

PyDoc_STRVAR(index_doc,
"index(sub_pauliebits, start=0, stop=<end>, /, right=False) -> int\n\
\n\
Return lowest (or rightmost when `right=True`) index where sub_pauliebits\n\
is found, such that sub_pauliebits is contained within `[start:stop]`.\n\
Raises `ValueError` when sub_pauliebits is not present.");


static int
insert_lock_held(pauliebitsobject *self, Py_ssize_t i, int vi)
{
    const Py_ssize_t n = self->nbits;

    if (i < 0) {
        i += n;
        if (i < 0)
            i = 0;
    }
    if (i > n)
        i = n;

    if (insert_n(self, i, 1) < 0)
        return -1;

    setbit(self, i, vi);
    return 0;
}

static PyObject *
pauliebits_insert(pauliebitsobject *self, PyObject *args)
{
    Py_ssize_t i;
    int vi, ret;

    RAISE_IF_READONLY(self, NULL);
    if (!PyArg_ParseTuple(args, "nO&:insert", &i, conv_pybit, &vi))
        return NULL;

    Py_BEGIN_CRITICAL_SECTION(self);
    ret = insert_lock_held(self, i, vi);
    Py_END_CRITICAL_SECTION();

    if (ret < 0)
        return NULL;
    Py_RETURN_NONE;
}

PyDoc_STRVAR(insert_doc,
"insert(index, value, /)\n\
\n\
Insert `value` into pauliebits before `index`.");


static PyObject *
pauliebits_invert(pauliebitsobject *self, PyObject *args)
{
    PyObject *arg = Py_None;

    RAISE_IF_READONLY(self, NULL);
    if (!PyArg_ParseTuple(args, "|O:invert", &arg))
        return NULL;

    if (PyIndex_Check(arg)) {
        Py_ssize_t i;
        int err = 0;

        i = PyNumber_AsSsize_t(arg, NULL);
        if (i == -1 && PyErr_Occurred())
            return NULL;

        Py_BEGIN_CRITICAL_SECTION(self);
        if (i < 0)
            i += self->nbits;
        if (i < 0 || i >= self->nbits) {
            PyErr_SetString(PyExc_IndexError, "index out of range");
            err = 1;
        }
        else {
            self->ob_item[i / 8] ^= BITMASK(self, i);
        }
        Py_END_CRITICAL_SECTION();
        if (err)
            return NULL;
    }
    else if (PySlice_Check(arg)) {
        Py_ssize_t start, stop, step, slicelength;

        if (PySlice_Unpack(arg, &start, &stop, &step) < 0)
            return NULL;

        Py_BEGIN_CRITICAL_SECTION(self);
        slicelength = PySlice_AdjustIndices(self->nbits, &start, &stop, step);
        adjust_step_positive(slicelength, &start, &stop, &step);
        invert_range(self, start, stop, step);
        Py_END_CRITICAL_SECTION();
    }
    else if (arg == Py_None) {
        Py_BEGIN_CRITICAL_SECTION(self);
        invert_span(self, 0, self->nbits);
        Py_END_CRITICAL_SECTION();
    }
    else {
        return PyErr_Format(PyExc_TypeError,
                            "index expected, not '%s' object",
                            Py_TYPE(arg)->tp_name);
    }
    Py_RETURN_NONE;
}

PyDoc_STRVAR(invert_doc,
"invert(index=<all bits>, /)\n\
\n\
Invert bits in-place.  When `index` is omitted, invert all bits.\n\
When `index` is an integer, invert the single bit at index.\n\
When `index` is a slice, invert the selected bits.");


static PyObject *
pauliebits_reduce(pauliebitsobject *self)
{
    PyObject *reconstructor;
    PyObject *dict, *bytes, *result;
    int padbits;

    reconstructor = pauliebits_module_attr("_pauliebits_reconstructor");
    if (reconstructor == NULL)
        return NULL;

    dict = PyObject_GetAttrString((PyObject *) self, "__dict__");
    if (dict == NULL) {
        PyErr_Clear();
        dict = Py_None;
        Py_INCREF(dict);
    }

    Py_BEGIN_CRITICAL_SECTION(self);
    set_padbits(self);
    padbits = (int) PADBITS(self);
    bytes = PyBytes_FromStringAndSize(self->ob_item, Py_SIZE(self));
    Py_END_CRITICAL_SECTION();

    if (bytes == NULL) {
        Py_DECREF(dict);
        Py_DECREF(reconstructor);
        return NULL;
    }

    result = Py_BuildValue("O(OOsii)O", reconstructor, Py_TYPE(self), bytes,
                           ENDIAN_STR(self->endian), padbits,
                           self->readonly, dict);
    Py_DECREF(dict);
    Py_DECREF(reconstructor);
    Py_DECREF(bytes);
    return result;
}

PyDoc_STRVAR(reduce_doc, "Internal. Used for pickling support.");

static PyObject *
pauliebits_repr(pauliebitsobject *self)
{
    PyObject *result;
    Py_ssize_t nbits, strsize, i;
    char *str;
    int err = 1;

    Py_BEGIN_CRITICAL_SECTION(self);
    nbits = self->nbits;
    Py_END_CRITICAL_SECTION();

    if (nbits == 0)
        return PyUnicode_FromString("pauliebits()");

    strsize = nbits + 14;  /* ИСПРАВЛЕНО: 14 — это длина "pauliebits('')" */
    str = PyMem_New(char, strsize);
    if (str == NULL)
        return PyErr_NoMemory();

    strcpy(str, "pauliebits('");  /* ИСПРАВЛЕНО: теперь длина этого префикса равна 12 */

    Py_BEGIN_CRITICAL_SECTION(self);
    if (self->nbits == nbits) {
        for (i = 0; i < nbits; i++)
            str[i + 12] = getbit(self, i) + '0'; /* ИСПРАВЛЕНО: смещение 12 вместо 10 */
        err = 0;
    }
    Py_END_CRITICAL_SECTION();

    if (err) {
        PyMem_Free((void *) str);
        PyErr_SetString(PyExc_RuntimeError,
                        "pauliebits changed size during repr()");
        return NULL;
    }

    str[strsize - 2] = '\'';
    str[strsize - 1] = ')';
    /* we know the string length beforehand - not null-terminated */
    result = PyUnicode_FromStringAndSize(str, strsize);
    PyMem_Free((void *) str);
    return result;
}


static void
reverse_lock_held(pauliebitsobject *self)
{
    Py_ssize_t p = PADBITS(self);  /* number of pad bits */
    char *buff = self->ob_item;

    /* Increase self->nbits to full buffer size.  The p pad bits will
       later be the leading p bits.  To remove those p leading bits, we
       must have p extra bits at the end of the pauliebits. */
    self->nbits += p;

    /* reverse order of bytes */
    swap_bytes(buff, Py_SIZE(self));

    /* reverse order of bits within each byte */
    bytereverse(self->ob_item, Py_SIZE(self));

    /* Remove the p pad bits at the end of the original pauliebits that
       are now the leading p bits.
       The reason why we don't just call delete_n(self, 0, p) here is that
       it calls resize(), and we want to allow reversing an imported
       writable buffer. */
    copy_n(self, 0, self, p, self->nbits - p);
    self->nbits -= p;
}

static PyObject *
pauliebits_reverse(pauliebitsobject *self)
{
    RAISE_IF_READONLY(self, NULL);
    Py_BEGIN_CRITICAL_SECTION(self);
    reverse_lock_held(self);
    Py_END_CRITICAL_SECTION();
    Py_RETURN_NONE;
}

PyDoc_STRVAR(reverse_doc,
"reverse()\n\
\n\
Reverse all bits in pauliebits (in-place).");


static PyObject *
pauliebits_setall(pauliebitsobject *self, PyObject *value)
{
    int vi;

    RAISE_IF_READONLY(self, NULL);
    if (!conv_pybit(value, &vi))
        return NULL;

    Py_BEGIN_CRITICAL_SECTION(self);
    if (self->ob_item)
        memset(self->ob_item, vi ? 0xff : 0x00, (size_t) Py_SIZE(self));
    Py_END_CRITICAL_SECTION();
    Py_RETURN_NONE;
}

PyDoc_STRVAR(setall_doc,
"setall(value, /)\n\
\n\
Set all elements in pauliebits to `value`.\n\
Note that `a.setall(value)` is equivalent to `a[:] = value`.");


static void
sort_lock_held(pauliebitsobject *self, int reverse)
{
    Py_ssize_t nbits = self->nbits, cnt1;

    cnt1 = count_span(self, 0, nbits);
    if (reverse) {
        set_span(self, 0, cnt1, 1);
        set_span(self, cnt1, nbits, 0);
    }
    else {
        Py_ssize_t cnt0 = nbits - cnt1;
        set_span(self, 0, cnt0, 0);
        set_span(self, cnt0, nbits, 1);
    }
}

static PyObject *
pauliebits_sort(pauliebitsobject *self, PyObject *args, PyObject *kwds)
{
    static char *kwlist[] = {"reverse", NULL};
    int reverse = 0;

    RAISE_IF_READONLY(self, NULL);
    if (!PyArg_ParseTupleAndKeywords(args, kwds, "|i:sort", kwlist, &reverse))
        return NULL;

    Py_BEGIN_CRITICAL_SECTION(self);
    sort_lock_held(self, reverse);
    Py_END_CRITICAL_SECTION();
    Py_RETURN_NONE;
}

PyDoc_STRVAR(sort_doc,
"sort(reverse=False)\n\
\n\
Sort all bits in pauliebits (in-place).");


static PyObject *
pauliebits_tolist(pauliebitsobject *self)
{
    PyObject *zero = Py_GetConstant(Py_CONSTANT_ZERO);
    PyObject *one = Py_GetConstant(Py_CONSTANT_ONE);
    PyObject *list;
    Py_ssize_t nbits, i;
    int err = 1;  /* pauliebits changed size */

    if (zero == NULL || one == NULL)
        goto error;

    Py_BEGIN_CRITICAL_SECTION(self);
    nbits = self->nbits;
    Py_END_CRITICAL_SECTION();

    list = PyList_New(nbits);
    if (list == NULL)
        goto error;

    Py_BEGIN_CRITICAL_SECTION(self);
    if (self->nbits == nbits) {
        for (i = 0; i < nbits; i++)
            PyList_SET_ITEM(list, i,
                            Py_NewRef(getbit(self, i) ? one : zero));
        err = 0;
    }
    Py_END_CRITICAL_SECTION();
    Py_DECREF(zero);
    Py_DECREF(one);

    if (err) {
        Py_DECREF(list);
        PyErr_SetString(PyExc_RuntimeError,
                        "pauliebits changed size during .tolist()");
        return NULL;
    }
    return list;
 error:
    Py_XDECREF(zero);
    Py_XDECREF(one);
    return NULL;
}

PyDoc_STRVAR(tolist_doc,
"tolist() -> list\n\
\n\
Return pauliebits as list of integers.\n\
`a.tolist()` equals `list(a)`.");


static int
frombytes_lock_held(pauliebitsobject *self, const Py_buffer *view)
{
    Py_ssize_t n = Py_SIZE(self);  /* nbytes before extending */
    Py_ssize_t p = PADBITS(self);  /* number of pad bits */

    if (resize(self, 8 * (n + view->len)) < 0)
        return -1;

    if (view->len)
        memcpy(self->ob_item + n, (char *) view->buf, (size_t) view->len);

    /* remove pad bits starting at previous bit length (8 * n - p) */
    return delete_n(self, 8 * n - p, p);
}

static PyObject *
pauliebits_frombytes(pauliebitsobject *self, PyObject *buffer)
{
    Py_buffer view;
    int ret;

    RAISE_IF_READONLY(self, NULL);
    if (PyObject_GetBuffer(buffer, &view, PyBUF_SIMPLE) < 0)
        return NULL;

    Py_BEGIN_CRITICAL_SECTION2(self, view.obj);
    ret = frombytes_lock_held(self, &view);
    Py_END_CRITICAL_SECTION2();
    PyBuffer_Release(&view);

    if (ret < 0)
        return NULL;
    Py_RETURN_NONE;
}

PyDoc_STRVAR(frombytes_doc,
"frombytes(bytes, /)\n\
\n\
Extend pauliebits with raw bytes from a bytes-like object.\n\
Each added byte will add eight bits to the pauliebits.");


static PyObject *
pauliebits_tobytes(pauliebitsobject *self)
{
    PyObject *res;

    Py_BEGIN_CRITICAL_SECTION(self);
    set_padbits(self);
    res = PyBytes_FromStringAndSize(self->ob_item, Py_SIZE(self));
    Py_END_CRITICAL_SECTION();
    return res;
}

PyDoc_STRVAR(tobytes_doc,
"tobytes() -> bytes\n\
\n\
Return the pauliebits buffer (pad bits are set to zero).\n\
`a.tobytes()` is equivalent to `bytes(a)`");


/* Extend self with bytes from f.read(n).  Return number of bytes actually
   read and extended, or -1 on failure (after setting exception). */
static Py_ssize_t
extend_fread(pauliebitsobject *self, PyObject *f, Py_ssize_t n)
{
    PyObject *bytes, *ret;
    Py_ssize_t res;             /* result (size or -1) */

    bytes = PyObject_CallMethod(f, "read", "n", n);
    if (bytes == NULL)
        return -1;
    if (!PyBytes_Check(bytes)) {
        PyErr_Format(PyExc_TypeError, ".read() did not return 'bytes', "
                     "got '%s'", Py_TYPE(bytes)->tp_name);
        Py_DECREF(bytes);
        return -1;
    }
    res = PyBytes_GET_SIZE(bytes);
    assert(0 <= res && res <= n);

    ret = pauliebits_frombytes(self, bytes);
    Py_DECREF(bytes);
    if (ret == NULL)
        return -1;

    Py_DECREF(ret);
    return res;
}

static PyObject *
pauliebits_fromfile(pauliebitsobject *self, PyObject *args)
{
    PyObject *f;
    Py_ssize_t nread = 0, n = -1;

    RAISE_IF_READONLY(self, NULL);
    if (!PyArg_ParseTuple(args, "O|n:fromfile", &f, &n))
        return NULL;

    if (n < 0)  /* read till EOF */
        n = PY_SSIZE_T_MAX;

    while (nread < n) {
        Py_ssize_t nblock = Py_MIN(n - nread, BLOCKSIZE), size;

        size = extend_fread(self, f, nblock);
        if (size < 0)
            return NULL;

        nread += size;
        assert(size <= nblock && nread <= n);

        if (size < nblock) {
            if (n == PY_SSIZE_T_MAX)  /* read till EOF */
                break;
            PyErr_SetString(PyExc_EOFError, "not enough bytes to read");
            return NULL;
        }
    }
    Py_RETURN_NONE;
}

PyDoc_STRVAR(fromfile_doc,
"fromfile(f, n=-1, /)\n\
\n\
Extend pauliebits with up to `n` bytes read from file object `f` (or any\n\
other binary stream that supports a `.read()` method, e.g. `io.BytesIO`).\n\
Each read byte will add eight bits to the pauliebits.  When `n` is omitted\n\
or negative, reads and extends all data until EOF.\n\
When `n` is non-negative but exceeds the available data, `EOFError` is\n\
raised.  However, the available data is still read and extended.");


static PyObject *
pauliebits_tofile(pauliebitsobject *self, PyObject *f)
{
    Py_ssize_t nbits, nbytes, offset;

    Py_BEGIN_CRITICAL_SECTION(self);
    nbits = self->nbits;
    nbytes = Py_SIZE(self);
    set_padbits(self);
    Py_END_CRITICAL_SECTION();

    for (offset = 0; offset < nbytes; offset += BLOCKSIZE) {
        Py_ssize_t size = Py_MIN(nbytes - offset, BLOCKSIZE);
        PyObject *block, *ret;
        char *dst;
        int err = 1;

        /* allocate before locking self - block object will stay private */
        block = PyBytes_FromStringAndSize(NULL, size);
        if (block == NULL)
            return NULL;
        dst = PyBytes_AS_STRING(block);

        Py_BEGIN_CRITICAL_SECTION(self);
        if (self->nbits == nbits) {
            memcpy(dst, self->ob_item + offset, (size_t) size);
            err = 0;
        }
        Py_END_CRITICAL_SECTION();

        if (err) {
            Py_DECREF(block);
            PyErr_SetString(PyExc_RuntimeError,
                            "pauliebits changed size during .tofile()");
            return NULL;
        }

        ret = PyObject_CallMethod(f, "write", "O", block);
        Py_DECREF(block);
        if (ret == NULL)
            return NULL;
        Py_DECREF(ret);
    }

    Py_RETURN_NONE;
}

PyDoc_STRVAR(tofile_doc,
"tofile(f, /)\n\
\n\
Write pauliebits buffer to file object `f`.");


static PyObject *
pauliebits_to01(pauliebitsobject *self, PyObject *args, PyObject *kwds)
{
    static char *kwlist[] = {"group", "sep", NULL};
    size_t strsize, j, nsep;
    Py_ssize_t group = 0, nbits, i;
    PyObject *result;
    char *sep = " ", *str;
    int err = 1;

    if (!PyArg_ParseTupleAndKeywords(args, kwds, "|ns:to01", kwlist,
                                     &group, &sep))
        return NULL;

    if (group < 0)
        return PyErr_Format(PyExc_ValueError, "non-negative integer "
                            "expected, got: %zd", group);

    Py_BEGIN_CRITICAL_SECTION(self);
    nbits = self->nbits;
    Py_END_CRITICAL_SECTION();

    strsize = nbits;
    nsep = (group && strsize) ? strlen(sep) : 0;  /* 0 means no grouping */
    if (nsep)
        strsize += nsep * ((strsize - 1) / group);

    str = PyMem_New(char, strsize);
    if (str == NULL)
        return PyErr_NoMemory();

    Py_BEGIN_CRITICAL_SECTION(self);
    if (self->nbits == nbits) {
        for (i = j = 0; i < nbits; i++) {
            if (nsep && i && i % group == 0) {
                memcpy(str + j, sep, nsep);
                j += nsep;
            }
            str[j++] = getbit(self, i) + '0';
        }
        assert(j == strsize);
        err = 0;
    }
    Py_END_CRITICAL_SECTION();

    if (err) {
        PyMem_Free((void *) str);
        PyErr_SetString(PyExc_RuntimeError,
                        "pauliebits changed size during .to01()");
        return NULL;
    }

    result = PyUnicode_FromStringAndSize(str, strsize);
    PyMem_Free((void *) str);
    return result;
}

PyDoc_STRVAR(to01_doc,
"to01(group=0, sep=' ') -> str\n\
\n\
Return pauliebits as (Unicode) string of `0`s and `1`s.\n\
The bits are grouped into `group` bits (default is no grouping).\n\
When grouped, the string `sep` is inserted between groups\n\
of `group` characters, default is a space.");


static PyObject *
pauliebits_unpack(pauliebitsobject *self, PyObject *args, PyObject *kwds)
{
    static char *kwlist[] = {"zero", "one", NULL};
    PyObject *res;
    char zero = 0x00, one = 0x01, *str;
    Py_ssize_t nbits, i;
    int err = 1;

    if (!PyArg_ParseTupleAndKeywords(args, kwds, "|cc:unpack", kwlist,
                                     &zero, &one))
        return NULL;

    Py_BEGIN_CRITICAL_SECTION(self);
    nbits = self->nbits;
    Py_END_CRITICAL_SECTION();

    res = PyBytes_FromStringAndSize(NULL, nbits);
    if (res == NULL)
        return NULL;

    Py_BEGIN_CRITICAL_SECTION(self);
    if (self->nbits == nbits) {
        str = PyBytes_AsString(res);
        for (i = 0; i < nbits; i++)
            str[i] = getbit(self, i) ? one : zero;
        err = 0;
    }
    Py_END_CRITICAL_SECTION();

    if (err) {
        Py_DECREF(res);
        PyErr_SetString(PyExc_RuntimeError,
                        "pauliebits changed size during .unpack()");
        return NULL;
    }
    return res;
}

PyDoc_STRVAR(unpack_doc,
"unpack(zero=b'\\x00', one=b'\\x01') -> bytes\n\
\n\
Return bytes that contain one byte for each bit in the pauliebits,\n\
using the specified mapping.");


static PyObject *
pauliebits_pack(pauliebitsobject *self, PyObject *buffer)
{
    Py_ssize_t nbits, i;
    Py_buffer view;
    int ret;

    RAISE_IF_READONLY(self, NULL);
    if (PyObject_GetBuffer(buffer, &view, PyBUF_SIMPLE) < 0)
        return NULL;

    Py_BEGIN_CRITICAL_SECTION2(self, view.obj);
    nbits = self->nbits;
    ret = resize(self, nbits + view.len);
    if (ret == 0) {
        for (i = 0; i < view.len; i++)
            setbit(self, nbits + i, ((char *) view.buf)[i]);
    }
    Py_END_CRITICAL_SECTION2();

    PyBuffer_Release(&view);
    if (ret < 0)
        return NULL;
    Py_RETURN_NONE;
}

PyDoc_STRVAR(pack_doc,
"pack(bytes, /)\n\
\n\
Extend pauliebits from a bytes-like object, where each byte corresponds\n\
to a single bit.  The byte `b'\\x00'` maps to bit 0 and all other bytes\n\
map to bit 1.");


/* Pop and return bit 0 or 1 while self is locked; return -1 on error. */
static int
pop_lock_held(pauliebitsobject *self, Py_ssize_t i)
{
    Py_ssize_t n = self->nbits;
    int vi;

    if (n == 0) {
        /* special case -- most common failure cause */
        PyErr_SetString(PyExc_IndexError, "pop from empty pauliebits");
        return -1;
    }

    if (i < 0)
        i += n;

    if (i < 0 || i >= n) {
        PyErr_SetString(PyExc_IndexError, "pop index out of range");
        return -1;
    }

    vi = getbit(self, i);
    if (delete_n(self, i, 1) < 0)
        return -1;

    return vi;
}

static PyObject *
pauliebits_pop(pauliebitsobject *self, PyObject *args)
{
    Py_ssize_t i = -1;
    int vi;

    RAISE_IF_READONLY(self, NULL);
    if (!PyArg_ParseTuple(args, "|n:pop", &i))
        return NULL;

    Py_BEGIN_CRITICAL_SECTION(self);
    vi = pop_lock_held(self, i);
    Py_END_CRITICAL_SECTION();

    return vi < 0 ? NULL : PyLong_FromLong(vi);
}

PyDoc_STRVAR(pop_doc,
"pop(index=-1, /) -> item\n\
\n\
Remove and return item at `index` (default last).\n\
Raises `IndexError` if index is out of range.");


static PyObject *
pauliebits_remove(pauliebitsobject *self, PyObject *value)
{
    Py_ssize_t i;
    int ret = -1, vi;

    RAISE_IF_READONLY(self, NULL);
    if (!conv_pybit(value, &vi))
        return NULL;

    Py_BEGIN_CRITICAL_SECTION(self);
    i = find_bit(self, vi, 0, self->nbits, 0);
    if (i < 0) {
        PyErr_Format(PyExc_ValueError, "%d not in pauliebits", vi);
    }
    else {
        ret = delete_n(self, i, 1);
    }
    Py_END_CRITICAL_SECTION();

    if (ret < 0)
        return NULL;
    Py_RETURN_NONE;
}

PyDoc_STRVAR(remove_doc,
"remove(value, /)\n\
\n\
Remove the first occurrence of `value`.\n\
Raises `ValueError` if value is not present.");


static void
rotate_lock_held(pauliebitsobject *self, pauliebitsobject *tmp, Py_ssize_t k)
{
    Py_ssize_t n = self->nbits;

    assert(tmp->nbits <= n / 2);  /* at most half size */

    if (tmp->nbits == k) {           /* tail is smaller */
        copy_n(tmp, 0, self, n - k, k);   /* save tail */
        copy_n(self, k, self, 0, n - k);  /* shift array right by k */
        copy_n(self, 0, tmp, 0, k);       /* copy stored tail at front */
    }
    else if (tmp->nbits == n - k) {  /* head is smaller */
        copy_n(tmp, 0, self, 0, n - k);   /* save head */
        copy_n(self, 0, self, n - k, k);  /* shift array left by n-k */
        copy_n(self, k, tmp, 0, n - k);   /* copy stored head at end */
    }
    else {
        Py_UNREACHABLE();
    }
}

static PyObject *
pauliebits_rotate(pauliebitsobject *self, PyObject *args)
{
    pauliebitsobject *tmp;
    Py_ssize_t n, k = 1;
    int err = 1;

    RAISE_IF_READONLY(self, NULL);
    if (!PyArg_ParseTuple(args, "|n:rotate", &k))
        return NULL;

    Py_BEGIN_CRITICAL_SECTION(self);
    n = self->nbits;
    Py_END_CRITICAL_SECTION();

    if (n < 2)
        Py_RETURN_NONE;

    k %= n;  /* C remainder may be negative, as it preserves the sign of k */
    if (k < 0)
        k += n;  /* make it equivalent to Python's k %= n */
    if (k == 0)
        Py_RETURN_NONE;

    assert(0 < k && k < n);

    /* temporary pauliebits to store head or tail (whichever is smaller) */
    tmp = newpauliebitsobject(&Pauliebits_Type, Py_MIN(k, n - k), self->endian);
    if (tmp == NULL)
        return NULL;

    Py_BEGIN_CRITICAL_SECTION(self);
    if (self->nbits == n) {
        rotate_lock_held(self, tmp, k);
        err = 0;
    }
    Py_END_CRITICAL_SECTION();
    Py_DECREF(tmp);

    if (err) {
        PyErr_SetString(PyExc_RuntimeError,
                        "pauliebits changed size during .rotate()");
        return NULL;
    }
    Py_RETURN_NONE;
}

PyDoc_STRVAR(rotate_doc,
"rotate(k=1, /)\n\
\n\
Rotate pauliebits in-place by `k` positions.\n\
Positive `k` rotates right, negative `k` rotates left.");


static PyObject *
pauliebits_sizeof(pauliebitsobject *self)
{
    Py_ssize_t res;

    Py_BEGIN_CRITICAL_SECTION(self);
    res = sizeof(pauliebitsobject) + self->allocated;
    if (self->buffer)
        res += sizeof(Py_buffer);
    Py_END_CRITICAL_SECTION();
    return PyLong_FromSsize_t(res);
}

PyDoc_STRVAR(sizeof_doc, "Return size of pauliebits object in bytes.");


/* private method - called only when frozenpauliebits is initialized to
   disallow memoryviews to change the buffer */
static PyObject *
pauliebits_freeze(pauliebitsobject *self)
{
    if (self->buffer) {
        assert(self->buffer->readonly == self->readonly);
        if (self->readonly == 0) {
            PyErr_SetString(PyExc_TypeError, "cannot import writable "
                            "buffer into frozenpauliebits");
            return NULL;
        }
    }
    set_padbits(self);
    self->readonly = 1;
    Py_RETURN_NONE;
}

/* -------- pauliebits methods exposed in debug mode for testing ---------- */

#ifndef NDEBUG

static PyObject *
pauliebits_shift_r8(pauliebitsobject *self, PyObject *args)
{
    Py_ssize_t a, b;
    int n;

    if (!PyArg_ParseTuple(args, "nni", &a, &b, &n))
        return NULL;

    Py_BEGIN_CRITICAL_SECTION(self);
    shift_r8(self, a, b, n);
    Py_END_CRITICAL_SECTION();
    Py_RETURN_NONE;
}

static PyObject *
pauliebits_copy_n(pauliebitsobject *self, PyObject *args)
{
    PyObject *other;
    Py_ssize_t a, b, n;

    if (!PyArg_ParseTuple(args, "nO!nn", &a, &Pauliebits_Type, &other, &b, &n))
        return NULL;

    Py_BEGIN_CRITICAL_SECTION2(self, other);
    copy_n(self, a, (pauliebitsobject *) other, b, n);
    Py_END_CRITICAL_SECTION2();
    Py_RETURN_NONE;
}

static PyObject *
pauliebits_overlap(pauliebitsobject *self, PyObject *other)
{
    int res;

    assert(pauliebits_Check(other));

    Py_BEGIN_CRITICAL_SECTION2(self, other);
    res = buffers_overlap(self, (pauliebitsobject *) other);
    Py_END_CRITICAL_SECTION2();

    return PyBool_FromLong(res);
}

#endif  /* NDEBUG */

/* ---------------------- pauliebits getset members ---------------------- */

static PyObject *
pauliebits_get_endian(pauliebitsobject *self, void *Py_UNUSED(ignored))
{
    return PyUnicode_FromString(ENDIAN_STR(self->endian));
}

static PyObject *
pauliebits_get_nbytes(pauliebitsobject *self, void *Py_UNUSED(ignored))
{
    return PyLong_FromSsize_t(Py_SIZE(self));
}

static PyObject *
pauliebits_get_padbits(pauliebitsobject *self, void *Py_UNUSED(ignored))
{
    return PyLong_FromSsize_t(PADBITS(self));
}

static PyObject *
pauliebits_get_readonly(pauliebitsobject *self, void *Py_UNUSED(ignored))
{
    return PyBool_FromLong(self->readonly);
}

static PyGetSetDef pauliebits_getsets[] = {
    {"endian", (getter) pauliebits_get_endian, NULL,
     PyDoc_STR("bit-endianness as Unicode string")},
    {"nbytes", (getter) pauliebits_get_nbytes, NULL,
     PyDoc_STR("buffer size in bytes")},
    {"padbits", (getter) pauliebits_get_padbits, NULL,
     PyDoc_STR("number of pad bits")},
    {"readonly", (getter) pauliebits_get_readonly, NULL,
     PyDoc_STR("bool indicating whether buffer is read-only")},
    {NULL, NULL, NULL, NULL}
};

/* ----------------------- pauliebits_as_sequence ------------------------ */

static Py_ssize_t
pauliebits_len(pauliebitsobject *self)
{
    return self->nbits;
}

static PyObject *
pauliebits_concat(pauliebitsobject *self, PyObject *other)
{
    pauliebitsobject *res;
    int ret;

    Py_BEGIN_CRITICAL_SECTION(self);
    res = pauliebits_cp(self);
    Py_END_CRITICAL_SECTION();

    if (res == NULL)
        return NULL;

    if (pauliebits_Check(other)) {
        Py_BEGIN_CRITICAL_SECTION(other);
        ret = extend_pauliebits(res, (pauliebitsobject *) other);
        Py_END_CRITICAL_SECTION();
    }
    else {
        ret = extend_dispatch(res, other);
    }

    if (ret < 0) {
        Py_DECREF(res);
        return NULL;
    }
    return freeze_if_frozen(res);
}

static PyObject *
pauliebits_repeat(pauliebitsobject *self, Py_ssize_t n)
{
    pauliebitsobject *res;

    Py_BEGIN_CRITICAL_SECTION(self);
    res = pauliebits_cp(self);
    Py_END_CRITICAL_SECTION();

    if (res == NULL)
        return NULL;

    if (repeat(res, n) < 0) {
        Py_DECREF(res);
        return NULL;
    }
    return freeze_if_frozen(res);
}

static PyObject *
pauliebits_item_lock_held(pauliebitsobject *self, Py_ssize_t i)
{
    if (i < 0 || i >= self->nbits) {
        PyErr_SetString(PyExc_IndexError, "pauliebits index out of range");
        return NULL;
    }
    return PyLong_FromLong(getbit(self, i));
}

static PyObject *
pauliebits_item(pauliebitsobject *self, Py_ssize_t i)
{
    PyObject *res;

    Py_BEGIN_CRITICAL_SECTION(self);
    res = pauliebits_item_lock_held(self, i);
    Py_END_CRITICAL_SECTION();

    return res;
}

/* vi is 0 or 1 for assignment, and 2 for deletion. */
static int
pauliebits_ass_item_lock_held(pauliebitsobject *self, Py_ssize_t i, int vi)
{
    if (i < 0 || i >= self->nbits) {
        PyErr_SetString(PyExc_IndexError,
                        "pauliebits assignment index out of range");
        return -1;
    }

    if (vi == 2) {
        return delete_n(self, i, 1);
    }

    setbit(self, i, vi);
    return 0;
}

static int
pauliebits_ass_item(pauliebitsobject *self, Py_ssize_t i, PyObject *value)
{
    int vi, res;

    RAISE_IF_READONLY(self, -1);
    if (value != NULL && !conv_pybit(value, &vi))
        return -1;

    if (value == NULL)  /* delete item */
        vi = 2;

    Py_BEGIN_CRITICAL_SECTION(self);
    res = pauliebits_ass_item_lock_held(self, i, vi);
    Py_END_CRITICAL_SECTION();

    return res;
}

/* return 1 if value (which can be an int or pauliebits) is in self,
   0 otherwise, and -1 on error */
static int
pauliebits_contains(pauliebitsobject *self, PyObject *value)
{
    Py_ssize_t pos;

    if (PyIndex_Check(value)) {
        int vi;

        if (!conv_pybit(value, &vi))
            return -1;

        Py_BEGIN_CRITICAL_SECTION(self);
        pos = find_bit(self, vi, 0, self->nbits, 0);
        Py_END_CRITICAL_SECTION();
    }
    else if (pauliebits_Check(value)) {
        Py_BEGIN_CRITICAL_SECTION2(self, value);
        pos = find_sub(self, (pauliebitsobject *) value, 0, self->nbits, 0);
        Py_END_CRITICAL_SECTION2();
    }
    else {
        PyErr_Format(PyExc_TypeError, "sub_pauliebits must be pauliebits or "
                     "int, not '%s'", Py_TYPE(value)->tp_name);
        return -1;
    }
    return pos >= 0;
}

static PyObject *
pauliebits_inplace_concat(pauliebitsobject *self, PyObject *other)
{
    RAISE_IF_READONLY(self, NULL);
    if (extend_thread_safe(self, other) < 0)
        return NULL;
    Py_INCREF(self);
    return (PyObject *) self;
}

static PyObject *
pauliebits_inplace_repeat(pauliebitsobject *self, Py_ssize_t n)
{
    int ret;

    RAISE_IF_READONLY(self, NULL);
    Py_BEGIN_CRITICAL_SECTION(self);
    ret = repeat(self, n);
    Py_END_CRITICAL_SECTION();

    if (ret < 0)
        return NULL;
    Py_INCREF(self);
    return (PyObject *) self;
}

static PySequenceMethods pauliebits_as_sequence = {
    (lenfunc) pauliebits_len,                     /* sq_length */
    (binaryfunc) pauliebits_concat,               /* sq_concat */
    (ssizeargfunc) pauliebits_repeat,             /* sq_repeat */
    (ssizeargfunc) pauliebits_item,               /* sq_item */
    0,                                          /* sq_slice */
    (ssizeobjargproc) pauliebits_ass_item,        /* sq_ass_item */
    0,                                          /* sq_ass_slice */
    (objobjproc) pauliebits_contains,             /* sq_contains */
    (binaryfunc) pauliebits_inplace_concat,       /* sq_inplace_concat */
    (ssizeargfunc) pauliebits_inplace_repeat,     /* sq_inplace_repeat */
};

/* ----------------------- pauliebits_as_mapping ------------------------- */

/* return new pauliebits with item in self, specified by slice indices */
static PyObject *
getslice_indices_lock_held(pauliebitsobject *self, Py_ssize_t start,
                           Py_ssize_t step, Py_ssize_t slicelength)
{
    pauliebitsobject *res;

    res = newpauliebitsobject(Py_TYPE(self), slicelength, self->endian);
    if (res == NULL)
        return NULL;

    if (step == 1) {
        copy_n(res, 0, self, start, slicelength);
    }
    else {
        Py_ssize_t i, j;

        for (i = 0, j = start; i < slicelength; i++, j += step)
            setbit(res, i, getbit(self, j));
    }
    return freeze_if_frozen(res);
}

/* return new pauliebits with item in self, specified by slice */
static PyObject *
getslice(pauliebitsobject *self, PyObject *slice)
{
    PyObject *res;
    Py_ssize_t start, stop, step, slicelength;

    assert(PySlice_Check(slice));
    if (PySlice_Unpack(slice, &start, &stop, &step) < 0)
        return NULL;

    Py_BEGIN_CRITICAL_SECTION(self);
    slicelength = PySlice_AdjustIndices(self->nbits, &start, &stop, step);
    res = getslice_indices_lock_held(self, start, step, slicelength);
    Py_END_CRITICAL_SECTION();

    return res;
}

static int
ensure_mask_size(pauliebitsobject *self, pauliebitsobject *mask)
{
    if (self->nbits != mask->nbits) {
        PyErr_Format(PyExc_IndexError, "pauliebits length is %zd, but "
                     "mask has length %zd", self->nbits, mask->nbits);
        return -1;
    }
    return 0;
}

static PyObject *
getmask_lock_held(pauliebitsobject *self, pauliebitsobject *mask)
{
    pauliebitsobject *res;
    Py_ssize_t n, i, j;

    if (ensure_mask_size(self, mask) < 0)
        return NULL;

    n = count_span(mask, 0, mask->nbits);
    res = newpauliebitsobject(Py_TYPE(self), n, self->endian);
    if (res == NULL)
        return NULL;

    for (i = j = 0; i < mask->nbits; i++) {
        if (getbit(mask, i))
            setbit(res, j++, getbit(self, i));
    }
    assert(j == n);
    return (PyObject *) res;
}

/* return a new pauliebits with items from 'self' masked by pauliebits 'mask' */
static PyObject *
getmask(pauliebitsobject *self, pauliebitsobject *mask)
{
    PyObject *res = NULL;

    Py_BEGIN_CRITICAL_SECTION2(self, mask);
    res = getmask_lock_held(self, mask);
    Py_END_CRITICAL_SECTION2();

    if (res == NULL)
        return NULL;
    return freeze_if_frozen((pauliebitsobject *) res);
}

/* Return j-th item from sequence.  The item is considered an index into
   an array with given length, and is normalized pythonically.
   On failure, an exception is set and -1 is returned. */
static Py_ssize_t
index_from_seq(PyObject *sequence, Py_ssize_t j, Py_ssize_t length)
{
    PyObject *item;
    Py_ssize_t i;

    item = PySequence_GetItem(sequence, j);
    if (item == NULL)
        return -1;

    i = PyNumber_AsSsize_t(item, PyExc_IndexError);
    Py_DECREF(item);
    if (i == -1 && PyErr_Occurred())
        return -1;
    if (i < 0)
        i += length;
    if (i < 0 || i >= length) {
        PyErr_SetString(PyExc_IndexError, "pauliebits index out of range");
        return -1;
    }
    return i;
}

/* Convert 'seq' to an array of indices normalized to 'length'.
   On success, store the allocated array in '*indices', its number of
   elements in '*size', and return 0.  The array may be NULL when
   '*size' is zero and must be freed with PyMem_Free().
   On error, leave the output arguments unchanged and return -1 with
   an exception set. */
static int
sequence_as_array(PyObject *seq, Py_ssize_t length,
                  Py_ssize_t **indices, Py_ssize_t *size)
{
    Py_ssize_t *p = NULL;
    Py_ssize_t n, j;

    n = PySequence_Size(seq);  /* may execute arbitrary Python code */
    if (n < 0)
        return -1;

    if (n != 0) {
        p = PyMem_New(Py_ssize_t, n);
        if (p == NULL) {
            PyErr_NoMemory();
            return -1;
        }
    }

    for (j = 0; j < n; j++) {
        Py_ssize_t i = index_from_seq(seq, j, length);
        if (i < 0) {
            PyMem_Free(p);
            return -1;
        }
        p[j] = i;
    }

    *indices = p;
    *size = n;
    return 0;
}

/* return a new pauliebits with items from 'self' listed by
   sequence (of indices) 'seq' */
static PyObject *
getsequence(pauliebitsobject *self, PyObject *seq)
{
    pauliebitsobject *res;
    Py_ssize_t *indices = NULL;
    Py_ssize_t nbits, n, j;
    int err = 1;

    Py_BEGIN_CRITICAL_SECTION(self);
    nbits = self->nbits;
    Py_END_CRITICAL_SECTION();

    if (sequence_as_array(seq, nbits, &indices, &n) < 0)
        return NULL;

    res = newpauliebitsobject(Py_TYPE(self), n, self->endian);
    if (res == NULL)
        goto error;

    Py_BEGIN_CRITICAL_SECTION(self);
    if (self->nbits == nbits) {
        for (j = 0; j < n; j++)
            setbit(res, j, getbit(self, indices[j]));
        err = 0;
    }
    Py_END_CRITICAL_SECTION();

    PyMem_Free(indices);

    if (err) {
        Py_DECREF(res);
        PyErr_SetString(PyExc_RuntimeError,
                        "pauliebits changed size during sequence indexing");
        return NULL;
    }
    return freeze_if_frozen(res);

 error:
    PyMem_Free(indices);
    return NULL;
}

static int
subscr_seq_check(PyObject *item)
{
    if (PyTuple_Check(item)) {
        PyErr_SetString(PyExc_TypeError, "multiple dimensions not supported");
        return -1;
    }
    if (PySequence_Check(item))
        return 0;

    PyErr_Format(PyExc_TypeError, "pauliebits subscript must be an index, "
                 "slice or sequence, not '%s'",
                 Py_TYPE(item)->tp_name);
    return -1;
}

static PyObject *
pauliebits_subscr(pauliebitsobject *self, PyObject *item)
{
    if (PyIndex_Check(item)) {
        PyObject *res;
        Py_ssize_t i;

        i = PyNumber_AsSsize_t(item, PyExc_IndexError);
        if (i == -1 && PyErr_Occurred())
            return NULL;

        Py_BEGIN_CRITICAL_SECTION(self);
        if (i < 0)
            i += self->nbits;
        res = pauliebits_item_lock_held(self, i);
        Py_END_CRITICAL_SECTION();

        return res;
    }

    if (PySlice_Check(item))
        return getslice(self, item);

    if (pauliebits_Check(item))
        return getmask(self, (pauliebitsobject *) item);

    if (subscr_seq_check(item) < 0)
        return NULL;

    return getsequence(self, item);
}

/* The following functions are called from assign_slice(). */

static int
setslice_lock_held(pauliebitsobject *self, pauliebitsobject *other,
                   Py_ssize_t start, Py_ssize_t stop, Py_ssize_t step)
{
    Py_ssize_t slicelength, increase;

    slicelength = PySlice_AdjustIndices(self->nbits, &start, &stop, step);

    /* number of bits by which self has to be increased (decreased) */
    increase = other->nbits - slicelength;

    if (step == 1) {
        if (increase > 0) {        /* increase self */
            if (insert_n(self, start + slicelength, increase) < 0)
                return -1;
        }
        if (increase < 0) {        /* decrease self */
            if (delete_n(self, start + other->nbits, -increase) < 0)
                return -1;
        }
        /* copy new values into self */
        copy_n(self, start, other, 0, other->nbits);
    }
    else {
        Py_ssize_t i, j;

        if (increase != 0) {
            PyErr_Format(PyExc_ValueError, "attempt to assign sequence of "
                         "size %zd to extended slice of size %zd",
                         other->nbits, slicelength);
            return -1;
        }
        for (i = 0, j = start; i < slicelength; i++, j += step)
            setbit(self, j, getbit(other, i));
    }
    return 0;
}

/* set items in self, specified by slice, to other pauliebits */
static int
setslice_pauliebits(pauliebitsobject *self, PyObject *slice,
                  pauliebitsobject *other)
{
    pauliebitsobject *copy = NULL;
    pauliebitsobject *src = other;
    Py_ssize_t start, stop, step;
    int res;

    assert(PySlice_Check(slice));
    if (PySlice_Unpack(slice, &start, &stop, &step) < 0)
        return -1;

    Py_BEGIN_CRITICAL_SECTION2(self, other);
    /* Make a copy of other, in case the buffers overlap.  This is obviously
       the case when self and other are the same object, but can also happen
       when the two pauliebits share memory. */
    if (buffers_overlap(self, other)) {
        copy = pauliebits_cp(other);
        src = copy;
    }
    if (src == NULL)
        res = -1;  /* pauliebits_cp() failed */
    else
        res = setslice_lock_held(self, src, start, stop, step);
    Py_END_CRITICAL_SECTION2();

    Py_XDECREF(copy);
    return res;
}

/* set items in self, specified by slice, to value */
static int
setslice_bool(pauliebitsobject *self, PyObject *slice, PyObject *value)
{
    Py_ssize_t start, stop, step, slicelength;
    int vi;

    assert(PySlice_Check(slice) && PyIndex_Check(value));
    if (!conv_pybit(value, &vi))
        return -1;

    if (PySlice_Unpack(slice, &start, &stop, &step) < 0)
        return -1;

    Py_BEGIN_CRITICAL_SECTION(self);
    slicelength = PySlice_AdjustIndices(self->nbits, &start, &stop, step);
    adjust_step_positive(slicelength, &start, &stop, &step);
    set_range(self, start, stop, step, vi);
    Py_END_CRITICAL_SECTION();
    return 0;
}

static int
delslice_lock_held(pauliebitsobject *self,
                   Py_ssize_t start, Py_ssize_t stop, Py_ssize_t step)
{
    Py_ssize_t slicelength;

    slicelength = PySlice_AdjustIndices(self->nbits, &start, &stop, step);
    adjust_step_positive(slicelength, &start, &stop, &step);

    if (step > 1) {
        /* set items not to be removed (up to stop) */
        Py_ssize_t i = start + 1, j = start;

        if (step >= 4) {
            for (; i < stop; i += step) {
                Py_ssize_t length = Py_MIN(step - 1, stop - i);
                copy_n(self, j, self, i, length);
                j += length;
            }
        }
        else {
            for (; i < stop; i++) {
                if ((i - start) % step != 0)
                    setbit(self, j++, getbit(self, i));
            }
        }
        assert(slicelength == 0 || j == stop - slicelength);
    }
    return delete_n(self, stop - slicelength, slicelength);
}

/* delete items in self, specified by slice */
static int
delslice(pauliebitsobject *self, PyObject *slice)
{
    Py_ssize_t start, stop, step;
    int ret;

    if (PySlice_Unpack(slice, &start, &stop, &step) < 0)
        return -1;

    Py_BEGIN_CRITICAL_SECTION(self);
    ret = delslice_lock_held(self, start, stop, step);
    Py_END_CRITICAL_SECTION();
    return ret;
}

/* assign slice of pauliebits self to value */
static int
assign_slice(pauliebitsobject *self, PyObject *slice, PyObject *value)
{
    if (value == NULL)
        return delslice(self, slice);

    if (pauliebits_Check(value))
        return setslice_pauliebits(self, slice, (pauliebitsobject *) value);

    if (PyIndex_Check(value))
        return setslice_bool(self, slice, value);

    PyErr_Format(PyExc_TypeError, "pauliebits or int expected for slice "
                 "assignment, not '%s'", Py_TYPE(value)->tp_name);
    return -1;
}

/* The following functions are called from assign_mask(). */

/* assign mask of pauliebits self to pauliebits other */
static int
setmask_pauliebits_lock_held(pauliebitsobject *self, pauliebitsobject *mask,
                           pauliebitsobject *other)
{
    Py_ssize_t n, i, j;

    assert(self->nbits == mask->nbits);

    n = count_span(mask, 0, mask->nbits);  /* mask size */
    if (n != other->nbits) {
        PyErr_Format(PyExc_IndexError, "attempt to assign mask of size %zd "
                     "to pauliebits of size %zd", n, other->nbits);
        return -1;
    }

    for (i = j = 0; i < mask->nbits; i++) {
        if (getbit(mask, i))
            setbit(self, i, getbit(other, j++));
    }
    assert(j == n);
    return 0;
}

static int
setmask_pauliebits(pauliebitsobject *self, pauliebitsobject *mask,
                 pauliebitsobject *other)
{
    pauliebitsobject *src;
    int res = -1;

#ifdef Py_GIL_DISABLED
    /* copy other so the operation below only needs to lock self and mask */
    Py_BEGIN_CRITICAL_SECTION(other);
    src = pauliebits_cp(other);
    Py_END_CRITICAL_SECTION();
#else
    src = (pauliebitsobject *) Py_NewRef(other);
#endif

    if (src == NULL)
        return -1;

    Py_BEGIN_CRITICAL_SECTION2(self, mask);
    if (ensure_mask_size(self, mask) == 0)
        res = setmask_pauliebits_lock_held(self, mask, src);
    Py_END_CRITICAL_SECTION2();

    Py_DECREF(src);
    return res;
}

/* assign mask of pauliebits self to boolean value */
static int
setmask_bool(pauliebitsobject *self, pauliebitsobject *mask, PyObject *value)
{
    static char *expr[] = {"a &= ~mask",  /* a[mask] = 0 */
                           "a |= mask"};  /* a[mask] = 1 */
    int vi;

    if (!conv_pybit(value, &vi))
        return -1;

    PyErr_Format(PyExc_NotImplementedError, "mask assignment to bool not "
                 "implemented;\n`a[mask] = %d` equivalent to `%s`",
                 vi, expr[vi]);
    return -1;
}

/* delete items in self, specified by mask */
static int
delmask_lock_held(pauliebitsobject *self, pauliebitsobject *mask)
{
    Py_ssize_t nbits = self->nbits, cnt;
    Py_ssize_t n = 0, i;

    assert(nbits == mask->nbits);

    cnt = count_span(mask, 0, nbits);
    if (cnt == 0)      /* no bits in mask are 1 - do nothing */
        return resize(self, nbits);  /* check for BufferError */

    if (cnt == nbits)  /* all bits in mask are 1 - remove everything */
        return resize(self, 0);      /* clear */

    if (cnt == 1) {    /* mask has one bit 1 - find its position and delete */
        i = find_bit(mask, 1, 0, nbits, 0);
        assert(i >= 0);
        return delete_n(self, i, 1);
    }

    for (i = 0; i < nbits; i++) {
        if (getbit(mask, i) == 0)  /* set items we want to keep */
            setbit(self, n++, getbit(self, i));
    }
    assert(n == nbits - cnt);

    return resize(self, n);
}

static int
delmask(pauliebitsobject *self, pauliebitsobject *mask)
{
    int res = -1;

    Py_BEGIN_CRITICAL_SECTION2(self, mask);
    if (ensure_mask_size(self, mask) == 0)
        res = delmask_lock_held(self, mask);
    Py_END_CRITICAL_SECTION2();

    return res;
}

/* assign mask of pauliebits self to value */
static int
assign_mask(pauliebitsobject *self, pauliebitsobject *mask, PyObject *value)
{
    if (value == NULL)
        return delmask(self, mask);

    if (pauliebits_Check(value))
        return setmask_pauliebits(self, mask, (pauliebitsobject *) value);

    if (PyIndex_Check(value))
        return setmask_bool(self, mask, value);

    PyErr_Format(PyExc_TypeError, "pauliebits or int expected for mask "
                 "assignment, not '%s'", Py_TYPE(value)->tp_name);
    return -1;
}

/* The following functions are called from assign_sequence(). */

/* assign sequence (of indices) of pauliebits self to pauliebits */
static int
setseq_pauliebits(pauliebitsobject *self, PyObject *seq, pauliebitsobject *other)
{
    pauliebitsobject *copy = NULL;
    pauliebitsobject *src = other;
    Py_ssize_t *indices = NULL;
    Py_ssize_t nbits, n;
    int res = -1;

    Py_BEGIN_CRITICAL_SECTION(self);
    nbits = self->nbits;
    Py_END_CRITICAL_SECTION();

    if (sequence_as_array(seq, nbits, &indices, &n) < 0)
        return -1;

    Py_BEGIN_CRITICAL_SECTION2(self, other);
    if (n != other->nbits) {
        PyErr_Format(PyExc_ValueError, "attempt to assign sequence of "
                     "size %zd to pauliebits of size %zd", n, other->nbits);
    }
    else if (self->nbits != nbits) {
        PyErr_SetString(PyExc_RuntimeError,
                        "pauliebits changed size during sequence indexing");
    }
    else {
        /* Make a copy of other, see comment in setslice_pauliebits(). */
        if (buffers_overlap(self, other)) {
            copy = pauliebits_cp(other);
            src = copy;
        }
        if (src) {
            Py_ssize_t j;
            for (j = 0; j < n; j++)
                setbit(self, indices[j], getbit(src, j));
            res = 0;
        }
    }
    Py_END_CRITICAL_SECTION2();

    PyMem_Free(indices);
    Py_XDECREF(copy);
    return res;
}

/* assign sequence (of indices) of pauliebits self to Boolean value */
static int
setseq_bool(pauliebitsobject *self, PyObject *seq, PyObject *value)
{
    Py_ssize_t *indices = NULL;
    Py_ssize_t nbits, n;
    int res = -1, vi;

    if (!conv_pybit(value, &vi))
        return -1;

    Py_BEGIN_CRITICAL_SECTION(self);
    nbits = self->nbits;
    Py_END_CRITICAL_SECTION();

    if (sequence_as_array(seq, nbits, &indices, &n) < 0)
        return -1;

    Py_BEGIN_CRITICAL_SECTION(self);
    if (self->nbits == nbits) {
        Py_ssize_t j;
        for (j = 0; j < n; j++)
            setbit(self, indices[j], vi);
        res = 0;
    }
    Py_END_CRITICAL_SECTION();

    if (res < 0)
        PyErr_SetString(PyExc_RuntimeError,
                        "pauliebits changed size during sequence indexing");

    PyMem_Free(indices);
    return res;
}

/* Materialize 'seq' into indices normalized to 'length' as
   a pauliebits of 'length'. */
static pauliebitsobject *
sequence_as_pauliebits(PyObject *seq, Py_ssize_t length)
{
    pauliebitsobject *res;
    Py_ssize_t n, j;

    n = PySequence_Size(seq);  /* may execute arbitrary Python code */
    if (n < 0)
        return NULL;

    res = newpauliebitsobject(&Pauliebits_Type, length, ENDIAN_DEFAULT);
    if (res == NULL)
        return NULL;

    if (res->ob_item)
        memset(res->ob_item, 0x00, (size_t) Py_SIZE(res));

    /* set indices from sequence */
    for (j = 0; j < n; j++) {
        Py_ssize_t i = index_from_seq(seq, j, length);
        if (i < 0) {
            Py_DECREF(res);
            return NULL;
        }
        setbit(res, i, 1);
    }
    return res;
}

/* delete items in self, specified by sequence of indices */
static int
delsequence(pauliebitsobject *self, PyObject *seq)
{
    Py_ssize_t nbits;
    pauliebitsobject *mask;  /* temporary pauliebits masking items to remove */
    int res = -1;

    Py_BEGIN_CRITICAL_SECTION(self);
    nbits = self->nbits;
    Py_END_CRITICAL_SECTION();

    mask = sequence_as_pauliebits(seq, nbits);
    if (mask == NULL)
        return -1;

    Py_BEGIN_CRITICAL_SECTION(self);
    if (self->nbits == nbits) {
        res = delmask_lock_held(self, mask);  /* do actual work here */
    }
    else {
        PyErr_SetString(PyExc_RuntimeError,
                        "pauliebits changed size during sequence indexing");
    }
    Py_END_CRITICAL_SECTION();

    Py_DECREF(mask);
    return res;
}

/* assign sequence (of indices) of pauliebits self to value */
static int
assign_sequence(pauliebitsobject *self, PyObject *seq, PyObject *value)
{
    assert(PySequence_Check(seq));

    if (value == NULL)
        return delsequence(self, seq);

    if (pauliebits_Check(value))
        return setseq_pauliebits(self, seq, (pauliebitsobject *) value);

    if (PyIndex_Check(value))
        return setseq_bool(self, seq, value);

    PyErr_Format(PyExc_TypeError, "pauliebits or int expected for sequence "
                 "assignment, not '%s'", Py_TYPE(value)->tp_name);
    return -1;
}

static int
pauliebits_ass_subscr(pauliebitsobject *self, PyObject *item, PyObject *value)
{
    RAISE_IF_READONLY(self, -1);

    if (PyIndex_Check(item)) {
        Py_ssize_t i;
        int vi, res;

        i = PyNumber_AsSsize_t(item, PyExc_IndexError);
        if (i == -1 && PyErr_Occurred())
            return -1;

        if (value != NULL && !conv_pybit(value, &vi))
            return -1;

        if (value == NULL)  /* delete item */
            vi = 2;

        Py_BEGIN_CRITICAL_SECTION(self);
        if (i < 0)
            i += self->nbits;
        res = pauliebits_ass_item_lock_held(self, i, vi);
        Py_END_CRITICAL_SECTION();

        return res;
    }

    if (PySlice_Check(item))
        return assign_slice(self, item, value);

    if (pauliebits_Check(item))
        return assign_mask(self, (pauliebitsobject *) item, value);

    if (subscr_seq_check(item) < 0)
        return -1;

    return assign_sequence(self, item, value);
}

static PyMappingMethods pauliebits_as_mapping = {
    (lenfunc) pauliebits_len,
    (binaryfunc) pauliebits_subscr,
    (objobjargproc) pauliebits_ass_subscr,
};

/* --------------------------- pauliebits_as_number ---------------------- */

static PyObject *
pauliebits_cpinvert(pauliebitsobject *self)
{
    pauliebitsobject *res;

    Py_BEGIN_CRITICAL_SECTION(self);
    res = pauliebits_cp(self);
    Py_END_CRITICAL_SECTION();

    if (res == NULL)
        return NULL;

    invert_span(res, 0, res->nbits);
    return freeze_if_frozen(res);
}

/* perform bitwise in-place operation */
static void
bitwise(pauliebitsobject *self, pauliebitsobject *other, const char oper)
{
    const Py_ssize_t nbytes = Py_SIZE(self);
    const Py_ssize_t cwords = nbytes / 8;      /* complete 64-bit words */
    Py_ssize_t i;
    char *buff_s = self->ob_item;
    char *buff_o = other->ob_item;
    uint64_t *wbuff_s = WBUFF(self);
    uint64_t *wbuff_o = WBUFF(other);

    assert(self->nbits == other->nbits);
    assert(self->endian == other->endian);
    assert(self->readonly == 0);
    switch (oper) {
    case '&':
        for (i = 0; i < cwords; i++)
            wbuff_s[i] &= wbuff_o[i];
        for (i = 8 * cwords; i < nbytes; i++)
            buff_s[i] &= buff_o[i];
        break;

    case '|':
        for (i = 0; i < cwords; i++)
            wbuff_s[i] |= wbuff_o[i];
        for (i = 8 * cwords; i < nbytes; i++)
            buff_s[i] |= buff_o[i];
        break;

    case '^':
        for (i = 0; i < cwords; i++)
            wbuff_s[i] ^= wbuff_o[i];
        for (i = 8 * cwords; i < nbytes; i++)
            buff_s[i] ^= buff_o[i];
        break;

    default:
        Py_UNREACHABLE();
    }
}

/* Return 0 if both a and b are pauliebits objects with same length and
   bit-endianness.  Otherwise, set exception and return -1. */
static int
bitwise_check(PyObject *a, PyObject *b, const char *ostr)
{
    if (!pauliebits_Check(a) || !pauliebits_Check(b)) {
        PyErr_Format(PyExc_TypeError,
                     "unsupported operand type(s) for %s: '%s' and '%s'",
                     ostr, Py_TYPE(a)->tp_name, Py_TYPE(b)->tp_name);
        return -1;
    }
    return ensure_eq_size_endian((pauliebitsobject *) a, (pauliebitsobject *) b);
}

#define BITWISE_FUNC(name, inplace, ostr)                   \
static PyObject *                                           \
pauliebits_ ## name (PyObject *self, PyObject *other)         \
{                                                           \
    pauliebitsobject *res = NULL;                             \
                                                            \
    if (inplace)                                            \
        RAISE_IF_READONLY(self, NULL);                      \
                                                            \
    Py_BEGIN_CRITICAL_SECTION2(self, other);                \
    if (bitwise_check(self, other, ostr) == 0) {            \
        res = inplace                                       \
            ? (pauliebitsobject *) Py_NewRef(self)            \
            : pauliebits_cp((pauliebitsobject *) self);         \
                                                            \
        if (res)                                            \
            bitwise(res, (pauliebitsobject *) other, *ostr);  \
    }                                                       \
    Py_END_CRITICAL_SECTION2();                             \
    if (res == NULL)                                        \
        return NULL;                                        \
    if (!inplace)                                           \
        return freeze_if_frozen(res);                       \
    return (PyObject *) res;                                \
}

BITWISE_FUNC(and,  0, "&")   /* pauliebits_and */
BITWISE_FUNC(or,   0, "|")   /* pauliebits_or  */
BITWISE_FUNC(xor,  0, "^")   /* pauliebits_xor */
BITWISE_FUNC(iand, 1, "&=")  /* pauliebits_iand */
BITWISE_FUNC(ior,  1, "|=")  /* pauliebits_ior  */
BITWISE_FUNC(ixor, 1, "^=")  /* pauliebits_ixor */


/* shift pauliebits n positions to left (right=0) or right (right=1) */
static void
shift(pauliebitsobject *self, Py_ssize_t n, int right)
{
    const Py_ssize_t nbits = self->nbits;

    assert(n >= 0 && self->readonly == 0);
    if (n > nbits)
        n = nbits;

    assert(n <= nbits);
    if (right) {                /* rshift */
        copy_n(self, n, self, 0, nbits - n);
        set_span(self, 0, n, 0);
    }
    else {                      /* lshift */
        copy_n(self, 0, self, n, nbits - n);
        set_span(self, nbits - n, nbits, 0);
    }
}

/* check shift arguments and return shift count, -1 on error */
static Py_ssize_t
shift_check(PyObject *self, PyObject *other, const char *ostr)
{
    Py_ssize_t n;

    if (!pauliebits_Check(self) || !PyIndex_Check(other)) {
        PyErr_Format(PyExc_TypeError,
                     "unsupported operand type(s) for %s: '%s' and '%s'",
                     ostr, Py_TYPE(self)->tp_name, Py_TYPE(other)->tp_name);
        return -1;
    }
    n = PyNumber_AsSsize_t(other, PyExc_OverflowError);
    if (n == -1 && PyErr_Occurred())
        return -1;

    if (n < 0) {
        PyErr_SetString(PyExc_ValueError, "negative shift count");
        return -1;
    }
    return n;
}

#define SHIFT_FUNC(name, inplace, ostr)                     \
static PyObject *                                           \
pauliebits_ ## name (PyObject *self, PyObject *other)         \
{                                                           \
    pauliebitsobject *res = NULL;                             \
    Py_ssize_t n;                                           \
                                                            \
    if ((n = shift_check(self, other, ostr)) < 0)           \
        return NULL;                                        \
                                                            \
    if (inplace)                                            \
        RAISE_IF_READONLY(self, NULL);                      \
                                                            \
    Py_BEGIN_CRITICAL_SECTION(self);                        \
    res = inplace                                           \
        ? (pauliebitsobject *) Py_NewRef(self)                \
        : pauliebits_cp((pauliebitsobject *) self);             \
                                                            \
    if (res)                                                \
        shift(res, n, *ostr == '>');                        \
    Py_END_CRITICAL_SECTION();                              \
                                                            \
    if (res == NULL)                                        \
        return NULL;                                        \
    if (!inplace)                                           \
        return freeze_if_frozen(res);                       \
    return (PyObject *) res;                                \
}

SHIFT_FUNC(lshift,  0, "<<")  /* pauliebits_lshift */
SHIFT_FUNC(rshift,  0, ">>")  /* pauliebits_rshift */
SHIFT_FUNC(ilshift, 1, "<<=") /* pauliebits_ilshift */
SHIFT_FUNC(irshift, 1, ">>=") /* pauliebits_irshift */


static PyNumberMethods pauliebits_as_number = {
    0,                                   /* nb_add */
    0,                                   /* nb_subtract */
    0,                                   /* nb_multiply */
    0,                                   /* nb_remainder */
    0,                                   /* nb_divmod */
    0,                                   /* nb_power */
    0,                                   /* nb_negative */
    0,                                   /* nb_positive */
    0,                                   /* nb_absolute */
    0,                                   /* nb_bool (was nb_nonzero) */
    (unaryfunc) pauliebits_cpinvert,       /* nb_invert */
    (binaryfunc) pauliebits_lshift,        /* nb_lshift */
    (binaryfunc) pauliebits_rshift,        /* nb_rshift */
    (binaryfunc) pauliebits_and,           /* nb_and */
    (binaryfunc) pauliebits_xor,           /* nb_xor */
    (binaryfunc) pauliebits_or,            /* nb_or */
    0,                                   /* nb_int */
    0,                                   /* nb_reserved (was nb_long) */
    0,                                   /* nb_float */
    0,                                   /* nb_inplace_add */
    0,                                   /* nb_inplace_subtract */
    0,                                   /* nb_inplace_multiply */
    0,                                   /* nb_inplace_remainder */
    0,                                   /* nb_inplace_power */
    (binaryfunc) pauliebits_ilshift,       /* nb_inplace_lshift */
    (binaryfunc) pauliebits_irshift,       /* nb_inplace_rshift */
    (binaryfunc) pauliebits_iand,          /* nb_inplace_and */
    (binaryfunc) pauliebits_ixor,          /* nb_inplace_xor */
    (binaryfunc) pauliebits_ior,           /* nb_inplace_or */
    0,                                   /* nb_floor_divide */
    0,                                   /* nb_true_divide */
    0,                                   /* nb_inplace_floor_divide */
    0,                                   /* nb_inplace_true_divide */
    0,                                   /* nb_index */
};

/**************************************************************************
                    variable length encoding and decoding
 **************************************************************************/

static int
check_codedict(PyObject *codedict)
{
    if (!PyDict_Check(codedict)) {
        PyErr_Format(PyExc_TypeError, "dict expected, got '%s'",
                     Py_TYPE(codedict)->tp_name);
        return -1;
    }
    if (PyDict_Size(codedict) == 0) {
        PyErr_SetString(PyExc_ValueError, "non-empty dict expected");
        return -1;
    }
    return 0;
}

static int
check_value(PyObject *value)
{
     if (!pauliebits_Check(value)) {
         PyErr_SetString(PyExc_TypeError, "pauliebits expected for dict value");
         return -1;
     }
     if (((pauliebitsobject *) value)->nbits == 0) {
         PyErr_SetString(PyExc_ValueError, "non-empty pauliebits expected");
         return -1;
     }
     return 0;
}

static PyObject *
pauliebits_encode(pauliebitsobject *self, PyObject *args)
{
    PyObject *codedict, *iterable, *iter, *symbol, *value;

    RAISE_IF_READONLY(self, NULL);
    if (!PyArg_ParseTuple(args, "OO:encode", &codedict, &iterable))
        return NULL;

    if (check_codedict(codedict) < 0)
        return NULL;

    iter = PyObject_GetIter(iterable);
    if (iter == NULL)
        return PyErr_Format(PyExc_TypeError, "'%s' object is not iterable",
                            Py_TYPE(iterable)->tp_name);

    /* extend self with the pauliebits from codedict */
    while ((symbol = PyIter_Next(iter))) {
        int ret;

        if (PyDict_GetItemRef(codedict, symbol, &value) < 0)
            goto error;

        if (value == NULL) {
            PyErr_Format(PyExc_ValueError,
                         "symbol not defined in prefix code: %A", symbol);
            goto error;
        }
        Py_BEGIN_CRITICAL_SECTION2(self, value);
        ret = check_value(value);
        if (ret == 0)
            ret = extend_pauliebits(self, (pauliebitsobject *) value);
        Py_END_CRITICAL_SECTION2();
        if (ret < 0)
            goto error;

        Py_DECREF(symbol);
        Py_DECREF(value);
    }
    Py_DECREF(iter);
    if (PyErr_Occurred())       /* from PyIter_Next() */
        return NULL;
    Py_RETURN_NONE;

 error:
    Py_DECREF(iter);
    Py_DECREF(symbol);
    Py_XDECREF(value);
    return NULL;
}


PyDoc_STRVAR(encode_doc,
"encode(code, iterable, /)\n\
\n\
Given a prefix code (a dict mapping symbols to pauliebits),\n\
iterate over the iterable object with symbols, and extend pauliebits\n\
with corresponding pauliebits for each symbol.");


static PyObject *
pauliebits_encode_ixyz(pauliebitsobject *self, PyObject *args)
{
    PyObject *str_obj;
    if (!PyArg_ParseTuple(args, "U", &str_obj)) {
        return NULL;
    }
    if (self->readonly) {
        PyErr_SetString(PyExc_BufferError, "cannot resize read-only pauliebits");
        return NULL;
    }

    Py_ssize_t len = PyUnicode_GET_LENGTH(str_obj);
    if (len == 0) {
        if (resize(self, 0) < 0) {
            return NULL;
        }
        self->endian = 1; // Дефолтный Big Endian для пустой строки
        Py_RETURN_NONE;
    }

    Py_ssize_t nbits = len * 2;
    Py_ssize_t nbytes = (nbits + 7) / 8;

    char *buffer = (char *)PyMem_Malloc(nbytes);
    if (buffer == NULL) {
        return PyErr_NoMemory();
    }
    memset(buffer, 0, nbytes);

    Py_ssize_t byte_idx = 0;
    Py_ssize_t i = 0;

    // --- Быстрая обработка блоками по 4 символа (заполняем ровно 1 байт) ---
    Py_ssize_t fast_len = len - (len % 4); 
    for (; i < fast_len; i += 4) {
        char b0 = 0, b1 = 0, b2 = 0, b3 = 0;
        
        Py_UCS4 c0 = PyUnicode_READ_CHAR(str_obj, i);
        Py_UCS4 c1 = PyUnicode_READ_CHAR(str_obj, i + 1);
        Py_UCS4 c2 = PyUnicode_READ_CHAR(str_obj, i + 2);
        Py_UCS4 c3 = PyUnicode_READ_CHAR(str_obj, i + 3);

        // Маппинг: I=0, Z=1, X=2, Y=3 (младший бит — even, старший — odd)
        b0 = (c0 == 'Z') ? 1 : (c0 == 'X') ? 2 : (c0 == 'Y') ? 3 : 0;
        b1 = (c1 == 'Z') ? 1 : (c1 == 'X') ? 2 : (c1 == 'Y') ? 3 : 0;
        b2 = (c2 == 'Z') ? 1 : (c2 == 'X') ? 2 : (c2 == 'Y') ? 3 : 0;
        b3 = (c3 == 'Z') ? 1 : (c3 == 'X') ? 2 : (c3 == 'Y') ? 3 : 0;

        // Пакуем в стиле Big Endian (первый символ в старшие биты байта)
        buffer[byte_idx++] = (b0 << 6) | (b1 << 4) | (b2 << 2) | b3;
    }

    // --- Обработка остатка строки (если длина не кратна 4) ---
    int bit_shift = 6;
    for (; i < len; i++) {
        Py_UCS4 ch = PyUnicode_READ_CHAR(str_obj, i);
        
        char val = (ch == 'Z') ? 1 : (ch == 'X') ? 2 : (ch == 'Y') ? 3 : 0;

        buffer[byte_idx] |= (val << bit_shift);
        bit_shift -= 2;
        if (bit_shift < 0) {
            bit_shift = 6;
            byte_idx++;
        }
    }

    // Изменяем размер исходного объекта под новые данные
    if (resize(self, nbits) < 0) {
        PyMem_Free(buffer);
        return NULL;
    }

    // ПРИНУДИТЕЛЬНО выставляем endian = 1 (Big Endian), так как наш буфер упакован именно так!
    self->endian = 1;

    // Копируем подготовленные байты во внутренний буфер объекта
    memcpy(self->ob_item, buffer, nbytes);
    PyMem_Free(buffer);

    // Гарантированная очистка «хвоста» последнего байта (стандарт bitarray для предотвращения мусора)
    if (nbits & 7) {
        size_t last_byte_idx = (size_t)(nbits >> 3);
        int bits_in_last_byte = (int)(nbits & 7);
        uint8_t mask = (uint8_t)(0xFF << (8 - bits_in_last_byte));
        ((unsigned char *)self->ob_item)[last_byte_idx] &= mask;
    }

    Py_RETURN_NONE;
}

PyDoc_STRVAR(encode_ixyz_doc,
"encode(str)\n\
\n\
Optimized 2-bit-per-symbol encoding of IXYZ strings.");



/* Кроссплатформенный popcount для 64-битных чисел */
#if defined(_MSC_VER)
    #include <intrin.h>
    #define POPCOUNT64(x) __popcnt64(x)
#elif defined(__GNUC__) || defined(__clang__)
    #define POPCOUNT64(x) __builtin_popcountll(x)
#else
    /* Запасной софтверный вариант, если компилятор экзотический */
    static inline int POPCOUNT64(unsigned long long v) {
        v = v - ((v >> 1) & 0x5555555555555555ULL);
        v = (v & 0x3333333333333333ULL) + ((v >> 2) & 0x3333333333333333ULL);
        return (int)((((v + (v >> 4)) & 0xF0F0F0F0F0F0F0FULL) * 0x101010101010101ULL) >> 56);
    }
#endif

static PyObject *
pauliebits_commutes_with(pauliebitsobject *self, PyObject *args)
{
    PyObject *other_obj;
    /* Проверяем тип аргумента (Pauliebits_Type — стандартное имя в pauliebits) */
    if (!PyArg_ParseTuple(args, "O!", &Pauliebits_Type, &other_obj)) {
        return NULL;
    }
    pauliebitsobject *other = (pauliebitsobject *)other_obj;

    Py_ssize_t self_bits = self->nbits;
    Py_ssize_t other_bits = other->nbits;
    Py_ssize_t min_bits = (self_bits < other_bits) ? self_bits : other_bits;
    Py_ssize_t nbytes = (min_bits + 7) / 8;

    if (nbytes == 0) {
        Py_RETURN_TRUE;
    }

    unsigned long long total_ones_1 = 0;
    unsigned long long total_ones_2 = 0;

    Py_ssize_t i = 0;
    /* Обработка блоками по 8 байт (64 бита) */
    Py_ssize_t fast_bytes = nbytes - (nbytes % 8);

    for (; i < fast_bytes; i += 8) {
        unsigned long long s_word;
        unsigned long long o_word;

        /* Безопасное копирование памяти (работает без Bus Error на ARM/Linux/macOS) */
        memcpy(&s_word, self->ob_item + i, 8);
        memcpy(&o_word, other->ob_item + i, 8);

        unsigned long long even_mask = 0x5555555555555555ULL;

        /* 1. count_and(self.even, other.odd) */
        unsigned long long and_1 = (s_word & even_mask) & (o_word >> 1);
        total_ones_1 += POPCOUNT64(and_1);

        /* 2. count_and(other.even, self.odd) */
        unsigned long long and_2 = (o_word & even_mask) & (s_word >> 1);
        total_ones_2 += POPCOUNT64(and_2);
    }

    /* Обработка оставшихся байт (< 8 байт) */
    for (; i < nbytes; i++) {
        unsigned char s_byte = (unsigned char)self->ob_item[i];
        unsigned char o_byte = (unsigned char)other->ob_item[i];

        /* Если это последний байт и он неполный, отсекаем лишнее */
        if (i == nbytes - 1 && (min_bits % 8) != 0) {
            int extra_bits = 8 - (min_bits % 8);
            /* Маска для Big-Endian (оригинальный pauliebits использует его по умолчанию) */
            unsigned char tail_mask = 0xFF << extra_bits; 
            s_byte &= tail_mask;
            o_byte &= tail_mask;
        }

        total_ones_1 += POPCOUNT64((s_byte & 0x55) & (o_byte >> 1));
        total_ones_2 += POPCOUNT64((o_byte & 0x55) & (s_byte >> 1));
    }

    /* Проверка равенства четностей */
    if ((total_ones_1 % 2) == (total_ones_2 % 2)) {
        Py_RETURN_TRUE;
    } else {
        Py_RETURN_FALSE;
    }
}

PyDoc_STRVAR(commutes_with_doc,
"commutes_with(pauliebits)\n\
\n\
Checking two strings for commutativity");


static PyObject *
pauliebits_count_non_trivially(pauliebitsobject *self, PyObject *Py_UNUSED(ignored))
{
    // Количество пар битов
    size_t num_pairs = (size_t)(self->nbits / 2);
    if (num_pairs == 0) {
        return PyLong_FromLong(0);
    }

    size_t total_count = 0;

    // Вспомогательный макрос чтения бита с учетом Big/Little Endian
    #ifndef GET_BIT
    #define GET_BIT(ptr, index, endian) \
        ((endian == 1) ? \
         (((const unsigned char *)(ptr))[(index) >> 3] & (0x80 >> ((index) & 7))) : \
         (((const unsigned char *)(ptr))[(index) >> 3] & (0x01 << ((index) & 7))))
    #endif

    // Идем по каждой паре битов
    for (size_t i = 0; i < num_pairs; i++) {
        size_t even_idx = i * 2;
        size_t odd_idx = i * 2 + 1;

        // Извлекаем четный и нечетный бит из буфера
        int even_bit = GET_BIT(self->ob_item, even_idx, self->endian) ? 1 : 0;
        int odd_bit  = GET_BIT(self->ob_item, odd_idx, self->endian) ? 1 : 0;

        // Если хотя бы один из битов в паре равен 1 (операция OR), инкрементируем счетчик
        if (even_bit | odd_bit) {
            total_count++;
        }
    }

    return PyLong_FromSize_t(total_count);
}

PyDoc_STRVAR(count_non_trivially_doc,
"count_non_trivially(pauliebits)\n\
\n\
count_or for even odd bits");

static PyObject *
pauliebits_diagonal_index(pauliebitsobject *self, PyObject *Py_UNUSED(ignored))
{
    if (self->nbits == 0) {
        return PyLong_FromLong(0);
    }

    size_t num_bytes = (self->nbits + 7) >> 3;
    uint8_t *buffer = (uint8_t *)self->ob_item;

    const uint8_t EVEN_MASK = 0x55; 
    const uint8_t ODD_MASK  = 0xAA; 

    for (size_t i = 0; i < num_bytes; i++) {
        if (buffer[i] & EVEN_MASK) {
            return PyLong_FromLong(-1);
        }
    }

    size_t result_bits = self->nbits / 2;
    if (result_bits == 0) {
        return PyLong_FromLong(0);
    }

    size_t res_bytes = (result_bits + 7) >> 3;
    
    uint8_t *res_buffer = (uint8_t *)PyMem_Malloc(res_bytes);
    if (res_buffer == NULL) {
        return PyErr_NoMemory();
    }
    memset(res_buffer, 0, res_bytes);
    size_t bit_idx = 0;
    for (size_t i = 0; i < num_bytes; i++) {
        uint8_t b = buffer[i];
        
        for (int step = 1; step < 8; step += 2) {
            if (((size_t)i * 8 + (size_t)step) >= (size_t)self->nbits) {
                break;
            }

            if (b & (1 << step)) {
                res_buffer[bit_idx >> 3] |= (1 << (bit_idx & 7));
            }
            bit_idx++;
        }
    }
    PyObject *result = _PyLong_FromByteArray(res_buffer, res_bytes, 
                                            1,
                                            0 );

    PyMem_Free(res_buffer);
    return result;
}



PyDoc_STRVAR(diagonal_index_doc,
"diagonal_index(pauliebits)\n\
\n\
diagonal index");

static PyObject *
pauliebits_phase(pauliebitsobject *self, PyObject *arg)
{
    if (!PyObject_TypeCheck(arg, Py_TYPE(self)) && 
        Py_TYPE(arg)->tp_basicsize != Py_TYPE(self)->tp_basicsize) {
        PyErr_SetString(PyExc_TypeError, "Argument must be a pauliebits object");
        return NULL;
    }
    
    pauliebitsobject *other = (pauliebitsobject *)arg;

    if (self->nbits != other->nbits) {
        PyErr_SetString(PyExc_ValueError, "pauliebits objects must have the same length");
        return NULL;
    }

    if (self->nbits == 0) {
        return PyLong_FromLong(0);
    }

    #ifndef GET_BIT
    #define GET_BIT(ptr, index, endian) \
        ((endian == 1) ? \
         (((const unsigned char *)(ptr))[(index) >> 3] & (0x80 >> ((index) & 7))) : \
         (((const unsigned char *)(ptr))[(index) >> 3] & (0x01 << ((index) & 7))))
    #endif

    size_t sum_1 = 0; // count_and(bits_even, other_bits_odd)
    size_t sum_2 = 0; // count_and(bits_odd, bits_even)
    size_t sum_3 = 0; // count_and(other_bits_odd, other_bits_even)
    size_t sum_4 = 0; // count_and(bits_even ^ other_bits_even, bits_odd ^ other_bits_odd)

    size_t res_bits = (size_t)(self->nbits / 2);

    for (size_t i = 0; i < res_bits; i++) {
        size_t even_idx = i * 2;
        size_t odd_idx = i * 2 + 1;

        int s_even = GET_BIT(self->ob_item, even_idx, self->endian) ? 1 : 0;
        int s_odd  = GET_BIT(self->ob_item, odd_idx, self->endian) ? 1 : 0;

        int o_even = GET_BIT(other->ob_item, even_idx, other->endian) ? 1 : 0;
        int o_odd  = GET_BIT(other->ob_item, odd_idx, other->endian) ? 1 : 0;

        if (s_even & o_odd)  sum_1++;
        if (s_odd  & s_even) sum_2++;
        if (o_odd  & o_even) sum_3++;
        if ((s_even ^ o_even) & (s_odd ^ o_odd)) sum_4++;
    }

    long long final_result = (2 * (long long)sum_1) + (long long)sum_2 + (long long)sum_3 - (long long)sum_4;

    return PyLong_FromLongLong(final_result);
}

PyDoc_STRVAR(phase_doc,
"phase(pauliebits)\n\
\n\
phase");


static PyObject *
pauliebits_complex_conjugate(pauliebitsobject *self, PyObject *Py_UNUSED(ignored))
{
    size_t num_pairs = (size_t)(self->nbits / 2);
    if (num_pairs == 0) {
        return PyLong_FromLong(0);
    }

    size_t total_ys = 0;

    #ifndef GET_BIT
    #define GET_BIT(ptr, index, endian) \
        ((endian == 1) ? \
         (((const unsigned char *)(ptr))[(index) >> 3] & (0x80 >> ((index) & 7))) : \
         (((const unsigned char *)(ptr))[(index) >> 3] & (0x01 << ((index) & 7))))
    #endif

    for (size_t i = 0; i < num_pairs; i++) {
        size_t even_idx = i * 2;
        size_t odd_idx = i * 2 + 1;

        int even_bit = GET_BIT(self->ob_item, even_idx, self->endian) ? 1 : 0;
        int odd_bit  = GET_BIT(self->ob_item, odd_idx, self->endian) ? 1 : 0;

        if (even_bit & odd_bit) {
            total_ys++;
        }
    }

    return PyLong_FromSize_t(total_ys);
}

PyDoc_STRVAR(complex_conjugate_doc,
"complex_conjugate(pauliebits)\n\
\n\
complex conjugate");

static PyObject *
pauliebits_not_identity_mask(pauliebitsobject *self, PyObject *Py_UNUSED(ignored))
{
    size_t res_bits = (size_t)(self->nbits / 2);
    
    // 1. Создаем пустой объект pauliebits через системный аллокатор типов
    pauliebitsobject *res = (pauliebitsobject *)Py_TYPE(self)->tp_alloc(Py_TYPE(self), 0);
    if (res == NULL) {
        return NULL;
    }

    // Инициализируем базовые поля структуры
    res->nbits = 0;
    res->ob_item = NULL;
    res->allocated = 0;
    res->endian = 1; // Устанавливаем дефолтный Big Endian, как у всех срезов
    res->ob_exports = 0;
    res->weakreflist = NULL;
    res->buffer = NULL;
    res->readonly = 0;

    if (res_bits == 0) {
        return (PyObject *)res;
    }

    // 2. Используем внутреннюю функцию библиотеки для выделения памяти.
    // Она сама выставит правильное значение res->allocated и подготовит res->ob_item.
    // (Убедитесь, что функция resize доступна в вашей области видимости)
    if (resize(res, (Py_ssize_t)res_bits) < 0) {
        Py_DECREF(res);
        return NULL;
    }

    // Дополнительно гарантированно очищаем выделенный буфер
    size_t res_bytes = (size_t)res->allocated;
    memset(res->ob_item, 0, res_bytes);

    // Вспомогательные макросы для корректной работы с Big и Little Endian
    #ifndef GET_BIT
    #define GET_BIT(ptr, index, endian) \
        ((endian == 1) ? \
         (((const unsigned char *)(ptr))[(index) >> 3] & (0x80 >> ((index) & 7))) : \
         (((const unsigned char *)(ptr))[(index) >> 3] & (0x01 << ((index) & 7))))
    #endif

    #ifndef SET_BIT_ON
    #define SET_BIT_ON(ptr, index, endian) \
        ((endian == 1) ? \
         (((unsigned char *)(ptr))[(index) >> 3] |= (0x80 >> ((index) & 7))) : \
         (((unsigned char *)(ptr))[(index) >> 3] |= (0x01 << ((index) & 7))))
    #endif

    // 3. Заполняем результирующий массив
    for (size_t i = 0; i < res_bits; i++) {
        size_t even_idx = i * 2;
        size_t odd_idx = i * 2 + 1;

        // Читаем из self с его оригинальным endian
        int even_bit = GET_BIT(self->ob_item, even_idx, self->endian) ? 1 : 0;
        int odd_bit  = GET_BIT(self->ob_item, odd_idx, self->endian) ? 1 : 0;

        // Записываем в res строго в режиме Big Endian (1)
        if (even_bit | odd_bit) {
            SET_BIT_ON(res->ob_item, i, 1);
        }
    }

    // 4. Математически точная очистка неиспользуемых бит в последнем байте
    if (res_bits & 7) {
        size_t last_byte_idx = res_bits >> 3;
        int bits_in_last_byte = res_bits & 7;
        uint8_t mask = (uint8_t)(0xFF << (8 - bits_in_last_byte));
        ((unsigned char *)res->ob_item)[last_byte_idx] &= mask;
    }

    return (PyObject *)res;
}

PyDoc_STRVAR(not_identity_mask_doc,
"not_identity_mask(pauliebits)\n\
\n\
even | odd");

static PyObject *
pauliebits_decode_ixyz(pauliebitsobject *self, PyObject *Py_UNUSED(ignored))
{
    // Длина результирующей строки равна количеству ПАР битов
    size_t length = (size_t)(self->nbits / 2);
    if (length == 0) {
        return PyUnicode_FromString("");
    }

    // Создаем Python-строку нужной длины
    PyObject *res_string = PyUnicode_New(length, 127);
    if (res_string == NULL) {
        return NULL;
    }
    
    Py_UCS1 *str_buf = PyUnicode_1BYTE_DATA(res_string);
    char *buffer = self->ob_item;

    // Массив подстановок согласно вашей таблице DECODEC:
    // Индекс: (odd_bit << 1) | even_bit
    // 0: (0,0)->I, 1: (1,0)->X, 2: (0,1)->Z, 3: (1,1)->Y
    const char DECODE_MAP[4] = {'I', 'X', 'Z', 'Y'};

    // Вспомогательный макрос для чтения конкретного логического бита.
    // Он проверяет поле self->endian и правильно лезет в байт.
    // Если в вашем файле макрос называется по-другому (например, BIT), используйте его.
    #ifndef GET_BIT
    #define GET_BIT(ptr, index, endian) \
        ((endian == 1) ? /* big endian */ \
         (((const unsigned char *)(ptr))[(index) >> 3] & (0x80 >> ((index) & 7))) : \
         (((const unsigned char *)(ptr))[(index) >> 3] & (0x01 << ((index) & 7))))
    #endif

    for (size_t i = 0; i < length; i++) {
        size_t even_idx = i * 2;
        size_t odd_idx = i * 2 + 1;

        // Извлекаем значения битов (0 или 1) с учетом порядка битов в вашем pauliebits
        uint8_t even_bit = GET_BIT(buffer, even_idx, self->endian) ? 1 : 0;
        uint8_t odd_bit  = GET_BIT(buffer, odd_idx, self->endian) ? 1 : 0;

        // Рассчитываем индекс в таблице DECODEC
        uint8_t lookup_idx = (odd_bit << 1) | even_bit;

        // Записываем символ прямо в строку Python
        str_buf[i] = (Py_UCS1)DECODE_MAP[lookup_idx];
    }

    return res_string;
}

PyDoc_STRVAR(decode_ixyz_doc,
"decode_ixyz(pauliebits)\n\
\n\
decode ixyz");


/* ----------------------- binary tree (C-level) ----------------------- */

/* a node has either children or a symbol, NEVER both */
typedef struct _bin_node
{
    struct _bin_node *child[2];
    PyObject *symbol;
} binode;


static binode *
binode_new(void)
{
    binode *nd;

    nd = PyMem_New(binode, 1);
    if (nd == NULL) {
        PyErr_NoMemory();
        return NULL;
    }
    nd->child[0] = NULL;
    nd->child[1] = NULL;
    nd->symbol = NULL;
    return nd;
}

static void
binode_delete(binode *nd)
{
    if (nd == NULL)
        return;

    binode_delete(nd->child[0]);
    binode_delete(nd->child[1]);
    Py_XDECREF(nd->symbol);
    PyMem_Free((void *) nd);
}

/* insert symbol (mapping to pauliebits a) into tree */
static int
binode_insert_symbol(binode *tree, pauliebitsobject *a, PyObject *symbol)
{
    binode *nd = tree, *prev;
    Py_ssize_t i;

    for (i = 0; i < a->nbits; i++) {
        int k = getbit(a, i);

        prev = nd;
        nd = nd->child[k];

        if (nd) {
            if (nd->symbol)     /* we cannot have already a symbol */
                goto ambiguity;
        }
        else {            /* if node does not exist, create new one */
            nd = binode_new();
            if (nd == NULL)
                return -1;
            prev->child[k] = nd;
        }
    }
    /* the new leaf node cannot already have a symbol or children */
    if (nd->symbol || nd->child[0] || nd->child[1])
        goto ambiguity;

    nd->symbol = symbol;
    Py_INCREF(symbol);
    return 0;

 ambiguity:
    PyErr_Format(PyExc_ValueError, "prefix code ambiguous: %A", symbol);
    return -1;
}

/* return a binary tree from a codedict, which is created by inserting
   all symbols mapping to pauliebits */
static binode *
binode_make_tree(PyObject *codedict)
{
    binode *tree;
    PyObject *symbol, *value;
    Py_ssize_t pos = 0;
    int ret = 0;

    tree = binode_new();
    if (tree == NULL)
        return NULL;

    Py_BEGIN_CRITICAL_SECTION(codedict);
    while (PyDict_Next(codedict, &pos, &symbol, &value)) {
        /* Keep the current borrowed references alive if a helper suspends
         * the critical section. */
        Py_INCREF(symbol);
        Py_INCREF(value);
        Py_BEGIN_CRITICAL_SECTION(value);
        ret = check_value(value);
        if (ret == 0) {
            ret = binode_insert_symbol(tree, (pauliebitsobject *) value,
                                       symbol);
        }
        Py_END_CRITICAL_SECTION();
        Py_DECREF(value);
        Py_DECREF(symbol);

        if (ret < 0)
            break;
    }
    Py_END_CRITICAL_SECTION();

    if (ret < 0) {
        binode_delete(tree);
        return NULL;
    }

    /* as we require the codedict to be non-empty the tree cannot be empty */
    assert(tree);
    return tree;
}

/* Traverse using the branches corresponding to bits in ba, starting
   at *indexp.  Return the symbol at the leaf node, or NULL when the end
   of the pauliebits has been reached.  On error, set the appropriate exception
   and also return NULL.
*/
static PyObject *
binode_traverse(binode *tree, pauliebitsobject *ba, Py_ssize_t *indexp)
{
    binode *nd = tree;
    Py_ssize_t start = *indexp;

    while (*indexp < ba->nbits) {
        assert(nd);
        nd = nd->child[getbit(ba, *indexp)];
        if (nd == NULL)
            return PyErr_Format(PyExc_ValueError,
                                "prefix code unrecognized in pauliebits "
                                "at position %zd .. %zd", start, *indexp);
        (*indexp)++;
        if (nd->symbol) {       /* leaf */
            assert(nd->child[0] == NULL && nd->child[1] == NULL);
            return nd->symbol;
        }
    }
    if (nd != tree)
        PyErr_Format(PyExc_ValueError,
                     "incomplete prefix code at position %zd", start);
    return NULL;
}

/* add the node's symbol to given dict */
static int
binode_to_dict(binode *nd, PyObject *dict, pauliebitsobject *prefix)
{
    int k;

    if (nd == NULL)
        return 0;

    if (nd->symbol) {
        assert(nd->child[0] == NULL && nd->child[1] == NULL);
        return PyDict_SetItem(dict, nd->symbol, (PyObject *) prefix);
    }

    for (k = 0; k < 2; k++) {
        pauliebitsobject *t;      /* prefix of the two child nodes */
        int ret;

        t = pauliebits_cp(prefix);
        if (t == NULL)
            return -1;
        if (resize(t, t->nbits + 1) < 0) {
            Py_DECREF(t);
            return -1;
        }
        setbit(t, t->nbits - 1, k);
        ret = binode_to_dict(nd->child[k], dict, t);
        Py_DECREF(t);
        if (ret < 0)
            return -1;
    }
    return 0;
}

/* return whether node is complete (has both children or is a symbol node) */
static int
binode_complete(binode *nd)
{
    if (nd == NULL)
        return 0;

    if (nd->symbol) {
        /* symbol node cannot have children */
        assert(nd->child[0] == NULL && nd->child[1] == NULL);
        return 1;
    }

    return (binode_complete(nd->child[0]) &&
            binode_complete(nd->child[1]));
}

/* return number of nodes */
static Py_ssize_t
binode_nodes(binode *nd)
{
    Py_ssize_t res;

    if (nd == NULL)
        return 0;

    /* a node cannot have a symbol and children */
    assert(!(nd->symbol && (nd->child[0] || nd->child[1])));
    /* a node must have a symbol or children */
    assert(nd->symbol || nd->child[0] || nd->child[1]);

    res = 1;
    res += binode_nodes(nd->child[0]);
    res += binode_nodes(nd->child[1]);
    return res;
}

/******************************** decodetree ******************************/

typedef struct {
    PyObject_HEAD
    binode *tree;
} decodetreeobject;


static PyObject *
decodetree_new(PyTypeObject *type, PyObject *args, PyObject *kwds)
{
    binode *tree;
    PyObject *codedict, *obj;

    if (!PyArg_ParseTuple(args, "O:decodetree", &codedict))
        return NULL;

    if (check_codedict(codedict) < 0)
        return NULL;

    tree = binode_make_tree(codedict);
    if (tree == NULL)
        return NULL;

    obj = type->tp_alloc(type, 0);
    if (obj == NULL) {
        binode_delete(tree);
        return NULL;
    }
    ((decodetreeobject *) obj)->tree = tree;

    return obj;
}

static PyObject *
decodetree_todict(decodetreeobject *self)
{
    PyObject *dict;
    pauliebitsobject *prefix;

    dict = PyDict_New();
    if (dict == NULL)
        return NULL;

    prefix = newpauliebitsobject(&Pauliebits_Type, 0, ENDIAN_DEFAULT);
    if (prefix == NULL)
        goto error;

    if (binode_to_dict(self->tree, dict, prefix) < 0)
        goto error;

    Py_DECREF(prefix);
    return dict;

 error:
    Py_DECREF(dict);
    Py_XDECREF(prefix);
    return NULL;
}

PyDoc_STRVAR(todict_doc,
"todict() -> dict\n\
\n\
Return a dict mapping the symbols to pauliebits.  This dict is a\n\
reconstruction of the code dict which the object was created with.");


static PyObject *
decodetree_complete(decodetreeobject *self)
{
    return PyBool_FromLong(binode_complete(self->tree));
}

PyDoc_STRVAR(complete_doc,
"complete() -> bool\n\
\n\
Return whether tree is complete.  That is, whether or not all\n\
nodes have both children (unless they are symbol nodes).");


static PyObject *
decodetree_nodes(decodetreeobject *self)
{
    return PyLong_FromSsize_t(binode_nodes(self->tree));
}

PyDoc_STRVAR(nodes_doc,
"nodes() -> int\n\
\n\
Return number of nodes in tree (internal and symbol nodes).");


static PyObject *
decodetree_sizeof(decodetreeobject *self)
{
    Py_ssize_t res;

    res = sizeof(decodetreeobject);
    res += sizeof(binode) * binode_nodes(self->tree);
    return PyLong_FromSsize_t(res);
}

static void
decodetree_dealloc(decodetreeobject *self)
{
    binode_delete(self->tree);
    Py_TYPE(self)->tp_free((PyObject *) self);
}

/* These methods are mostly useful for debugging and testing.  We provide
   docstrings, but they are not mentioned in the documentation, and are not
   part of the API */
static PyMethodDef decodetree_methods[] = {
    {"complete",   (PyCFunction) decodetree_complete, METH_NOARGS,
     complete_doc},
    {"nodes",      (PyCFunction) decodetree_nodes,    METH_NOARGS,
     nodes_doc},
    {"todict",     (PyCFunction) decodetree_todict,   METH_NOARGS,
     todict_doc},
    {"__sizeof__", (PyCFunction) decodetree_sizeof,   METH_NOARGS, 0},
    {NULL,         NULL}  /* sentinel */
};

PyDoc_STRVAR(decodetree_doc,
"decodetree(code, /) -> decodetree\n\
\n\
Given a prefix code (a dict mapping symbols to pauliebits),\n\
create a binary tree object to be passed to `.decode()`.");

static PyTypeObject DecodeTree_Type = {
    PyVarObject_HEAD_INIT(NULL, 0)
    "pauliebits.decodetree",                    /* tp_name */
    sizeof(decodetreeobject),                 /* tp_basicsize */
    0,                                        /* tp_itemsize */
    /* methods */
    (destructor) decodetree_dealloc,          /* tp_dealloc */
    0,                                        /* tp_print */
    0,                                        /* tp_getattr */
    0,                                        /* tp_setattr */
    0,                                        /* tp_compare */
    0,                                        /* tp_repr */
    0,                                        /* tp_as_number */
    0,                                        /* tp_as_sequence */
    0,                                        /* tp_as_mapping */
    PyObject_HashNotImplemented,              /* tp_hash */
    0,                                        /* tp_call */
    0,                                        /* tp_str */
    PyObject_GenericGetAttr,                  /* tp_getattro */
    0,                                        /* tp_setattro */
    0,                                        /* tp_as_buffer */
    Py_TPFLAGS_DEFAULT,                       /* tp_flags */
    decodetree_doc,                           /* tp_doc */
    0,                                        /* tp_traverse */
    0,                                        /* tp_clear */
    0,                                        /* tp_richcompare */
    0,                                        /* tp_weaklistoffset */
    0,                                        /* tp_iter */
    0,                                        /* tp_iternext */
    decodetree_methods,                       /* tp_methods */
    0,                                        /* tp_members */
    0,                                        /* tp_getset */
    0,                                        /* tp_base */
    0,                                        /* tp_dict */
    0,                                        /* tp_descr_get */
    0,                                        /* tp_descr_set */
    0,                                        /* tp_dictoffset */
    0,                                        /* tp_init */
    PyType_GenericAlloc,                      /* tp_alloc */
    decodetree_new,                           /* tp_new */
    PyObject_Del,                             /* tp_free */
};

#define DecodeTree_Check(op)  PyObject_TypeCheck(op, &DecodeTree_Type)

/* -------------------------- END decodetree --------------------------- */

/* return a binary tree from a decodetree or codedict */
static binode *
get_tree(PyObject *obj)
{
    if (DecodeTree_Check(obj))
        return ((decodetreeobject *) obj)->tree;

    if (check_codedict(obj) < 0)
        return NULL;

    return binode_make_tree(obj);
}

/*********************** (pauliebits) Decode Iterator ***********************/

typedef struct {
    PyObject_HEAD
    pauliebitsobject *self;       /* pauliebits we're decoding */
    binode *tree;               /* prefix tree containing symbols */
    Py_ssize_t index;           /* current index in pauliebits */
    PyObject *decodetree;       /* decodetree or NULL */
} decodeiterobject;

static PyTypeObject DecodeIter_Type;

/* create a new initialized pauliebits decode iterator object */
static PyObject *
pauliebits_decode(pauliebitsobject *self, PyObject *obj)
{
    decodeiterobject *it;       /* iterator to be returned */
    binode *tree;

    tree = get_tree(obj);
    if (tree == NULL)
        return NULL;

    it = PyObject_GC_New(decodeiterobject, &DecodeIter_Type);
    if (it == NULL) {
        if (!DecodeTree_Check(obj))
            binode_delete(tree);
        return NULL;
    }

    Py_INCREF(self);
    it->self = self;
    it->tree = tree;
    it->index = 0;
    it->decodetree = DecodeTree_Check(obj) ? obj : NULL;
    Py_XINCREF(it->decodetree);
    PyObject_GC_Track(it);
    return (PyObject *) it;
}

PyDoc_STRVAR(decode_doc,
"decode(code, /) -> decodeiterator\n\
\n\
Given a prefix code (a dict mapping symbols to pauliebits, or `decodetree`\n\
object), decode content of pauliebits and return an iterator over\n\
corresponding symbols.");


static PyObject *
decodeiter_next(decodeiterobject *it)
{
    PyObject *symbol;

    Py_BEGIN_CRITICAL_SECTION2(it, it->self);
    /* may be NULL when stop iteration OR error occurred */
    symbol = binode_traverse(it->tree, it->self, &(it->index));
    Py_END_CRITICAL_SECTION2();

    Py_XINCREF(symbol);
    return symbol;
}

static void
decodeiter_dealloc(decodeiterobject *it)
{
    PyObject_GC_UnTrack(it);
    if (it->decodetree)
        Py_DECREF(it->decodetree);
    else       /* when decodeiter was created from dict - free tree */
        binode_delete(it->tree);

    Py_DECREF(it->self);
    PyObject_GC_Del(it);
}

static int
decodeiter_traverse(decodeiterobject *it, visitproc visit, void *arg)
{
    Py_VISIT(it->self);
    Py_VISIT(it->decodetree);
    return 0;
}

static PyObject *
decodeiter_skipbits(decodeiterobject *it, PyObject *args)
{
    PyObject *skipped = NULL;
    Py_ssize_t n;  /* number of bits to skip */

    if (!PyArg_ParseTuple(args, "n:skipbits", &n))
        return NULL;

    if (n < 0)
        return PyErr_Format(PyExc_ValueError, "skip count cannot be "
                            "negative, got %zd", n);

    Py_BEGIN_CRITICAL_SECTION2(it, it->self);
    if (n <= it->self->nbits - it->index) {
        skipped = getslice_indices_lock_held(it->self, it->index, 1, n);
        if (skipped)
            it->index += n;
    }
    else {
        PyErr_Format(PyExc_ValueError, "skip count %zd cannot be "
                     "larger than remaining bits %zd",
                     n, it->self->nbits - it->index);
    }
    Py_END_CRITICAL_SECTION2();

    return skipped;
}

PyDoc_STRVAR(decodeiter_skipbits_doc,
"skipbits(n, /) -> pauliebits\n\
\n\
Skip over the next `n` bits and return them.\n\
Raises `ValueError` if count is out of range.");


static PyMethodDef decodeiter_methods[] = {
    {"skipbits",    (PyCFunction) decodeiter_skipbits, METH_VARARGS,
     decodeiter_skipbits_doc},
    {NULL}
};

static PyMemberDef decodeiter_members[] = {
    {"index", Py_T_PYSSIZET, offsetof(decodeiterobject, index), Py_READONLY,
     PyDoc_STR("current bit position to be decoded by subsequent `next`")},
    {NULL}
};

static PyTypeObject DecodeIter_Type = {
    PyVarObject_HEAD_INIT(NULL, 0)
    "pauliebits.decodeiterator",                /* tp_name */
    sizeof(decodeiterobject),                 /* tp_basicsize */
    0,                                        /* tp_itemsize */
    /* methods */
    (destructor) decodeiter_dealloc,          /* tp_dealloc */
    0,                                        /* tp_print */
    0,                                        /* tp_getattr */
    0,                                        /* tp_setattr */
    0,                                        /* tp_compare */
    0,                                        /* tp_repr */
    0,                                        /* tp_as_number */
    0,                                        /* tp_as_sequence */
    0,                                        /* tp_as_mapping */
    0,                                        /* tp_hash */
    0,                                        /* tp_call */
    0,                                        /* tp_str */
    PyObject_GenericGetAttr,                  /* tp_getattro */
    0,                                        /* tp_setattro */
    0,                                        /* tp_as_buffer */
    Py_TPFLAGS_DEFAULT | Py_TPFLAGS_HAVE_GC,  /* tp_flags */
    0,                                        /* tp_doc */
    (traverseproc) decodeiter_traverse,       /* tp_traverse */
    0,                                        /* tp_clear */
    0,                                        /* tp_richcompare */
    0,                                        /* tp_weaklistoffset */
    PyObject_SelfIter,                        /* tp_iter */
    (iternextfunc) decodeiter_next,           /* tp_iternext */
    decodeiter_methods,                       /* tp_methods */
    decodeiter_members,                       /* tp_members */
};

/*********************** (Pauliebits) Search Iterator ***********************/

/* Note: when .sub is NULL search for single bit value in member .vi */
typedef struct {
    PyObject_HEAD
    pauliebitsobject *self;   /* pauliebits we're searching in */
    pauliebitsobject *sub;    /* pauliebits being searched for */
    int vi;                 /* single bit being searched for */
    Py_ssize_t start;
    Py_ssize_t stop;
    int right;
} searchiterobject;

static PyTypeObject SearchIter_Type;

/* create a new initialized pauliebits search iterator object */
static PyObject *
pauliebits_search(pauliebitsobject *self, PyObject *args, PyObject *kwds)
{
    static char *kwlist[] = {"", "", "", "right", NULL};
    Py_ssize_t start = 0, stop = PY_SSIZE_T_MAX;
    int vi = -1, right = 0;
    PyObject *sub;
    searchiterobject *it;  /* iterator to be returned */

    if (!PyArg_ParseTupleAndKeywords(args, kwds, "O|nni", kwlist,
                                     &sub, &start, &stop, &right))
        return NULL;

    if (PyIndex_Check(sub)) {
        if (!conv_pybit(sub, &vi))
            return NULL;
    }
    else if (!pauliebits_Check(sub)) {
        return PyErr_Format(PyExc_TypeError, "sub_pauliebits must be pauliebits "
                            "or int, not '%s'", Py_TYPE(sub)->tp_name);
    }

    it = PyObject_GC_New(searchiterobject, &SearchIter_Type);
    if (it == NULL)
        return NULL;

    Py_INCREF(self);
    Py_BEGIN_CRITICAL_SECTION(self);
    PySlice_AdjustIndices(self->nbits, &start, &stop, 1);
    it->start = start;
    it->stop = stop;
    it->self = self;
    Py_END_CRITICAL_SECTION();

    it->sub = NULL;
    it->vi = vi;
    it->right = right;

    if (pauliebits_Check(sub)) {
        Py_INCREF(sub);
        it->sub = (pauliebitsobject *) sub;
    }
    PyObject_GC_Track(it);
    return (PyObject *) it;
}

PyDoc_STRVAR(search_doc,
"search(sub_pauliebits, start=0, stop=<end>, /, right=False) -> iterator\n\
\n\
Return iterator over indices where sub_pauliebits is found, such that\n\
sub_pauliebits is contained within `[start:stop]`.\n\
The indices are iterated in ascending order (from lowest to highest),\n\
unless `right=True`, which will iterate in descending order (starting with\n\
rightmost match).");


static PyObject *
searchiter_next(searchiterobject *it)
{
    Py_ssize_t start, stop, pos, width = 1;
    int right;

    Py_BEGIN_CRITICAL_SECTION(it);
    start = it->start;
    stop = it->stop;
    right = it->right;
    Py_END_CRITICAL_SECTION();

    assert(start >= 0);
    if (it->sub) {
        Py_BEGIN_CRITICAL_SECTION2(it->self, it->sub);
        if (start > it->self->nbits || stop < 0 || stop > it->self->nbits) {
            pos = -1;
        }
        else {
            width = it->sub->nbits;
            pos = find_sub(it->self, it->sub, start, stop, right);
        }
        Py_END_CRITICAL_SECTION2();
    }
    else {
        Py_BEGIN_CRITICAL_SECTION(it->self);
        if (start > it->self->nbits || stop < 0 || stop > it->self->nbits) {
            pos = -1;
        }
        else {
            pos = find_bit(it->self, it->vi, start, stop, right);
        }
        Py_END_CRITICAL_SECTION();
    }

    if (pos < 0)  /* no more positions -- stop iteration */
        return NULL;

    /* update start / stop for next iteration */
    Py_BEGIN_CRITICAL_SECTION(it);
    if (right)
        it->stop = pos + width - 1;
    else
        it->start = pos + 1;
    Py_END_CRITICAL_SECTION();

    return PyLong_FromSsize_t(pos);
}

static void
searchiter_dealloc(searchiterobject *it)
{
    PyObject_GC_UnTrack(it);
    Py_DECREF(it->self);
    Py_XDECREF(it->sub);
    PyObject_GC_Del(it);
}

static int
searchiter_traverse(searchiterobject *it, visitproc visit, void *arg)
{
    Py_VISIT(it->self);
    Py_VISIT(it->sub);
    return 0;
}

static PyTypeObject SearchIter_Type = {
    PyVarObject_HEAD_INIT(NULL, 0)
    "pauliebits.searchiterator",                /* tp_name */
    sizeof(searchiterobject),                 /* tp_basicsize */
    0,                                        /* tp_itemsize */
    /* methods */
    (destructor) searchiter_dealloc,          /* tp_dealloc */
    0,                                        /* tp_print */
    0,                                        /* tp_getattr */
    0,                                        /* tp_setattr */
    0,                                        /* tp_compare */
    0,                                        /* tp_repr */
    0,                                        /* tp_as_number */
    0,                                        /* tp_as_sequence */
    0,                                        /* tp_as_mapping */
    0,                                        /* tp_hash */
    0,                                        /* tp_call */
    0,                                        /* tp_str */
    PyObject_GenericGetAttr,                  /* tp_getattro */
    0,                                        /* tp_setattro */
    0,                                        /* tp_as_buffer */
    Py_TPFLAGS_DEFAULT | Py_TPFLAGS_HAVE_GC,  /* tp_flags */
    0,                                        /* tp_doc */
    (traverseproc) searchiter_traverse,       /* tp_traverse */
    0,                                        /* tp_clear */
    0,                                        /* tp_richcompare */
    0,                                        /* tp_weaklistoffset */
    PyObject_SelfIter,                        /* tp_iter */
    (iternextfunc) searchiter_next,           /* tp_iternext */
    0,                                        /* tp_methods */
};

/*********************** pauliebits method definitions **********************/

static PyMethodDef pauliebits_methods[] = {
    {"all",          (PyCFunction) pauliebits_all,         METH_NOARGS,
     all_doc},
    {"any",          (PyCFunction) pauliebits_any,         METH_NOARGS,
     any_doc},
    {"append",       (PyCFunction) pauliebits_append,      METH_O,
     append_doc},
    {"buffer_info",  (PyCFunction) pauliebits_buffer_info, METH_NOARGS,
     buffer_info_doc},
    {"bytereverse",  (PyCFunction) pauliebits_bytereverse, METH_VARARGS,
     bytereverse_doc},
    {"clear",        (PyCFunction) pauliebits_clear,       METH_NOARGS,
     clear_doc},
    {"copy",         (PyCFunction) pauliebits_copy,        METH_NOARGS,
     copy_doc},
    {"count",        (PyCFunction) pauliebits_count,       METH_VARARGS,
     count_doc},
    {"decode",       (PyCFunction) pauliebits_decode,      METH_O,
     decode_doc},
    {"encode",       (PyCFunction) pauliebits_encode,      METH_VARARGS,
     encode_doc},
    
    {"encode_ixyz", (PyCFunction)pauliebits_encode_ixyz, METH_VARARGS,
     encode_ixyz_doc},

    {"commutes_with", (PyCFunction)pauliebits_commutes_with, METH_VARARGS,
     commutes_with_doc},

    {"count_non_trivially", (PyCFunction)pauliebits_count_non_trivially, METH_VARARGS,
     count_non_trivially_doc},

    {"diagonal_index", (PyCFunction)pauliebits_diagonal_index, METH_VARARGS,
     diagonal_index_doc},

    {"phase", (PyCFunction)pauliebits_phase, METH_O,
     phase_doc},

    {"complex_conjugate", (PyCFunction)pauliebits_complex_conjugate, METH_VARARGS,
     complex_conjugate_doc},

    {"not_identity_mask", (PyCFunction)pauliebits_not_identity_mask, METH_VARARGS,
     not_identity_mask_doc},

    {"decode_ixyz", (PyCFunction)pauliebits_decode_ixyz, METH_VARARGS,
     decode_ixyz_doc},

    {"extend",       (PyCFunction) pauliebits_extend,      METH_O,
     extend_doc},
    {"fill",         (PyCFunction) pauliebits_fill,        METH_NOARGS,
     fill_doc},
    {"find",         (PyCFunction) pauliebits_find,        METH_VARARGS |
                                                         METH_KEYWORDS,
     find_doc},
    {"frombytes",    (PyCFunction) pauliebits_frombytes,   METH_O,
     frombytes_doc},
    {"fromfile",     (PyCFunction) pauliebits_fromfile,    METH_VARARGS,
     fromfile_doc},
    {"index",        (PyCFunction) pauliebits_index,       METH_VARARGS |
                                                         METH_KEYWORDS,
     index_doc},
    {"insert",       (PyCFunction) pauliebits_insert,      METH_VARARGS,
     insert_doc},
    {"invert",       (PyCFunction) pauliebits_invert,      METH_VARARGS,
     invert_doc},
    {"pack",         (PyCFunction) pauliebits_pack,        METH_O,
     pack_doc},
    {"pop",          (PyCFunction) pauliebits_pop,         METH_VARARGS,
     pop_doc},
    {"remove",       (PyCFunction) pauliebits_remove,      METH_O,
     remove_doc},
    {"reverse",      (PyCFunction) pauliebits_reverse,     METH_NOARGS,
     reverse_doc},
    {"rotate",       (PyCFunction) pauliebits_rotate,      METH_VARARGS,
     rotate_doc},
    {"search",       (PyCFunction) pauliebits_search,      METH_VARARGS |
                                                         METH_KEYWORDS,
     search_doc},
    {"setall",       (PyCFunction) pauliebits_setall,      METH_O,
     setall_doc},
    {"sort",         (PyCFunction) pauliebits_sort,        METH_VARARGS |
                                                         METH_KEYWORDS,
     sort_doc},
    {"to01",         (PyCFunction) pauliebits_to01,        METH_VARARGS |
                                                         METH_KEYWORDS,
     to01_doc},
    {"tobytes",      (PyCFunction) pauliebits_tobytes,     METH_NOARGS,
     tobytes_doc},
    {"__bytes__",    (PyCFunction) pauliebits_tobytes,     METH_NOARGS,
     tobytes_doc},
    {"tofile",       (PyCFunction) pauliebits_tofile,      METH_O,
     tofile_doc},
    {"tolist",       (PyCFunction) pauliebits_tolist,      METH_NOARGS,
     tolist_doc},
    {"unpack",       (PyCFunction) pauliebits_unpack,      METH_VARARGS |
                                                         METH_KEYWORDS,
     unpack_doc},

    {"__copy__",     (PyCFunction) pauliebits_copy,        METH_NOARGS,
     copy_doc},
    {"__deepcopy__", (PyCFunction) pauliebits_copy,        METH_O,
     copy_doc},
    {"__reduce__",   (PyCFunction) pauliebits_reduce,      METH_NOARGS,
     reduce_doc},
    {"__sizeof__",   (PyCFunction) pauliebits_sizeof,      METH_NOARGS,
     sizeof_doc},
    {"_freeze",      (PyCFunction) pauliebits_freeze,      METH_NOARGS,  0},

#ifndef NDEBUG
    /* functionality exposed in debug mode for testing */
    {"_shift_r8",    (PyCFunction) pauliebits_shift_r8,    METH_VARARGS, 0},
    {"_copy_n",      (PyCFunction) pauliebits_copy_n,      METH_VARARGS, 0},
    {"_overlap",     (PyCFunction) pauliebits_overlap,     METH_O,       0},
#endif

    {NULL,           NULL}  /* sentinel */
};

/* ------------------------ pauliebits initialization -------------------- */

/* Given string 'str', return an integer representing the bit-endianness.
   If the string is invalid, set exception and return -1. */
static int
endian_from_string(const char *str)
{
    if (str == NULL)
        return ENDIAN_DEFAULT;

    if (strcmp(str, "little") == 0)
        return ENDIAN_LITTLE;

    if (strcmp(str, "big") == 0)
        return ENDIAN_BIG;

    PyErr_Format(PyExc_ValueError, "bit-endianness must be either "
                 "'little' or 'big', not '%s'", str);
    return -1;
}

/* create a new pauliebits object whose buffer is imported from another object
   which exposes the buffer protocol */
static PyObject *
newpauliebits_from_buffer(PyTypeObject *type, PyObject *buffer, int endian)
{
    Py_buffer view;
    pauliebitsobject *obj;

    if (PyObject_GetBuffer(buffer, &view, PyBUF_SIMPLE) < 0)
        return NULL;

    obj = (pauliebitsobject *) type->tp_alloc(type, 0);
    if (obj == NULL) {
        PyBuffer_Release(&view);
        return NULL;
    }

    Py_SET_SIZE(obj, view.len);
    obj->ob_item = (char *) view.buf;
    obj->allocated = 0;       /* no buffer allocated (in this object) */
    obj->nbits = 8 * view.len;
    obj->endian = endian;
    obj->ob_exports = 0;
    obj->weakreflist = NULL;
    obj->readonly = view.readonly;

    obj->buffer = PyMem_New(Py_buffer, 1);
    if (obj->buffer == NULL) {
        PyObject_Del(obj);
        PyBuffer_Release(&view);
        return PyErr_NoMemory();
    }
    memcpy(obj->buffer, &view, sizeof(Py_buffer));

    return (PyObject *) obj;
}

/* return new pauliebits of length 'index', 'endian', and
   'init_zero' (initialize buffer with zeros) */
static PyObject *
newpauliebits_from_index(PyTypeObject *type, PyObject *index,
                       int endian, int init_zero)
{
    pauliebitsobject *res;
    Py_ssize_t nbits;

    assert(PyIndex_Check(index));
    nbits = PyNumber_AsSsize_t(index, PyExc_OverflowError);
    if (nbits == -1 && PyErr_Occurred())
        return NULL;

    if (nbits < 0) {
        PyErr_SetString(PyExc_ValueError, "pauliebits length must be >= 0");
        return NULL;
    }

    res = newpauliebitsobject(type, nbits, endian);
    if (res == NULL)
        return NULL;

    if (init_zero && nbits)
        memset(res->ob_item, 0x00, (size_t) Py_SIZE(res));

    return (PyObject *) res;
}

/* return new pauliebits from bytes-like object */
static PyObject *
newpauliebits_from_bytes(PyTypeObject *type, PyObject *buffer, int endian)
{
    pauliebitsobject *res;
    Py_buffer view;

    if (PyObject_GetBuffer(buffer, &view, PyBUF_SIMPLE) < 0)
        return NULL;

    res = newpauliebitsobject(type, 8 * view.len, endian);
    if (res == NULL) {
        PyBuffer_Release(&view);
        return NULL;
    }
    assert(Py_SIZE(res) == view.len);
    if (view.len) {
        Py_BEGIN_CRITICAL_SECTION(buffer);
        memcpy(res->ob_item, (char *) view.buf, (size_t) view.len);
        Py_END_CRITICAL_SECTION();
    }
    PyBuffer_Release(&view);
    return (PyObject *) res;
}

/* As of pauliebits version 2.9.0, "pauliebits(nbits)" will initialize all items
   to 0 (previously, the buffer was uninitialized).
   However, for speed, one might want to create an uninitialized pauliebits.
   In 2.9.1, we added the ability to create uninitialized pauliebits again,
   using "pauliebits(nbits, endian, Ellipsis)".
*/
static PyObject *
pauliebits_new(PyTypeObject *type, PyObject *args, PyObject *kwds)
{
    static char *kwlist[] = {"", "endian", "buffer", NULL};
    PyObject *initializer = Py_None, *buffer = Py_None;
    pauliebitsobject *res;
    char *endian_str = NULL;
    int endian, ret;

    if (!PyArg_ParseTupleAndKeywords(args, kwds, "|OzO:pauliebits", kwlist,
                                     &initializer, &endian_str, &buffer))
        return NULL;

    if ((endian = endian_from_string(endian_str)) < 0)
        return NULL;

    /* import buffer */
    if (buffer != Py_None && buffer != Py_Ellipsis) {
        if (initializer != Py_None) {
            PyErr_SetString(PyExc_TypeError,
                            "buffer requires no initializer argument");
            return NULL;
        }
        return newpauliebits_from_buffer(type, buffer, endian);
    }

    /* no arg / None */
    if (initializer == Py_None)
        return (PyObject *) newpauliebitsobject(type, 0, endian);

    /* bool */
    if (PyBool_Check(initializer)) {
        PyErr_SetString(PyExc_TypeError,
                        "cannot create pauliebits from 'bool' object");
        return NULL;
    }

    /* index (a number) */
    if (PyIndex_Check(initializer))
        return newpauliebits_from_index(type, initializer, endian,
                                      buffer == Py_None);

    /* bytes or bytearray */
    if (PyBytes_Check(initializer) || PyByteArray_Check(initializer))
        return newpauliebits_from_bytes(type, initializer, endian);

    /* pauliebits: use its bit-endianness when endian argument is None */
    if (pauliebits_Check(initializer) && endian_str == NULL)
        endian = ((pauliebitsobject *) initializer)->endian;

    /* empty pauliebits to be extended below */
    if ((res = newpauliebitsobject(type, 0, endian)) == NULL)
        return NULL;

    if (pauliebits_Check(initializer)) {
        Py_BEGIN_CRITICAL_SECTION(initializer);
        ret = extend_pauliebits(res, (pauliebitsobject *) initializer);
        Py_END_CRITICAL_SECTION();
    }
    else {  /* leave remaining type dispatch to extend method */
        ret = extend_dispatch(res, initializer);
    }

    if (ret < 0) {
        Py_DECREF(res);
        return NULL;
    }
    return (PyObject *) res;
}


static PyObject *
richcompare_lock_held(pauliebitsobject *va, pauliebitsobject *wa, int op)
{
    Py_ssize_t vs = va->nbits, ws = wa->nbits, i, c;
    char *vb = va->ob_item, *wb = wa->ob_item;

    if (op == Py_EQ || op == Py_NE) {
        /* shortcuts for EQ/NE */
        if (vs != ws) {
            /* if sizes differ, the pauliebits differ */
            return PyBool_FromLong(op == Py_NE);
        }
        else if (va->endian == wa->endian) {
            /* sizes and endianness are the same - use memcmp() */
            int cmp = (vs >= 8) ? memcmp(vb, wb, (size_t) vs / 8) : 0;

            if (cmp == 0 && vs % 8)  /* if equal, compare remaining bits */
                cmp = zlc(va) != zlc(wa);

            return PyBool_FromLong((cmp == 0) ^ (op == Py_NE));
        }
    }

    /* search for the first index where items are different */
    c = Py_MIN(vs, ws) / 8;  /* common buffer size */
    i = 0;                   /* byte index */
    if (va->endian == wa->endian) {
        /* equal endianness - skip ahead by comparing bytes directly */
        while (i < c && vb[i] == wb[i])
            i++;
    }
    else {
        /* opposite endianness - compare with reversed byte */
        while (i < c && vb[i] == reverse_trans[(unsigned char) wb[i]])
            i++;
    }
    i *= 8;  /* i is now the bit index up to which we compared bytes */

    for (; i < vs && i < ws; i++) {
        int vi = getbit(va, i);
        int wi = getbit(wa, i);

        if (vi != wi)
            /* we have an item that differs */
            Py_RETURN_RICHCOMPARE(vi, wi, op);
    }

    /* no more items to compare -- compare sizes */
    Py_RETURN_RICHCOMPARE(vs, ws, op);
}

static PyObject *
richcompare(PyObject *v, PyObject *w, int op)
{
    PyObject *res;

    if (!pauliebits_Check(v) || !pauliebits_Check(w))
        return Py_NewRef(Py_NotImplemented);

    Py_BEGIN_CRITICAL_SECTION2(v, w);
    res = richcompare_lock_held((pauliebitsobject *) v,
                                (pauliebitsobject *) w, op);
    Py_END_CRITICAL_SECTION2();

    return res;
}

/***************************** pauliebits iterator **************************/

typedef struct {
    PyObject_HEAD
    pauliebitsobject *self;            /* pauliebits we're iterating over */
    Py_ssize_t index;                /* current index in pauliebits */
} pauliebitsiterobject;

static PyTypeObject PauliebitsIter_Type;

/* create a new initialized pauliebits iterator object, this object is
   returned when calling iter(a) */
static PyObject *
pauliebits_iter(pauliebitsobject *self)
{
    pauliebitsiterobject *it;

    it = PyObject_GC_New(pauliebitsiterobject, &PauliebitsIter_Type);
    if (it == NULL)
        return NULL;

    Py_INCREF(self);
    it->self = self;
    it->index = 0;
    PyObject_GC_Track(it);
    return (PyObject *) it;
}

static PyObject *
pauliebitsiter_next(pauliebitsiterobject *it)
{
    int vi;

    Py_BEGIN_CRITICAL_SECTION2(it, it->self);
    if (it->index < it->self->nbits) {
        vi = getbit(it->self, it->index++);
    }
    else {
        vi = -1;  /* stop iteration */
    }
    Py_END_CRITICAL_SECTION2();

    return (vi < 0) ? NULL : PyLong_FromLong(vi);
}

static void
pauliebitsiter_dealloc(pauliebitsiterobject *it)
{
    PyObject_GC_UnTrack(it);
    Py_DECREF(it->self);
    PyObject_GC_Del(it);
}

static int
pauliebitsiter_traverse(pauliebitsiterobject *it, visitproc visit, void *arg)
{
    Py_VISIT(it->self);
    return 0;
}

static PyTypeObject PauliebitsIter_Type = {
    PyVarObject_HEAD_INIT(NULL, 0)
    "pauliebits.pauliebitsiterator",              /* tp_name */
    sizeof(pauliebitsiterobject),               /* tp_basicsize */
    0,                                        /* tp_itemsize */
    /* methods */
    (destructor) pauliebitsiter_dealloc,        /* tp_dealloc */
    0,                                        /* tp_print */
    0,                                        /* tp_getattr */
    0,                                        /* tp_setattr */
    0,                                        /* tp_compare */
    0,                                        /* tp_repr */
    0,                                        /* tp_as_number */
    0,                                        /* tp_as_sequence */
    0,                                        /* tp_as_mapping */
    0,                                        /* tp_hash */
    0,                                        /* tp_call */
    0,                                        /* tp_str */
    PyObject_GenericGetAttr,                  /* tp_getattro */
    0,                                        /* tp_setattro */
    0,                                        /* tp_as_buffer */
    Py_TPFLAGS_DEFAULT | Py_TPFLAGS_HAVE_GC,  /* tp_flags */
    0,                                        /* tp_doc */
    (traverseproc) pauliebitsiter_traverse,     /* tp_traverse */
    0,                                        /* tp_clear */
    0,                                        /* tp_richcompare */
    0,                                        /* tp_weaklistoffset */
    PyObject_SelfIter,                        /* tp_iter */
    (iternextfunc) pauliebitsiter_next,         /* tp_iternext */
    0,                                        /* tp_methods */
};

/******************** pauliebits buffer export interface ********************/
/*
   Here we create pauliebits_as_buffer for exporting pauliebits buffers.
   Buffer imports are handled in newpauliebits_from_buffer().
*/

static int
pauliebits_getbuffer(pauliebitsobject *self, Py_buffer *view, int flags)
{
    int ret;

    if (view == NULL) {
        Py_BEGIN_CRITICAL_SECTION(self);
        self->ob_exports++;
        Py_END_CRITICAL_SECTION();
        return 0;
    }

    Py_BEGIN_CRITICAL_SECTION(self);
    ret = PyBuffer_FillInfo(view,
                            (PyObject *) self,  /* exporter */
                            (void *) self->ob_item,
                            Py_SIZE(self),
                            self->readonly,
                            flags);
    if (ret >= 0)
        self->ob_exports++;

    Py_END_CRITICAL_SECTION();

    return ret;
}

static void
pauliebits_releasebuffer(pauliebitsobject *self, Py_buffer *view)
{
    Py_BEGIN_CRITICAL_SECTION(self);
    self->ob_exports--;
    Py_END_CRITICAL_SECTION();
}

static PyBufferProcs pauliebits_as_buffer = {
    (getbufferproc) pauliebits_getbuffer,
    (releasebufferproc) pauliebits_releasebuffer,
};

/***************************** Pauliebits Type ******************************/

PyDoc_STRVAR(pauliebitstype_doc,
"pauliebits(initializer=0, /, endian='big', buffer=None) -> pauliebits\n\
\n\
Return a new pauliebits object whose items are bits initialized from\n\
the optional initializer, and bit-endianness.\n\
The initializer may be one of the following types:\n\
a.) `int` pauliebits, initialized to zeros, of given length\n\
b.) `bytes` or `bytearray` to initialize buffer directly\n\
c.) `str` of 0s and 1s, ignoring whitespace and \"_\"\n\
d.) iterable of integers 0 or 1.\n\
\n\
Optional keyword arguments:\n\
\n\
`endian`: Specifies the bit-endianness of the created pauliebits object.\n\
Allowed values are `big` and `little` (the default is `big`).\n\
The bit-endianness affects the buffer representation of the pauliebits.\n\
\n\
`buffer`: Any object which exposes a buffer.  When provided, `initializer`\n\
cannot be present (or has to be `None`).  The imported buffer may be\n\
read-only or writable, depending on the object type.");


static PyTypeObject Pauliebits_Type = {
    PyVarObject_HEAD_INIT(NULL, 0)
    "pauliebits.pauliebits",                      /* tp_name */
    sizeof(pauliebitsobject),                   /* tp_basicsize */
    0,                                        /* tp_itemsize */
    /* methods */
    (destructor) pauliebits_dealloc,            /* tp_dealloc */
    0,                                        /* tp_print */
    0,                                        /* tp_getattr */
    0,                                        /* tp_setattr */
    0,                                        /* tp_compare */
    (reprfunc) pauliebits_repr,                 /* tp_repr */
    &pauliebits_as_number,                      /* tp_as_number */
    &pauliebits_as_sequence,                    /* tp_as_sequence */
    &pauliebits_as_mapping,                     /* tp_as_mapping */
    PyObject_HashNotImplemented,              /* tp_hash */
    0,                                        /* tp_call */
    0,                                        /* tp_str */
    PyObject_GenericGetAttr,                  /* tp_getattro */
    0,                                        /* tp_setattro */
    &pauliebits_as_buffer,                      /* tp_as_buffer */
    Py_TPFLAGS_DEFAULT | Py_TPFLAGS_BASETYPE, /* tp_flags */
    pauliebitstype_doc,                         /* tp_doc */
    0,                                        /* tp_traverse */
    0,                                        /* tp_clear */
    richcompare,                              /* tp_richcompare */
    offsetof(pauliebitsobject, weakreflist),    /* tp_weaklistoffset */
    (getiterfunc) pauliebits_iter,              /* tp_iter */
    0,                                        /* tp_iternext */
    pauliebits_methods,                         /* tp_methods */
    0,                                        /* tp_members */
    pauliebits_getsets,                         /* tp_getset */
    0,                                        /* tp_base */
    0,                                        /* tp_dict */
    0,                                        /* tp_descr_get */
    0,                                        /* tp_descr_set */
    0,                                        /* tp_dictoffset */
    0,                                        /* tp_init */
    PyType_GenericAlloc,                      /* tp_alloc */
    pauliebits_new,                             /* tp_new */
    PyObject_Del,                             /* tp_free */
};

/***************************** Module functions ***************************/

static PyObject *
bits2bytes(PyObject *module, PyObject *n)
{
    PyObject *zero, *seven, *eight, *a, *b;
    int cmp_res;

    if (!PyLong_Check(n))
        return PyErr_Format(PyExc_TypeError, "'int' object expected, "
                            "got '%s'", Py_TYPE(n)->tp_name);

    if ((zero = Py_GetConstant(Py_CONSTANT_ZERO)) == NULL)
        return NULL;
    cmp_res = PyObject_RichCompareBool(n, zero, Py_LT);
    Py_DECREF(zero);

    if (cmp_res < 0)
        return NULL;
    if (cmp_res) {
        PyErr_SetString(PyExc_ValueError, "non-negative int expected");
        return NULL;
    }

    if ((seven = PyLong_FromLong(7)) == NULL)
        return NULL;
    a = PyNumber_Add(n, seven);          /* a = n + 7 */
    Py_DECREF(seven);
    if (a == NULL)
        return NULL;

    if ((eight = PyLong_FromLong(8)) == NULL) {
        Py_DECREF(a);
        return NULL;
    }
    b = PyNumber_FloorDivide(a, eight);  /* b = a // 8 */
    Py_DECREF(eight);
    Py_DECREF(a);

    return b;
}

PyDoc_STRVAR(bits2bytes_doc,
"bits2bytes(n, /) -> int\n\
\n\
Return the number of bytes necessary to store n bits.");


static PyObject *
reconstructor(PyObject *module, PyObject *args)
{
    PyTypeObject *type;
    Py_ssize_t nbytes;
    PyObject *bytes;
    pauliebitsobject *res;
    char *endian_str;
    int endian, padbits, readonly;

    if (!PyArg_ParseTuple(args, "OOsii:_pauliebits_reconstructor",
                          &type, &bytes, &endian_str, &padbits, &readonly))
        return NULL;

    if (!PyType_Check(type))
        return PyErr_Format(PyExc_TypeError, "first argument must be a type "
                            "object, got '%s'", Py_TYPE(type)->tp_name);

    if (!PyType_IsSubtype(type, &Pauliebits_Type))
        return PyErr_Format(PyExc_TypeError, "'%s' is not a subtype of "
                            "pauliebits", type->tp_name);

    if (!PyBytes_Check(bytes))
        return PyErr_Format(PyExc_TypeError, "second argument must be bytes, "
                            "got '%s'", Py_TYPE(bytes)->tp_name);

    if ((endian = endian_from_string(endian_str)) < 0)
        return NULL;

    nbytes = PyBytes_GET_SIZE(bytes);
    if (padbits < 0 || padbits > 7 || (nbytes == 0 && padbits))
        return PyErr_Format(PyExc_ValueError,
                            "invalid number of pad bits: %d", padbits);

    res = newpauliebitsobject(type, 8 * nbytes - padbits, endian);
    if (res == NULL)
        return NULL;
    assert(Py_SIZE(res) == nbytes);
    if (nbytes)
        memcpy(res->ob_item, PyBytes_AS_STRING(bytes), (size_t) nbytes);
    if (readonly) {
        set_padbits(res);
        res->readonly = 1;
    }
    return (PyObject *) res;
}


static PyObject *
get_default_endian(PyObject *module)
{
    return PyUnicode_FromString(ENDIAN_STR(ENDIAN_DEFAULT));
}

PyDoc_STRVAR(get_default_endian_doc,
"get_default_endian() -> str\n\
\n\
Return the default bit-endianness for new pauliebits objects being created.");


static PyObject *
sysinfo(PyObject *module, PyObject *args)
{
    char *key;

    if (!PyArg_ParseTuple(args, "s:_sysinfo", &key))
        return NULL;

#define R(k, v)                             \
    if (strcmp(key, k) == 0)                \
        return PyLong_FromLong((long) (v))

    R("void*", sizeof(void *));
    R("size_t", sizeof(size_t));
    R("pauliebitsobject", sizeof(pauliebitsobject));
    R("decodetreeobject", sizeof(decodetreeobject));
    R("binode", sizeof(binode));
    R("PY_LITTLE_ENDIAN", PY_LITTLE_ENDIAN);
    R("PY_BIG_ENDIAN", PY_BIG_ENDIAN);
    R("HAVE_BUILTIN_BSWAP64", HAVE_BUILTIN_BSWAP64);
#ifdef Py_GIL_DISABLED   /* Python configured using --disable-gil */
    R("Py_GIL_DISABLED", 1);
#else
    R("Py_GIL_DISABLED", 0);
#endif
#ifdef Py_DEBUG          /* Python configured using --with-pydebug  */
    R("Py_DEBUG", 1);
#else
    R("Py_DEBUG", 0);
#endif
#ifndef NDEBUG           /* pauliebits compiled without -DNDEBUG */
    R("DEBUG", 1);
#else
    R("DEBUG", 0);
#endif

    PyErr_SetString(PyExc_KeyError, key);
    return NULL;
#undef R
}

PyDoc_STRVAR(sysinfo_doc,
"_sysinfo(key) -> int\n\
\n\
Return system- and compile-specific information given a key.");


static PyMethodDef module_functions[] = {
    {"bits2bytes",          (PyCFunction) bits2bytes,         METH_O,
     bits2bytes_doc},
    {"_pauliebits_reconstructor",
                            (PyCFunction) reconstructor,      METH_VARARGS,
     reduce_doc},
    {"get_default_endian",  (PyCFunction) get_default_endian, METH_NOARGS,
     get_default_endian_doc},
    {"_sysinfo",            (PyCFunction) sysinfo,            METH_VARARGS,
     sysinfo_doc},
    {NULL,                  NULL}  /* sentinel */
};

/******************************* Install Module ***************************/

/* register pauliebits as collections.abc.MutableSequence */
static int
register_abc(void)
{
    PyObject *abc_module, *mutablesequence, *res;

    abc_module = PyImport_ImportModule("collections.abc");
    if (abc_module == NULL)
        return -1;

    mutablesequence = PyObject_GetAttrString(abc_module, "MutableSequence");
    Py_DECREF(abc_module);
    if (mutablesequence == NULL)
        return -1;

    res = PyObject_CallMethod(mutablesequence, "register", "O",
                              (PyObject *) &Pauliebits_Type);
    Py_DECREF(mutablesequence);
    if (res == NULL)
        return -1;

    Py_DECREF(res);
    return 0;
}

static PyModuleDef moduledef = {
    PyModuleDef_HEAD_INIT, "_pauliebits", 0, -1, module_functions,
};

PyMODINIT_FUNC
PyInit__pauliebits(void)
{
    PyObject *m;

    /* setup translation table, which maps each byte to its reversed:
       reverse_trans = {0x00, 0x80, 0x40, 0xc0, 0x20, 0xa0, ..., 0xff} */
    setup_table(reverse_trans, 'r');

    if ((m = PyModule_Create(&moduledef)) == NULL)
        return NULL;

#ifdef Py_GIL_DISABLED
    PyUnstable_Module_SetGIL(m, Py_MOD_GIL_NOT_USED);
#endif

    if (PyType_Ready(&Pauliebits_Type) < 0)
        return NULL;
    Py_SET_TYPE(&Pauliebits_Type, &PyType_Type);
    Py_INCREF((PyObject *) &Pauliebits_Type);
    PyModule_AddObject(m, "pauliebits", (PyObject *) &Pauliebits_Type);

    if (register_abc() < 0)
        return NULL;

    if (PyType_Ready(&DecodeTree_Type) < 0)
        return NULL;
    Py_SET_TYPE(&DecodeTree_Type, &PyType_Type);
    Py_INCREF((PyObject *) &DecodeTree_Type);
    PyModule_AddObject(m, "decodetree", (PyObject *) &DecodeTree_Type);

    if (PyType_Ready(&DecodeIter_Type) < 0)
        return NULL;
    Py_SET_TYPE(&DecodeIter_Type, &PyType_Type);
    Py_INCREF((PyObject *) &DecodeIter_Type);
    PyModule_AddObject(m, "decodeiterator", (PyObject *) &DecodeIter_Type);

    if (PyType_Ready(&PauliebitsIter_Type) < 0)
        return NULL;
    Py_SET_TYPE(&PauliebitsIter_Type, &PyType_Type);

    if (PyType_Ready(&SearchIter_Type) < 0)
        return NULL;
    Py_SET_TYPE(&SearchIter_Type, &PyType_Type);

    if (PyModule_AddStringMacro(m, PAULIEBITS_VERSION) < 0)
        return NULL;

    return m;
}
