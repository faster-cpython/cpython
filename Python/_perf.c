
#include <assert.h>
#include <stdio.h>
#include <time.h>
#include <fcntl.h>

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

void
_PyPerf_TraceInit(void)
{
    const char *filename = "eval_loop.trace";
    _trace_file = fopen(filename, "w");
    assert(_trace_file != NULL);

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
