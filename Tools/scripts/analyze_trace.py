import contextlib
import datetime
import decimal
import opcode
import os
import os.path
import sys


def _parse_filename(filename):
    if not filename.endswith('.trace'):
        return None, None
    name, _, _ = filename.rpartition('.')
    before, sep, ts = name.rpartition('-')
    if sep and before and ts.isdigit():
        name = before
        ts = int(ts)
    else:
        ts = None
    return name, ts or None


def _resolve_filename(filename=None):
    prefix = None
    if filename:
        if os.path.isdir(filename):
            dirname = filename
        else:
            basename = os.path.basename(filename)
            if basename == filename:
                filename = os.path.join('.', basename)
                dirname = '.'
            else:
                dirname = os.path.dirname(filename)
            if not os.path.exists(dirname):
                return filename

            try:
                first = basename.index('*')
            except ValueError:
                if os.path.exists(filename) or filename.endswith('.trace'):
                    return filename
                prefix = basename
            else:
                if first != len(basename) - 1:
                    raise NotImplementedError(filename)
                prefix = basename[:-1]
    else:
        dirname = '.'

    # Find the best match.
    if prefix:
        maybe = set()
        for basename in os.listdir(dirname):
            if not basename.endswith('.trace'):
                continue
            if not basename.startswith(prefix):
                continue
            _, ts = _parse_filename(basename)
            maybe.add((ts or 0, os.path.join(dirname, basename)))
    else:
        byname = {}
        for basename in os.listdir(dirname):
            name, ts = _parse_filename(basename)
            if not name:
                continue
            if name not in byname:
                byname[name] = set()
            byname[name].add((ts or 0, os.path.join(dirname, basename)))
        if not byname:
            maybe = set()
        elif len(byname) > 1:
            # XXX Print a message?
            maybe = set()
        else:
            maybe, = byname.values()
    maybe = sorted(maybe)
    return maybe[-1][1] if maybe else None


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


EVENTS = [
    # These match the _PyPerf_Event enum by index.
    'init',
    'fini',
    'enter',
    'exit',
    'loop enter',
    'loop exit',
    'loop exception',
    'loop error',
    'op',
]


def _parse_event(line, info=None):
    ts, _, event = line.partition(' ')
    event, _, data = event.partition(' ')
    event = EVENTS[int(event)]

    # XXX datetime.datetime.utcfromtimestamp()?
    ts = decimal.Decimal(ts)

    if event == 'op':
        op = int(data)
        opname = opcode.opname[op]
        event = f'op {opname:20} ({op})'
    else:
        if data:
            raise NotImplementedError(line)

    # XXX What should we do with "info"?

    return ts, event


def _parse_info(line):
    # It starts with "#".
    comment = line[1:].strip()
    label, sep, text = comment.partition(':')
    label = label.strip()
    text = text.strip()
    if not sep or not label or not text:
        return None

    if label == 'start time':
        ts = int(text.split()[0])
        dt = datetime.datetime.utcfromtimestamp(ts)
        text = dt.isoformat(' ').split('.')[0]
        text += ' UTC'
    elif label == 'end time':
        ts = int(text.split()[0])
        dt = datetime.datetime.utcfromtimestamp(ts)
        text = dt.isoformat(' ').split('.')[0]
        text += ' UTC'

    line = f'# {label + ":":20} {text}'
    return line, (label, text)


def _iter_clean_lines(lines):
    if isinstance(lines, str):
        lines = lines.splitlines()
    for line in lines:
        line = line.strip()
        yield line or None


def _process_lines(lines):
    lines = iter(_iter_clean_lines(lines))

    # Handle the header first.
    for line in lines:
        if not line:
            break
        if not line.startswith('#'):
            raise NotImplementedError(line)
        line, _ = _parse_info(line)
        yield line
    yield None

    info = None
    for line in lines:
        if not line:
            # There probably shouldn't be any blank lines.
            raise NotImplementedError
        if line.startswith('#'):
            if info:
                raise NotImplementedError((info, line, next(lines)))
            line, info = _parse_info(line)
            yield line
        else:
            yield _parse_event(line, info)
            info = None


@contextlib.contextmanager
def _printed_section(name):
    name = name.upper()
    div = '#' * 20
    print(div)
    print(f'# BEGIN {name}')
    print(div)
    print()
    yield
    print()
    print(div)
    print(f'# END {name}')
    print(div)


##################################
# the script

def parse_args(argv=sys.argv[1:], prog=sys.argv[0]):
    import argparse
    parser = argparse.ArgumentParser(
        prog=prog,
    )
    parser.add_argument('filename', metavar='FILE', nargs='?')

    args = parser.parse_args(argv)
    ns = vars(args)

    return ns


DIV = '#' * 20


def main(filename=None):
    filename = _resolve_filename(filename)
    if not filename:
        raise Exception(f'no possible trace files found (match: {filename})')
    print(f'reading from {filename}')
    print()
    with open(filename) as infile:
        traces = iter(_process_lines(infile))

        if True:
        #with _printed_section('header'):
            for entry in traces:
                if entry is None:
                    break
                if not isinstance(entry, str):
                    raise NotImplementedError(entry)
                print(entry)
        print()

        with _printed_section('trace'):
            start, current = next(traces)
            for entry in traces:
                if isinstance(entry, str):
                    print(entry)
                    continue
                end, event = entry
                elapsed = format_elapsed(end - start)
                print(f'{current:30} -> {elapsed}')
                start, current = end, event
            assert event == 'fini'
            print(event)


if __name__ == '__main__':
    kwargs = parse_args()
    main(**kwargs)
