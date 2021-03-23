"""
Some helper functions to analyze the output of sys.getdxp() (which is
only available if Python was built with -DDYNAMIC_EXECUTION_PROFILE).
These will tell you which opcodes have been executed most frequently
in the current process, and, if Python was also built with -DDXPAIRS,
will tell you which instruction _pairs_ were executed most frequently,
which may help in choosing new instructions.

If you're running a script you want to profile, a simple way to get
the common pairs is:

$ PYTHONPATH=$PYTHONPATH:<python_srcdir>/Tools/scripts \
./python -i -O the_script.py --args
...
> from analyze_dxp import *
> s = render_common_pairs()
> open('/tmp/some_file', 'w').write(s)
"""

import json
import opcode
import operator
import os
import sys


if hasattr(sys, "getdxp"):
    import copy
    import threading


    _profile_lock = threading.RLock()
    _cumulative_profile = sys.getdxp()


    def reset_profile():
        """Forgets any execution profile that has been gathered so far."""
        with _profile_lock:
            sys.getdxp()  # Resets the internal profile
            global _cumulative_profile
            _cumulative_profile = sys.getdxp()  # 0s out our copy.


    def merge_profile():
        """Reads sys.getdxp() and merges it into this module's cached copy.

        We need this because sys.getdxp() 0s itself every time it's called."""

        with _profile_lock:
            new_profile = sys.getdxp()
            if has_pairs(new_profile):
                for first_inst in range(len(_cumulative_profile)):
                    for second_inst in range(len(_cumulative_profile[first_inst])):
                        _cumulative_profile[first_inst][second_inst] += (
                            new_profile[first_inst][second_inst])
            else:
                for inst in range(len(_cumulative_profile)):
                    _cumulative_profile[inst] += new_profile[inst]


    def snapshot_profile():
        """Returns the cumulative execution profile until this call."""
        with _profile_lock:
            merge_profile()
            return copy.deepcopy(_cumulative_profile)


def load_profile(filename=None):
    with open(filename or 'dxp.json') as infile:
        return json.load(infile)


# If Python was built with -DDXPAIRS, sys.getdxp() returns a list of
# lists of ints.  Otherwise it returns just a list of ints.
def has_pairs(profile):
    """Returns True if the Python that produced the argument profile
    was built with -DDXPAIRS."""

    return len(profile) > 0 and isinstance(profile[0], list)


def common_instructions(profile):
    """Returns the most common opcodes in order of descending frequency.

    The result is a list of tuples of the form
      (opcode, opname, # of occurrences)

    """
    # Ignore opcodes with a count of 0.
    result = [v for v in _common_instructions(profile) if v[-1] > 0]
    result.sort(key=operator.itemgetter(2), reverse=True)
    return result


def _common_instructions(profile):
    if has_pairs(profile) and profile:
        inst_list = profile[-1]
    else:
        inst_list = profile
    for op, count in enumerate(inst_list):
        yield (op, opcode.opname[op], count)


def common_pairs(profile):
    """Returns the most common opcode pairs in order of descending frequency.

    The result is a list of tuples of the form
      ((1st opcode, 2nd opcode),
       (1st opname, 2nd opname),
       # of occurrences of the pair)

    """
    if not has_pairs(profile):
        return []
    result = [v for v in _common_pairs(profile) if v[-1] > 0]
    result.sort(key=operator.itemgetter(2), reverse=True)
    return result


def _common_pairs(profile):
    # Drop the row of single-op profiles with [:-1]
    for op1, op1profile in enumerate(profile[:-1]):
        for op2, count in enumerate(op1profile):
            yield ((op1, op2), (opcode.opname[op1], opcode.opname[op2]), count)


def _summarize(profile):
    pairs = [(p, c) for _, p, c in _common_pairs(profile) if c > 0]

    op1_pairs = {}
    op2_pairs = {}
    for names, count in pairs:
        op1, op2 = names
        if op1 not in op1_pairs:
            op1_pairs[op1] = [0, 0]
        if op2 not in op2_pairs:
            op2_pairs[op2] = [0, 0]

        op1_pairs[op1][0] += 1
        op1_pairs[op1][1] += count
        op2_pairs[op2][0] += 1
        op2_pairs[op2][1] += count

    return {
        'totals': {
            'pairs': len(pairs),
            'op1_pairs': len(op1_pairs),
            'op2_pairs': len(op2_pairs),
            'used': sum(c for _, c in pairs)
        },
        # XXX mean?
        # XXX distribution?
        'top10': {
            'pairs': sorted(pairs, key=lambda v: v[1], reverse=True)[:10],
            'op1_pairs': sorted(((o[0], o[1][0]) for o in op1_pairs.items()),
                                key=lambda v: v[1], reverse=True)[:10],
            'op2_pairs': sorted(((o[0], o[1][0]) for o in op2_pairs.items()),
                                key=lambda v: v[1], reverse=True)[:10],
            'op1_count': sorted(((o[0], o[1][1]) for o in op1_pairs.items()),
                                key=lambda v: v[1], reverse=True)[:10],
            'op2_count': sorted(((o[0], o[1][1]) for o in op2_pairs.items()),
                                key=lambda v: v[1], reverse=True)[:10],
        },
    }


