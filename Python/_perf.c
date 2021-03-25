
#include <assert.h>
#include <stdio.h>
#include <time.h>
#include <fcntl.h>
#include "Python.h"
#include "pycore_initconfig.h"  // _PyArgv

static FILE * _trace_file = NULL;

void
_PyPerf_Trace(const char *id)
{
    struct timespec time = {0,0};
    clock_gettime(CLOCK_MONOTONIC, &time);
    if (_trace_file) {
        fprintf(_trace_file, "%ld.%ld %s\n", time.tv_sec, time.tv_nsec, id);
    }
}

void
_PyPerf_TraceOp(int op)
{
    struct timespec time = {0,0};
    clock_gettime(CLOCK_MONOTONIC, &time);
    if (_trace_file) {
        fprintf(_trace_file, "%ld.%ld <op %d>\n", time.tv_sec, time.tv_nsec, op);
    }
}

// timespec_sub is provided by <timespec.h> in C11.
static struct timespec
timespec_sub(struct timespec after, struct timespec before)
{
    struct timespec elapsed = {
        .tv_sec = after.tv_sec - before.tv_sec,
        .tv_nsec = after.tv_nsec - before.tv_nsec,
    };
    if (elapsed.tv_nsec < 0) {
        elapsed.tv_sec -= 1;
#ifndef NSEC_PER_SEC
#define NSEC_PER_SEC 1000000000L
#endif
        elapsed.tv_nsec += NSEC_PER_SEC;
    }
    return elapsed;
}

static long
_get_trace_cost_ns(void)
{
    // XXX Repeat multiple times for a stable result?
    struct timespec before = {0,0};
    struct timespec after = {0,0};
    clock_gettime(CLOCK_MONOTONIC, &before);
    assert(_trace_file);
    _PyPerf_Trace("???");
    clock_gettime(CLOCK_MONOTONIC, &after);
    fseek(_trace_file, 0, SEEK_SET);

    struct timespec elapsed = timespec_sub(after, before);
    assert(elapsed.tv_sec == 0);
    return elapsed.tv_nsec;
}

static void
_print_argv(FILE *f, int argc, char **argv)
{
    fprintf(f, "%s", argv[0]);
    for (int i = 1; i < argc; i++) {
        fprintf(f, " %s", argv[i]);
    }
}

void
_PyPerf_TraceInit(_PyArgv *args)
{
    const char *filename = "eval_loop.trace";
    _trace_file = fopen(filename, "w");
    assert(_trace_file != NULL);

    long cost = _get_trace_cost_ns();

    // Write a "header".
    assert(args);
    if (args->use_bytes_argv) {
        fprintf(_trace_file, "# argv: ");
        _print_argv(_trace_file, (int)args->argc, (char **)args->bytes_argv);
        fprintf(_trace_file, "\n");
    }
    else {
        // XXX Use Py_EncodeLocaleRaw()?
        fprintf(_trace_file, "# argv: <unknown>");
    }
    fprintf(_trace_file, "# per-trace: %ld ns\n", cost);
    fprintf(_trace_file, "\n");  // Add a blank line.

    _PyPerf_Trace("<init>");
}

void
_PyPerf_TraceFini(void)
{
    if (_trace_file == NULL) {
        return;
    }
    _PyPerf_Trace("<fini>");
    fclose(_trace_file);
    _trace_file = NULL;
}
