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


def _parse_event(line, annotations):
    ts, _, event = line.partition(' ')
    event, _, data = event.partition(' ')

    event = EVENTS[int(event)]
    data = int(data) if data else None

    # XXX datetime.datetime.utcfromtimestamp()?
    ts = decimal.Decimal(ts)

    if event == 'op':
        op = data
        opname = opcode.opname[op]
        data = (op, opname)
    else:
        if data is not None:
            raise NotImplementedError(line)

    return ts, event, data, annotations


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

    return label, text


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
        info = _parse_info(line)
        if info:
            yield 'info', info
        else:
            yield 'comment', line

    yield (None, None)

    annotations = []
    for line in lines:
        if not line:
            # There probably shouldn't be any blank lines.
            raise NotImplementedError
        if line.startswith('#'):
            info = _parse_info(line)
            if info:
                yield 'info', info
            else:
                yield 'comment', line
            annotations.append(info or line)
        else:
            yield 'event', _parse_event(line, tuple(annotations))
            annotations.clear()


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


def _format_info(info, *, align=True):
    label, text = info
    if align:
        yield f'# {label + ":":20} {text}'
    else:
        yield f'# {label}: {text}'


def _format_event(event, end=None):
    start, name, data, annotations = event

    if end is not None:
        elapsed = format_elapsed(end - start)
    else:
        elapsed = ''

    if name == 'op':
        op, opname = data
        data = f'{opname or "???"} ({op})'
    else:
        data = ''

    line = f'{elapsed:15} {name:15} {data}'

    if annotations:
        for entry in annotations[:-1]:
            if isinstance(entry, str):
                yield entry
            else:
                yield from _format_info(entry)
        infolines = list(_format_info(annotations[-1], align=False))
        if len(infolines) == 1:
            line = f'{line:50} {infolines[0]}'
        else:
            yield from _format_info(annotations[-1])
    yield line


def _render_traces(traces):
    traces = iter(traces)

    # Print the header first.
    for kind, entry in traces:
        if kind is None:
            break
        if kind == 'comment':
            yield entry
        elif kind == 'info':
            yield from _format_info(entry)
        else:
            raise NotImplementedError((kind, entry))
    yield ''

    with _printed_section('trace'):
        current = None
        for kind, entry in traces:
            line = None
            if kind == 'comment':
                pass  # covered by annotations
            elif kind == 'info':
                pass  # covered by annotations
            elif kind == 'event':
                if current:
                    end, _, _, _ = entry
                    yield from _format_event(current, end)
                current = entry
            else:
                raise NotImplementedError((kind, entry))

        assert current[1] == 'fini'
        yield from _format_event(current)
        yield 'fini'


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


def main(filename=None):
    filename = _resolve_filename(filename)
    if not filename:
        raise Exception(f'no possible trace files found (match: {filename})')
    print(f'reading from {filename}')
    print()
    with open(filename) as infile:
        traces = _process_lines(infile)
        for line in _render_traces(traces):
            print(line)


if __name__ == '__main__':
    kwargs = parse_args()
    main(**kwargs)
