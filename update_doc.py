import re
import sys
from doctest import testfile
from glob import glob
from io import StringIO

import pauliebits.util


BASE_URL = "https://github.com/ilanschnell/pauliebits"

NEW_IN = {
    'pauliebits':              ['2.3: optional `buffer` argument',
                              '3.4: allow initializer `bytes` or `bytearray` '
                                   'to set buffer directly'],
    'pauliebits.buffer_info':   '3.7: return named tuple',
    'pauliebits.bytereverse':  ['2.2.5: optional start and stop arguments',
                              '3.9.1: clip arguments instead of raising '
                                     '`IndexError`'],
    'pauliebits.clear':         '1.4',
    'pauliebits.count':        ['1.1.0: optional start and stop arguments',
                              '2.3.7: optional step argument',
                              '2.9: add non-overlapping sub-pauliebits count'],
    'pauliebits.decode':       ['3.0: returns iterator (equivalent to past '
                                   '`.iterdecode()`)',
                              '3.9: returns public `decodeiterator` object'],
    'pauliebits.endian':        '3.4: replaces former `.endian()` method',
    'pauliebits.extend':        '3.4: allow `bytes` object',
    'pauliebits.find':         ['2.1',
                              '2.9: add optional keyword argument `right`'],
    'pauliebits.frombytes':     '2.5.0: allow bytes-like argument',
    'pauliebits.index':         '2.9: add optional keyword argument `right`',
    'pauliebits.invert':        '1.5.3: optional index argument',
    'pauliebits.pack':          '2.5.0: allow bytes-like argument',
    'pauliebits.search':       ['2.9: optional start and stop arguments - '
                                   'add optional keyword argument `right`',
                              '3.0: returns iterator (equivalent to past '
                                   '`.itersearch()`)'],
    'pauliebits.to01':          '3.3: optional `group` and `sep` arguments',
    'pauliebits.rotate':        '3.9',
    'decodeiterator.index':   '3.9',
    'decodeiterator.skipbits':'3.9',
    'decodetree':             '1.6',
    'frozenpauliebits':         '1.1',
    'get_default_endian':     '1.3',
    'util.any_and':           '2.7',
    'util.byteswap':          '3.4',
    'util.ba2base':          ['1.9',
                              '3.3: optional `group` and `sep` arguments'],
    'util.base2ba':          ['1.9',
                              '3.3: ignore whitespace'],
    'util.ba2hex':            '3.3: optional `group` and `sep` arguments',
    'util.hex2ba':            '3.3: ignore whitespace',
    'util.correspond_all':    '3.4',
    'util.count_n':           '2.3.6: optional value argument',
    'util.deserialize':      ['1.8',
                              '2.5.0: allow bytes-like argument'],
    'util.intervals':         '2.7',
    'util.ones':              '2.9',
    'util.parity':            '1.9',
    'util.sum_indices':      ['3.6',
                              '3.7: add optional mode argument'],
    'util.xor_indices':       '3.2',
    'util.pprint':            '1.8',
    'util.serialize':         '1.8',
    'util.urandom':           '1.7',
    'util.random_k':          '3.6',
    'util.random_p':          '3.5',
    'util.gen_primes':        '3.7',
    'util.sc_encode':         '2.7',
    'util.sc_decode':         '2.7',
    'util.vl_decode':         '2.2',
    'util.vl_encode':         '2.2',
    'util.canonical_huffman': '2.5',
    'util.canonical_decode':  '2.5',
}

DOCS = {
    'ba3': ('pauliebits 3 transition', 'pauliebits3.rst'),
    'chc': ('Canonical Huffman Coding', 'canonical.rst'),
    'rep': ('pauliebits representations', 'represent.rst'),
    'rnd': ('Random pauliebitss', 'random_p.rst'),
    'sc':  ('Compression of sparse pauliebitss', 'sparse_compression.rst'),
    'vlf': ('Variable length pauliebits format', 'variable_length.rst'),
}

