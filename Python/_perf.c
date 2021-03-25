
#include <assert.h>
#include <stdio.h>
#include <time.h>
#include <fcntl.h>
#include "Python.h"
#include "pycore_initconfig.h"  // _PyArgv
#include "frameobject.h"        // PyFrameObject

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

static inline struct timespec
_now(void)
{
    struct timespec time = {0,0};
    clock_gettime(CLOCK_MONOTONIC, &time);
    return time;
}

static char *
_render_argv(int argc, char **argv)
{
    assert(argc && argv[0]);  // There must be a program at least.
    size_t size = 0;
    for (int i=0; i < argc; i++) {
        size += strlen(argv[i]);
    }
    char *res = (char *)PyMem_RawMalloc(size + 1);
    if (res == NULL) {
        return NULL;
    }

    char *ptr = res;
    for (int i=0; i < argc; i++) {
        const char *arg = argv[i];
        while (*arg) {
            *ptr++ = *arg++;
        }
    }
    *ptr = 0;  // res[size]

    return res;
}

static inline const char *
_get_frame_name(PyFrameObject *f)
{
    // XXX qualname?
    // XXX identify class vs. module vs. func?
    return PyUnicode_AsUTF8(f->f_code->co_name);
}

static const char *
_get_filename_default(const char *name)
{
    const char *suffix = ".trace";
    char *filename = (char *)PyMem_Malloc(strlen(name) + strlen(suffix) + 1);
    sprintf(filename, "%s%s", name, suffix);
    return filename;
}

static const char *
_get_filename(const char *name)
{
    // XXX Look up an optional env var?
    return _get_filename_default(name);
}

static inline void
_log_id(FILE *logfile, const char *id)
{
    struct timespec time = _now();
    fprintf(logfile, "%ld.%ld <%s>\n", time.tv_sec, time.tv_nsec, id);
}

static inline void
_log_op(FILE *logfile, int op)
{
    struct timespec time = _now();
    fprintf(logfile, "%ld.%ld <op %d>\n", time.tv_sec, time.tv_nsec, op);
}

static inline void
_log_info(FILE *logfile, const char *label, const char *text)
{
    fprintf(logfile, "# %s: %s\n", label, text);
}

static inline void
_log_info_amount(FILE *logfile, const char *label, long value, const char *units)
{
    fprintf(logfile, "# %s: %ld %s\n", label, value, units);
}

static FILE *_fake_api_file = NULL;

// Duplicate _PyPerf_Trace() without relying on _trace_file.
void
_fake_api(const char *id)
{
    if (_fake_api_file) {
        _log_id(_fake_api_file, id);
    }
}

static long
_get_trace_cost_ns(FILE *f)
{
    assert(f);
    _fake_api_file = f;
    long pos_orig = ftell(f);

    // XXX Repeat multiple times for a stable result?
    struct timespec before = _now();
    _fake_api("???");
    struct timespec after = _now();

    fseek(f, pos_orig, SEEK_SET);
    _fake_api_file = NULL;

    struct timespec elapsed = timespec_sub(after, before);
    assert(elapsed.tv_sec == 0);
    return elapsed.tv_nsec;
}

static FILE * _trace_file = NULL;

//======================
// the public API
//======================

void
_PyPerf_Trace(const char *id)
{
    if (_trace_file) {
        _log_id(_trace_file, id);
    }
}

void
_PyPerf_TraceOp(int op)
{
    if (_trace_file) {
        _log_op(_trace_file, op);
    }
}

void
_PyPerf_TraceFrameEnter(PyFrameObject *f)
{
    if (_trace_file) {
        const char *funcname = _get_frame_name(f);
        _log_info(_trace_file, "func", funcname);
        // XXX Differentiate generators?
        _log_id(_trace_file, "enter");
    }
}

void
_PyPerf_TraceFrameExit(PyFrameObject *f)
{
    if (_trace_file) {
        const char *funcname = _get_frame_name(f);
        _log_info(_trace_file, "func", funcname);
        // XXX Differentiate generators?
        _log_id(_trace_file, "exit");
    }
}

void
_PyPerf_TraceInit(_PyArgv *args)
{
    const char *filename = _get_filename("eval_loop");
    _trace_file = fopen(filename, "w");
    assert(_trace_file != NULL);

    long cost = _get_trace_cost_ns(_trace_file);

    // Write a "header".
    assert(args);
    if (args->use_bytes_argv) {
        char *argv_str = _render_argv((int)args->argc, (char **)args->bytes_argv);
        _log_info(_trace_file, "argv", (const char *)argv_str);
        PyMem_RawFree(argv_str);
    }
    else {
        // XXX Use Py_EncodeLocaleRaw()?
        _log_info(_trace_file, "argv", "<unknown>");
    }
    _log_info_amount(_trace_file, "per-trace", cost, "ns");
    fprintf(_trace_file, "\n");  // Add a blank line.

    // Log the first event.
    _PyPerf_Trace("init");
}

void
_PyPerf_TraceFini(void)
{
    if (_trace_file == NULL) {
        return;
    }
    // Log the very last event.
    _PyPerf_Trace("fini");
    fclose(_trace_file);
    _trace_file = NULL;
}
