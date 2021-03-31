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


def _ensure_up_to_date(actual, expected):
    actual = set(actual)
    expected = set(expected)
    if actual - expected:
        raise NotImplementedError(actual - expected)
    elif expected - actual:
        raise NotImplementedError(expected - actual)


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
        cls = type(self)
        code = cls.__init__.__code__
        return code.co_varnames[1: code.co_argcount]

    @property
    def labels(self):
        return tuple(name.replace('_', ' ') for name in self._names)

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

    def summarize(self):
        keys = ('argv', 'start', 'end', 'elapsed')
        summary = {k: getattr(self, k) for k in keys}
        return summary

    # XXX "_names" and "labels" would be more correct as class properties.


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
                label, text = info
                if label == 'log written':
                    elapsed = parse_clock(text.split()[0])
                    info = (label, elapsed)
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


def _iter_events(traces, depth=None, *, hidelog=True):
    current = None
    for kind, entry, _, _ in traces:
        line = None
        if kind == 'comment':
            pass
        elif kind == 'info':
            pass
        elif kind == 'event':
            if current:
                start, event, _, _ = current
                end, _, _, annotations = entry
                elapsed = end - start
                if elapsed < 0:
                    raise NotImplementedError((current, entry))
                if hidelog:
                    for info in annotations or ():
                        if isinstance(info, str):
                            continue
                        label, info_data = info
                        if label != 'log written':
                            continue
                        elapsed -= info_data
                if depth is not None:
                    if event == 'exit':
                        depth -= 1
                yield current, elapsed, depth
                if depth is not None:
                    if event == 'enter':
                        depth += 1
            current = entry
        else:
            raise NotImplementedError((kind, entry))

    assert current[1] == 'fini'
    yield current, None, None


def _summarize(header, traces):
    summary = header.summarize()
    summary.update({
        'events': {
            # name -> { count, total_elapsed }
        },
        'ops': {
            # name -> { count, total_elapsed }
        },
        'funcs': {
            # name -> count
            # XXX Track a summary per func?
        },
    })
    events = summary['events']
    ops = summary['ops']
    funcs = summary['funcs']
    for event, elapsed, _ in _iter_events(traces, hidelog=True):
        _, name, data, annotations = event
        try:
            event = events[name]
        except KeyError:
            event = events[name] = {
                'count': 0,
                'total_elapsed_sec': 0,
            }
        event['count'] += 1
        if elapsed:
            event['total_elapsed_sec'] += elapsed

        if name == 'enter':
            for info in annotations or ():
                if isinstance(info, str):
                    continue
                label, funcname = info
                if label == 'func':
                    if funcname in funcs:
                        funcs[funcname] += 1
                    else:
                        funcs[funcname] = 1
        elif name == 'op':
            _, opname = data
            try:
                op = ops[opname]
            except KeyError:
                op = ops[opname] = {
                    'count': 0,
                    'total_elapsed_sec': 0,
                }
            op['count'] += 1
            if elapsed:
                op['total_elapsed_sec'] += elapsed
    return summary


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
    label, data = info
    if label == 'log written':
        elapsed = data
        if elapsed < 0.001:
            text = format_elapsed(elapsed, align=False)
        else:
            text = f'{elapsed} s'
    else:
        text = data
    if align:
        yield f'# {label + ":":15} {text}'
    else:
        yield f'# {label}: {text}'


def _format_event(event, elapsed=None, depth=None, *, hidelog=False):
    _, name, data, annotations = event

    if elapsed is not None:
        elapsed = format_elapsed(elapsed)
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
                if hidelog and entry[0] == 'log written':
                    continue
                yield from _format_info(entry)
        last = annotations[-1]
        if isinstance(last, str):
            yield last
        elif last[0] == 'log written':
            if not hidelog:
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


def _render_header_summary(summary, keys):
    for key in keys:
        value = summary[key]
        if key in ('start', 'end'):
            text = format_timestamp(value, secondsonly=False)
        elif key == 'elapsed':
            text = format_elapsed(value, align=False)
        elif isinstance(value, str):
            text = value
        else:
            raise NotImplementedError((key, value))
        yield key, text


def _render_header(header):
    labels = header.labels
    keys = ('argv', 'start', 'end', 'elapsed')
    summary = header.summarize()
    # XXX Ensuring "keys" matches "summary" should be done in a unit test.
    _ensure_up_to_date(summary, keys)

    skipkeys = False
    for kind, entry, _, _ in header.entries:
        if kind == 'comment':
            yield entry
        elif kind == 'info':
            label, _ = entry
            if label in labels:
                if skipkeys:
                    continue
                for rendered in _render_header_summary(summary, keys):
                    yield from _format_info(rendered)
                skipkeys = True
            else:
                raise NotImplementedError(entry)
        else:
            raise NotImplementedError((kind, entry))


def _render_summary(summary, *, showfuncs=False):
    headerkeys = ('argv', 'start', 'end', 'elapsed')
    # XXX Ensuring "summary" matches should be done in a unit test.
    _ensure_up_to_date(summary, headerkeys + ('events', 'ops', 'funcs'))

    for key, text in _render_header_summary(summary, headerkeys):
        yield f'{key + ":":10} {text}'
    yield ''

    header = f' {"name":^20}   {"count":^10}   {"mean elapsed":^12}'
    div = ' '.join('-' * w for w in (20+2, 10+2, 12+2))
    fmt = ' {name:20}   {count:>10,}   {elapsed:>12}'

    subsummary = summary['events']
    yield 'events:'
    yield ''
    yield header
    yield div
    for name, info in sorted(subsummary.items()):
        count = info['count']
        elapsed = format_elapsed(info['total_elapsed_sec'] / count)
        yield fmt.format(name=name, count=count, elapsed=elapsed)
    yield div
    yield f' total: {len(subsummary)}/{len(EVENTS)}'
    yield ''

    subsummary = summary['ops']
    yield 'ops:'
    yield ''
    yield header
    yield div
    for opname, info in sorted(subsummary.items()):
        count = info['count']
        elapsed = format_elapsed(info['total_elapsed_sec'] / count)
        yield fmt.format(name=opname, count=count, elapsed=elapsed)
    yield div
    yield f' total: {len(subsummary)}/{len(opcode.opmap)}'
    yield ''

    subsummary = summary['funcs']
    if showfuncs:
        header = f' {"name":^30}   {"count":^10}'
        div = ' '.join('-' * w for w in (30+2, 10+2))
        fmt = ' {name:30}   {count:>10,}'

        yield ''
        yield 'funcs:'
        yield ''
        yield header
        yield div
        for funcname, count in sorted(subsummary.items()):
            yield fmt.format(name=funcname, count=count)
        yield div
    else:
        yield f'funcs: {len(subsummary):,} called (by "simple" name)'
    yield ''


def _render_all(header, traces, *, fmt='simple-indent', hidelog=True):
    traces = iter(traces)

    depth = None
    if fmt == 'simple-indent':
        fmt = 'simple'
        depth = 0
    if fmt == 'simple':
        yield from _render_header(header)
        yield ''
        with _printed_section('trace'):
            for current, elapsed, depth in _iter_events(traces, depth, hidelog=hidelog):
                yield from _format_event(current, elapsed, depth=depth, hidelog=hidelog)
    elif fmt == 'raw':
        # This is handled in main().
        raise NotImplementedError
    elif fmt == 'summary':
        summary = _summarize(header, traces)
        yield from _render_summary(summary)
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
