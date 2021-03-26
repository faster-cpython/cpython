import contextlib
import datetime
import decimal
import opcode
import os
import os.path
import sys


def log_info(msg):
    # XXX Log to stderr?  Ideally we would just use the logging module.
    print(msg)


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


def iter_clean_lines(lines):
    if isinstance(lines, str):
        lines = lines.splitlines()
    for rawline in lines:
        if rawline.endswith('\r\n'):
            rawline = rawline[:-2]
        elif rawline.endswith('\n'):
            rawline = rawline[:-1]

        line = rawline.strip()
        line, sep, comment = line.partition('#')
        if sep:
            line = line.rstrip()
            comment = comment.lstrip()
        else:
            comment = None

        yield line or None, comment, rawline


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


def _parse_info(comment):
    if comment.startswith('#'):
        comment = comment[1:].lstrip()
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


def _parse_header_lines(cleanlines):
    for line, comment, rawline in cleanlines:
        if not rawline:
            # A single blank line divides the header and data.
            return
        if line is not None:
            # All lines in the header are info/comments.
            raise ValueError(f'unexpected line in header: {rawline!r}')
        if comment is None:
            raise NotImplementedError('should be unreachable')

        info = _parse_info(comment)
        if info:
            yield 'info', info, f'# {comment}', rawline
        else:
            yield 'comment', comment, f'# {comment}', rawline


def _parse_trace_lines(cleanlines):
    annotations = []
    for line, comment, rawline in cleanlines:
        if line is None and comment is None:
            # There should not be any blank lines among the trace lines.
            raise ValueError('got an unexpected blank line')

        if comment is not None:
            info = _parse_info(comment)
            commentline = f'# {comment}'
            if info:
                yield 'info', info, commentline, rawline
            else:
                yield 'comment', comment, commentline, rawline
            annotations.append(info or commentline)

        if line is not None:
            event = _parse_event(line, tuple(annotations))
            yield 'event', event, line, rawline
            annotations.clear()


def _parse_lines(rawlines):
    cleanlines = iter(iter_clean_lines(rawlines))
    yield from _parse_header_lines(cleanlines)
    yield (None, None, None, None)
    yield from _parse_trace_lines(cleanlines)


def _sanitize_raw(lines):
    for rawline in lines:
        if rawline.endswith('\r\n'):
            rawline = rawline[:-2]
        elif rawline.endswith('\n'):
            rawline = rawline[:-1]
        yield rawline


##################################
# output

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


def _format_event(event, end=None, depth=None):
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

    if depth is None:
        line = f'{elapsed:15} {name:15} {data}'
    else:
        indent = '|  ' * depth if depth else ''
        line = f'{elapsed:15} {indent + name:25} {data}'

    if annotations:
        for entry in annotations[:-1]:
            if isinstance(entry, str):
                yield entry
            else:
                yield from _format_info(entry)
        infolines = list(_format_info(annotations[-1], align=False))
        if len(infolines) == 1:
            if depth is None:
                line = f'{line:50} {infolines[0]}'
            else:
                line = f'{line:65} {infolines[0]}'
        else:
            yield from _format_info(annotations[-1])
    yield line


def _render_traces(traces, *, fmt='simple-indent'):
    traces = iter(traces)

    depth = None
    if fmt == 'simple-indent':
        fmt = 'simple'
        depth = 0
    if fmt == 'simple':
        # Print the header first.
        for kind, entry, _, _ in traces:
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
            for kind, entry, _, _ in traces:
                line = None
                if kind == 'comment':
                    pass  # covered by annotations
                elif kind == 'info':
                    pass  # covered by annotations
                elif kind == 'event':
                    if current:
                        end, _, _, _ = entry
                        _, event, _, _ = current
                        if depth is not None:
                            if event == 'exit':
                                depth -= 1
                        yield from _format_event(current, end, depth=depth)
                        if depth is not None:
                            if event == 'enter':
                                depth += 1
                    current = entry
                else:
                    raise NotImplementedError((kind, entry))

            assert current[1] == 'fini'
            yield from _format_event(current)
    elif fmt == 'raw':
        # This is handled in main().
        raise NotImplementedError
    elif fmt == 'summary':
        raise NotImplementedError
    else:
        raise ValueError(f'unsupported fmt {fmt!r}')


##################################
# formats

FORMATS = [
    'raw',
    'simple',
    'simple-indent',
    'summary',
]


##################################
# the script

def parse_args(argv=sys.argv[1:], prog=sys.argv[0]):
    import argparse
    parser = argparse.ArgumentParser(
        prog=prog,
    )
    # XXX Add --verbose (-v) and --quiet (-q).
    parser.add_argument('--format', dest='fmt',
                        choices=list(FORMATS), default='simple-indent')
    parser.add_argument('filename', metavar='FILE', nargs='?')

    args = parser.parse_args(argv)
    ns = vars(args)

    return ns


def main(filename=None, *, fmt='simple-indent'):
    filename = _resolve_filename(filename)
    if not filename:
        raise Exception(f'no possible trace files found (match: {filename})')
    log_info(f'reading from {filename}')
    log_info(f'(showing results in {fmt!r} format)')
    log_info('')
    with open(filename) as infile:
        if fmt == 'raw':
            for line in _sanitize_raw(infile):
                print(line)
        else:
            traces = _parse_lines(infile)
            for line in _render_traces(traces, fmt=fmt):
                print(line)


if __name__ == '__main__':
    try:
        kwargs = parse_args()
        main(**kwargs)
    except BrokenPipeError:
        pass