def render_common_pairs(profile=None):
    """Renders the most common opcode pairs to a string in order of
    descending frequency.

    The result is a series of lines of the form:
      # of occurrences: ('1st opname', '2nd opname')

    """
    if profile is None:
        if not hasattr(sys, "getdxp"):
            raise RuntimeError("missing dxp profile: Python built without -DDYNAMIC_EXECUTION_PROFILE")
        profile = snapshot_profile()
    if not has_pairs(profile):
        return ''
    lines = _render_profile(profile, fmt='simple') + ['']
    return os.linesep.join(lines)


def _render_profile(profile, *, fmt='summary', sort='count', descending=True):
    if fmt == 'summary' or not fmt:
        summary = _summarize(profile)
        yield '============='
        yield '== Summary =='
        yield '============='
        yield ''
        yield '- Usage -'
        yield ''
        yield f'total: {" " * 40} {summary["totals"]["used"]:10,}'
        yield ''
        yield 'Top 10 pairs:'
        for pair, count in summary['top10']['pairs']:
            op1, op2 = pair
            yield f'  {op1:20} --> {op2:20} {count:>10,}'
        yield ''
        yield 'Top 10 op1:'
        for op, count in summary['top10']['op1_count']:
            yield f'  {op:20} {" " * 24} {count:>10,}'
        yield ''
        yield 'Top 10 op2:'
        for op, count in summary['top10']['op2_count']:
            yield f'  {op:20} {" " * 24} {count:>10,}'
        yield ''
        yield '- Pairs -'
        yield ''
        yield f'total:     {" " * 8} {summary["totals"]["pairs"]:>6,} / {256 * 256:,}'
        yield f'total op1: {" " * 11} {summary["totals"]["op1_pairs"]:>3,} / 256'
        yield f'total op2: {" " * 11} {summary["totals"]["op2_pairs"]:>3,} / 256'
        yield ''
        yield 'Top 10 op1:'
        for op, count in summary['top10']['op1_pairs']:
            yield f'  {op:20} {count:>3,}'
        yield ''
        yield 'Top 10 op2:'
        for op, count in summary['top10']['op2_pairs']:
            yield f'  {op:20} {count:>3,}'
    elif fmt == 'flat':
        # XXX sort?
        for op1, op1profile in enumerate(profile):
            # XXX break up into multiple lines?
            yield f'{op1:>3}: {op1profile}'
    elif fmt == 'json':
        # XXX sort?
        # XXX indent?
        text = json.dumps(profile)
        yield from text.splitlines()
    elif fmt == 'raw':
        # XXX pprint?
        yield str(profile)
    else:
        pairs = [v for v in _common_pairs(profile) if v[-1] > 0]
        if sort == 'count':
            pairs.sort(key=operator.itemgetter(2), reverse=descending)
        elif sort == 'op1':
            pairs.sort(key=(lambda v: (v[1][0], v[1][1])),
                       reverse=not descending)
        elif sort == 'op2':
            pairs.sort(key=(lambda v: (v[1][1], v[1][0])),
                       reverse=not descending)
        elif sort != 'raw':
            raise NotImplementedError(sort)
        if fmt == 'simple':
            for _, ops, count in pairs:
                op1, op2 = ops
                yield f'  {op1:20} --> {op2:20} {count:>10,}'
            yield f'total: {len(pairs)} pairs'
        else:
            raise ValueError(f'unsupported fmt {fmt!r}')


#############################
# the script

def parse_args(argv=sys.argv[1:], prog=sys.argv[0]):
    import argparse
    parser = argparse.ArgumentParser()
    formats = ['summary', 'simple', 'flat', 'json', 'raw']
    parser.add_argument('--format', dest='fmt', choices=formats,
                        default='summary')
    for fmt in formats:
        parser.add_argument(f'--{fmt}', dest='fmt',
                            action='store_const', const=fmt)
    parser.add_argument('--descending', action='store_true')
    parser.add_argument('--ascending', dest='descending', action='store_false')
    parser.set_defaults(descending=True)
    parser.add_argument('--sort', choices=['count', 'op1', 'op2', 'raw'],
                        default='count')
    parser.add_argument('filename', metavar='FILE')
    args = parser.parse_args()

    return vars(args)


def main(filename=None, **kwargs):
    profile = load_profile(filename)
    for line in _render_profile(profile, **kwargs):
        print(line)


if __name__ == '__main__':
    main(**parse_args())
