import json
from base64 import b64encode, b64decode

from pauliebits import pauliebits
from pauliebits.util import serialize, deserialize


class JSONEncoder(json.JSONEncoder):

    def default(self, obj):

        if isinstance(obj, pauliebits):
            if len(obj) > 50:
                return {'pauliebits_b64': b64encode(serialize(obj)).decode()}
            else:
                return {'pauliebits': obj.to01()}

        return json.JSONEncoder.default(self, obj)


class JSONDecoder(json.JSONDecoder):

    def __init__(self, *args, **kwargs):
        json.JSONDecoder.__init__(self, object_hook=self.object_hook,
                                  *args, **kwargs)

    def object_hook(self, obj):
        if isinstance(obj, dict) and len(obj) == 1:
            if 'pauliebits_b64' in obj:
                return deserialize(b64decode(obj['pauliebits_b64']))

            if 'pauliebits' in obj:
                return pauliebits(obj['pauliebits'])

        return obj


def test():
    from random import getrandbits
    from pauliebits.util import urandom

    a = [urandom(n * n, endian=['little', 'big'][getrandbits(1)])
         for n in range(12)]
    a.append({'key1': pauliebits('010'),
              'key2': 'value2',
              'key3': urandom(300)})
    j = JSONEncoder(indent=2).encode(a)
    print(j)

    b = JSONDecoder().decode(j)
    assert a == b
    assert b[-1]['key1'] == pauliebits('010')


if __name__ == '__main__':
    test()
