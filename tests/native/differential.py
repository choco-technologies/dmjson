"""Seeded round trips/acceptance checks against Python's JSON implementation."""
import json
import random
import subprocess
import sys

rng = random.Random(0xDABC0123)


def text():
    alphabet = 'abc"\\/~\0\b\f\t\r\né中😀\U0010ffff'
    return ''.join(rng.choice(alphabet) for _ in range(rng.randrange(20)))


def value(depth=0):
    kind = rng.randrange(5 if depth == 5 else 7)
    if kind == 0:
        return None
    if kind == 1:
        return bool(rng.randrange(2))
    if kind == 2:
        return rng.randrange(-10**30, 10**30)
    if kind == 3:
        return rng.uniform(-1e100, 1e100)
    if kind == 4:
        return text()
    if kind == 5:
        return [value(depth + 1) for _ in range(rng.randrange(6))]
    return {text(): value(depth + 1) for _ in range(rng.randrange(6))}


inputs = [json.dumps(value(), ensure_ascii=bool(i % 2), allow_nan=False)
          for i in range(2000)]
# Deliberately malformed/truncated documents, including some valid by chance.
base = '{"a":[true,false,null,{},[],"é\\u0000",-0.01e+9]}'
inputs += [base[:i] for i in range(len(base))]
alphabet = '[]{}:,"0129-.e+truefalsn \\'
inputs += [''.join(rng.choice(alphabet) for _ in range(rng.randrange(1, 90)))
           for _ in range(3000)]
inputs += ['1e999', '-0', '"é中😀"', '"\\ud83d\\ude00"',
           '{"a":1,"a":2}', '"\\u0000"', '"/\\\\\\\""']
process = subprocess.run([sys.argv[1]], input='\n'.join(inputs) + '\n',
                         text=True, encoding='utf-8', capture_output=True, check=True)
outputs = process.stdout.splitlines()
assert len(outputs) == len(inputs), process.stderr
for source, output in zip(inputs, outputs):
    try:
        expected = json.loads(source)
    except json.JSONDecodeError:
        assert output == 'ERROR', (source, output)
    else:
        assert output != 'ERROR', source
        actual = json.loads(output)
        assert actual == expected, (source, output, expected)
print(f'{len(inputs)} differential JSON cases passed')
