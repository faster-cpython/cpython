
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
