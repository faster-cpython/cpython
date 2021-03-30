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


def parse_timestamp(text):
    ts = int(text.split()[0])
    return datetime.datetime.utcfromtimestamp(ts)


def parse_clock(text):
    # We can't just use datetime.timedelta
    # since it does not support nanonseconds.
    if ',' in text:
        text = text.rstrip('0')
    return decimal.Decimal(text)


def clock_elapsed(before, after):
    elapsed = after - before
    seconds = int(elapsed)
    return datetime.timedelta(
        seconds=seconds,
        microseconds=int((elapsed - seconds) * 1_000_000),
    )


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
    'runtime other',
    'enter',
    'exit',
    'loop enter',
    'loop slow',
    'loop fast',
    'dispatch',
    'op',
    'loop exception',
    'loop error',
    'loop exiting',
    'loop exit',
]


def _parse_event(line, annotations):
    ts, _, event = line.partition(' ')
    event, _, data = event.partition(' ')

    event = EVENTS[int(event)]
    data = int(data) if data else None

    ts = parse_clock(ts)

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
    return label, text


class Header:

    __slots__ = ('_entries', '_raw', '_start', '_end')

    def __init__(self, argv, start_time, start_clock, end_clock):
        assert isinstance(start_time, datetime.datetime), start_time
        assert isinstance(start_clock, decimal.Decimal), start_clock
        assert isinstance(end_clock, decimal.Decimal), end_clock
        self._raw = (argv, start_time, start_clock, end_clock)

    def __repr__(self):
        kwargs = ', '.join(f'{k}={v!r}' for k, v in zip(self._names, self._raw))
        return f'{type(self).__name__}({kwargs})'

    @property
    def _names(self):
        code = type(self).__init__.__code__
        return code.co_varnames[1: code.co_argcount]

    @property
    def argv(self):
        return self._raw[0]

    @property
    def start_time(self):
        return self._raw[1]

    @property
    def start_clock(self):
        return self._raw[2]

    @property
    def end_clock(self):
        return self._raw[3]

    @property
    def start(self):
        try:
            return self._start
        except AttributeError:
            nsec = clock_elapsed(int(self.start_clock), self.start_clock)
            self._start = self.start_time + nsec
            return self._start

    @property
    def end(self):
        try:
            return self._end
        except AttributeError:
            self._end = self.resolve_clock(self.end_clock)
            return self._end

    @property
    def elapsed(self):
        return self.end_clock - self.start_clock

    @property
    def entries(self):
        try:
            return self._entries
        except AttributeError:
            entries = []
            for name, value in zip(self._names, self._raw):
                label = name.replace('_', ' ')
                if name == 'start_time':
                    value = int(value.timestamp())
                comment = f'# {label}: {value}'
                entries.append(('info', (label, value), comment, comment))
            self._entries = tuple(entries)
            return self._entries

    def resolve_clock(self, clock):
        elapsed = clock_elapsed(self.start_clock, clock)
        return self.start + elapsed


def _parse_header(cleanlines):
    kwargs = {}
    entries = []
    for line, comment, rawline in cleanlines:
        if not rawline:
            # A single blank line divides the header and data.
            break
        if line is not None:
            # All lines in the header are info/comments.
            raise ValueError(f'unexpected line in header: {rawline!r}')
        if comment is None:
            raise NotImplementedError('should be unreachable')

        info = _parse_info(comment)
        if info:
            label, text = info
            if label == 'argv':
                value = text
            elif label == 'start time':
                value = parse_timestamp(text.split()[0])
            elif label == 'start clock':
                value = parse_clock(text.split()[0])
            elif label == 'end clock':
                value = parse_clock(text.split()[0])
            else:
                raise ValueError(f'unsupported header entry {comment!r}')
            kwargs[label.replace(' ', '_')] = value
            entry = ('info', info, f'# {comment}', rawline)
        else:
            entry = ('comment', comment, f'# {comment}', rawline)
        entries.append(entry)
    header = Header(**kwargs)
    header._entries = tuple(entries)
    return header


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
    header = _parse_header(cleanlines)
    return header, _parse_trace_lines(cleanlines)


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


def format_elapsed(elapsed, *, align=True):
    units = ['s', 'ms', 'µs', 'ns']
    while elapsed < 1:
        before = elapsed
        elapsed *= 1000
        assert before != elapsed
        units.pop(0)
    assert elapsed > 1
    whole = int(elapsed)
    dec = int((elapsed - whole) * 10)
    if align:
        return f'{whole:>3,}.{dec} {units[0]}'
    else:
        return f'{whole:,}.{dec} {units[0]}'


def format_elapsed(elapsed, *, align=True):
    micro = elapsed * 1_000_000
    whole = int(micro)
    dec = int((micro - whole) * 10)
    if align:
        return f'{whole:>5,}.{dec} µs'
    else:
        return f'{whole:,}.{dec} µs'


def format_timestamp(timestamp, *, secondsonly=True):
    offset = timestamp.utcoffset()
    dt = timestamp - offset if offset else timestamp
    text = dt.isoformat(' ')
    if secondsonly:
        text = text.split('.')[0]
    return f'{text} UTC'


def _format_info(info, *, align=True):
    label, text = info
    if align:
        yield f'# {label + ":":15} {text}'
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
        last = annotations[-1]
        if isinstance(last, str):
            yield last
        elif last[0] == 'log written':
            yield from _format_info(last)
        else:
            infolines = list(_format_info(last, align=False))
            if len(infolines) == 1:
                if depth is None:
                    line = f'{line:50} {infolines[0]}'
                else:
                    line = f'{line:65} {infolines[0]}'
            else:
                yield from _format_info(last)
    yield line


def _render_header(header):
    for kind, entry, _, _ in header.entries:
        if kind == 'comment':
            yield entry
        elif kind == 'info':
            label, text = entry
            if label == 'argv':
                yield from _format_info(entry)
            elif label == 'start time':
                text = format_timestamp(header.start, secondsonly=False)
                entry = ('start', text)
                yield from _format_info(entry)

                text = format_timestamp(header.end, secondsonly=False)
                entry = ('end', text)
                yield from _format_info(entry)

                text = format_elapsed(header.elapsed, align=False)
                entry = ('elapsed', text)
                yield from _format_info(entry)
            elif label == 'start clock':
                continue
            elif label == 'end clock':
                continue
            else:
                raise NotImplementedError(entry)
        else:
            raise NotImplementedError((kind, entry))


def _render_all(header, traces, *, fmt='simple-indent'):
    traces = iter(traces)

    depth = None
    if fmt == 'simple-indent':
        fmt = 'simple'
        depth = 0
    if fmt == 'simple':
        yield from _render_header(header)
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
            header, traces = _parse_lines(infile)
            for line in _render_all(header, traces, fmt=fmt):
                print(line)


if __name__ == '__main__':
    try:
        kwargs = parse_args()
        main(**kwargs)
    except BrokenPipeError:
        pass
