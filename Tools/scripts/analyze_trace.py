import decimal
import opcode
import os.path
import sys


def format_elapsed(elapsed):
    units = ['s', 'ms', 'µs', 'ns']
    while elapsed < 1:
        before = elapsed
        elapsed *= 1000
        assert before != elapsed
        units.pop(0)
    assert elapsed > 1
    whole = int(elapsed)
    dec = int((elapsed - whole) * 10)
    return f'{whole:>3,}.{dec} {units[0]}'


def format_elapsed(elapsed):
    micro = elapsed * 1_000_000
    whole = int(micro)
    dec = int((micro - whole) * 10)
    return f'{whole:>5,}.{dec} µs'


def _parse_trace(line):
    # 16540234.193887170 <init>
    # 16540234.193896170 <enter>
    # 16540234.193896770 <loop enter>
    # 16540234.193896970 <op 116>
    ts, _, event = line.partition(' ')

    # XXX datetime.datetime.utcfromtimestamp()?
    ts = decimal.Decimal(ts)

    event = event[1:-1]
    if event.startswith('op '):
        op = int(event[3:])
        opname = opcode.opname[op]
        event = f'op {opname:20} ({op})'

    return ts, event


def _process_lines(lines):
    if isinstance(lines, str):
        lines = lines.splitlines()
    for line in lines:
        line = line.strip()
        if not line:
            continue
        if line.startswith('#'):
            yield line
        else:
            yield _parse_trace(line)


##################################
# the script

def parse_args(argv=sys.argv[1:], prog=sys.argv[0]):
    import argparse
    parser = argparse.ArgumentParser(
        prog=prog,
    )
    parser.add_argument('filename', metavar='FILE')

    args = parser.parse_args(argv)
    ns = vars(args)

    return ns


def main(filename):
    if os.path.basename(filename) == filename:
        filename = os.path.join('.', filename)
    print(f'reading from {filename}')
    print()
    with open(filename) as infile:
        traces = iter(_process_lines(infile))

        first = next(traces)
        while isinstance(first, str):
            print(first)
            first = next(traces)
        start, current = first
        for end, event in _process_lines(infile):
            elapsed = format_elapsed(end - start)
            print(f'{current:30} -> {elapsed}')
            start, current = end, event
        assert event == 'fini'
        print('fini!')


if __name__ == '__main__':
    kwargs = parse_args()
    main(**kwargs)