DOC_LINKS = {
    'pauliebits.decode':         'ba3',
    'pauliebits.search':         'ba3',
    'util.canonical_huffman':  'chc',
    'util.canonical_decode':   'chc',
    'util.ba2base':            'rep',
    'util.base2ba':            'rep',
    'util.deserialize':        'rep',
    'util.random_p':           'rnd',
    'util.serialize':          'rep',
    'util.sc_decode':          'sc',
    'util.sc_encode':          'sc',
    'util.vl_decode':          'vlf',
    'util.vl_encode':          'vlf',
}

NOTES = {
    'pauliebits.pack': """\
   This method, as well as the ``.unpack()`` method, are meant for efficient
   transfer of data between pauliebits objects to other Python objects (for
   example NumPy's ndarray object) which have a different memory view.""",

    'pauliebits.rotate': """\
   When pauliebits ``a`` is not empty, rotating one step to the right is
   equivalent to ``a.insert(0, a.pop())``, and rotating one step to the left
   is equivalent to ``a.append(a.pop(0))``.
   The same convention is used by the ``.rotate()`` method of
   the ``collections.deque`` object.""",

    'pauliebits.search': """\
   For example, ``a.search(1)`` is the easiest (and most efficient) way
   to create an iterator over all active indices in ``a``.""",

    'pauliebits.tolist': """\
   Note that the list object being created will require 32 or 64 times more
   memory (depending on the machine architecture) than the pauliebits object,
   which may cause a memory error if the pauliebits is very large.""",

    'util.byteswap': """\
   We should mention that Python's ``array.array`` object has a
   method ``.byteswap()`` with similar functionality.  However, unlike
   pauliebits's ``util.byteswap()`` function, this method is limited to
   swapping 2, 4, or 8 consecutive bytes.""",

    'util.gen_primes': """\
   Apart from working with prime numbers, this function is useful for
   testing, as it provides a simple way to create a well-defined pauliebits
   of any length.""",

    'util.count_xor': "   This is also known as the Hamming distance.",
}

GETSET = {
    'pauliebits.endian':      'str',
    'pauliebits.nbytes':      'int',
    'pauliebits.padbits':     'int',
    'pauliebits.readonly':    'bool',
    'decodeiterator.index': 'int',
}

_NAMES = set()

sig_pat = re.compile(r"""
(                # group 1
  (\w+)          # function name, group 2
  \([^()]*\)     # (...)
)
(                # optional group 3
  \s->\s(.+)     # return type, group 4
)?
""", re.VERBOSE)

def get_doc(name):
    parts = name.split('.')
    obj = pauliebits
    while parts:
        obj = getattr(obj, parts.pop(0))

    lines = obj.__doc__.splitlines()

    if name in GETSET:
        sig = '``%s`` -> %s' % (obj.__name__, GETSET[name])
        return sig, lines

    m = sig_pat.match(lines[0])
    if m is None:
        raise Exception("signature invalid: %r" % lines[0])
    sig = '``%s``' %  m.group(1)
    assert m.group(2) == obj.__name__, lines[0]
    if m.group(4):
        sig += ' -> %s' % m.group(4)
    assert lines[1] == ''
    return sig, lines[2:]


def write_doc(fo, name):
    _NAMES.add(name)
    sig, lines = get_doc(name)
    fo.write(sig + '\n')
    for line in lines:
        out = line.rstrip()
        fo.write("   %s\n" % out.replace('`', '``') if out else "\n")

    note = NOTES.get(name)
    if note:
        fo.write("\n%s\n" % note)

    link = DOC_LINKS.get(name)
    if link:
        title, filename = DOCS[link]
        url = BASE_URL + '/blob/master/doc/' + filename
        fo.write("\n   See also: `%s <%s>`__\n" % (title, url))

    new_in = NEW_IN.get(name)
    if new_in:
        for line in new_in if isinstance(new_in, list) else [new_in]:
            fo.write("\n   New in version %s\n" % line.replace('`', '``'))

    fo.write('\n\n')


def get_names(cl, getset=False):
    for name in sorted(dir(cl)):
        if name.startswith('_'):
            continue
        name = '%s.%s' % (cl.__name__, name)
        if getset == (name in GETSET):
            yield name

def write_reference_for_class(fo, cl):
    class_name = cl.__name__
    heading = "%s methods:" % class_name
    fo.write("%s\n%s\n\n" % (heading, '-' * len(heading)))
    for name in get_names(cl):
        write_doc(fo, name)

    getset_names = list(get_names(cl, True))
    if getset_names:
        heading = "%s data descriptors:" % class_name
        fo.write("%s\n%s\n\n" % (heading, '-' * len(heading)))
        if class_name == "pauliebits":
            fo.write("Data descriptors were added in version 2.6.\n\n")
        for name in getset_names:
            write_doc(fo, name)


def write_reference(fo):
    fo.write("""\
Reference
=========

pauliebits version: %s -- `change log <%s>`__

In the following, ``item`` and ``value`` are usually a single bit -
an integer 0 or 1.

Also, ``sub_pauliebits`` refers to either a pauliebits, or an ``item``.


The pauliebits object:
--------------------

""" % (pauliebits.__version__, BASE_URL + "/blob/master/doc/changelog.rst"))
    write_doc(fo, 'pauliebits')

    write_reference_for_class(fo, pauliebits.pauliebits)
    write_reference_for_class(fo, pauliebits.decodeiterator)

    fo.write("Other objects:\n"
             "--------------\n\n")
    write_doc(fo, 'frozenpauliebits')
    write_doc(fo, 'decodetree')

    fo.write("Functions defined in the `pauliebits` module:\n"
             "-------------------------------------------\n\n")
    for func in sorted(['test', 'bits2bytes', 'get_default_endian']):
        write_doc(fo, func)

    fo.write("Functions defined in `pauliebits.util` module:\n"
             "--------------------------------------------\n\n"
             "This sub-module was added in version 1.2.\n\n")
    for func in sorted(pauliebits.util.__all__):
        write_doc(fo, 'util.%s' % func)

    for name in list(NEW_IN) + list(DOC_LINKS):
        assert name in _NAMES, name


def update_readme(path):
    ver_pat = re.compile(r'(pauliebits.+?)\s(\d+\.\d+\.\d+)')

    with open(path, 'r') as fi:
        data = fi.read()

    with StringIO() as fo:
        for line in data.splitlines():
            if line == 'Reference':
                break
            line = ver_pat.sub(r'\1 ' + pauliebits.__version__, line)
            fo.write("%s\n" % line.rstrip())

        write_reference(fo)
        new_data = fo.getvalue()

    if new_data == data:
        print("already up-to-date")
    else:
        with open(path, 'w') as f:
            f.write(new_data)


def write_changelog(fo):
    ver_pat = re.compile(r'(\d{4}-\d{2}-\d{2})\s+(\d+\.\d+\.\d+)')
    hash_pat = re.compile(r'#([0-9a-f]+)')
    link_pat = re.compile(r'\[(.+)\]\((.+)\)')

    def hash_replace(match):
        group1 = match.group(1)
        if len(group1) >= 7:
            if len(group1) != 8:
                print("Warning: commit hash length != 8, got", len(group1))
            url = "%s/commit/%s" % (BASE_URL, group1)
        else:
            url = "%s/issues/%d" % (BASE_URL, int(group1))
        return "`%s <%s>`__" % (match.group(0), url)

    fo.write("Change log\n"
             "==========\n\n")

    for line in open('./CHANGE_LOG'):
        line = line.rstrip()
        match = ver_pat.match(line)
        if match:
            line = match.expand(r'**\2** (\1):')
        elif line.startswith('-----'):
            line = ''
        elif line.startswith('  '):
            line = line[2:]
            line = line.replace('`', '``')
            line = hash_pat.sub(hash_replace, line)
            line = link_pat.sub(r"`\1 <\2>`__", line)
        fo.write(line + '\n')


def main():
    if len(sys.argv) > 1:
        sys.exit("no arguments expected")

    update_readme('./README.rst')
    with open('./doc/reference.rst', 'w') as fo:
        write_reference(fo)
    with open('./doc/changelog.rst', 'w') as fo:
        write_changelog(fo)

    testfile('./README.rst')
    for path in glob("./doc/*.rst"):
        testfile(path)
    for path in glob("./examples/*.rst"):
        testfile(path)


if __name__ == '__main__':
    main()
