
#include <assert.h>
#include <stdio.h>
#include <time.h>
#include <fcntl.h>
#include "Python.h"
#include "pycore_perf.h"        // _PyPerf_Event
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
_clock_now(void)
{
    struct timespec time = {0,0};
    clock_gettime(CLOCK_MONOTONIC, &time);
    return time;
}

static char *
_render_argv(int argc, char **argv)
{
    assert(argc && argv[0]);  // There must be a program at least.
    size_t size = argc;  // One for each space and the null byte.
    for (int i=0; i < argc; i++) {
        size += strlen(argv[i]);
    }
    char *res = (char *)PyMem_RawMalloc(size);
    if (res == NULL) {
        return NULL;
    }

    char *ptr = res;
    for (int i=0; i < argc; i++) {
        const char *arg = argv[i];
        while (*arg) {
            *ptr++ = *arg++;
        }
        *ptr++ = ' ';
    }
    res[size - 1] = '\0';

    return res;
}

static inline size_t
_count_digits(long long val)
{
    assert(val > 0);
    size_t count = 0;
    while (val != 0) {
        val /= 10;
        count += 1;
    }
    return count;
}

static inline const char *
_get_frame_name(PyFrameObject *f)
{
    // XXX qualname?
    // XXX identify class vs. module vs. func?
    return PyUnicode_AsUTF8(f->f_code->co_name);
}

static inline const char *
_get_filename_default(const char *name, time_t started)
{
    const char *suffix = ".trace";
    ssize_t time_width = _count_digits((long long)started);
    if (time_width < 6) {
        time_width = 6;
    }
    char *filename = (char *)PyMem_RawMalloc(
            strlen(name) + time_width + strlen(suffix) + 1);
    if (filename == NULL) {
        return "???";
    }
    sprintf(filename, "%s-%06ld%s", name, started, suffix);
    // XXX The memory will need to be freed.
    return filename;
}

static const char *
_get_filename(const char *name, time_t started)
{
    // XXX Look up an optional env var?
    return _get_filename_default(name, started);
}

#define MAX_LOG_LINES 40
#define MAX_LOG_LINE_LEN 40
#define MAX_LOG_LEN (MAX_LOG_LINES * MAX_LOG_LINE_LEN)
static char _log[MAX_LOG_LEN];
static int _log_bytes_written = 0;

static inline char *
_get_next_logline(void)
{
    return &_log[_log_bytes_written];
}

static inline void
_log_event(FILE *logfile, _PyPerf_Event event)
{
    struct timespec time = _clock_now();
    char *buf = _get_next_logline();
    sprintf(buf, "%ld.%ld %d\n", time.tv_sec, time.tv_nsec, (int)event);
    _log_bytes_written += strlen(buf);
}

static inline void
_log_event_with_data(FILE *logfile, _PyPerf_Event event, int data)
{
    struct timespec time = _clock_now();
    char *buf = _get_next_logline();
    sprintf(buf, "%ld.%ld %d %d\n", time.tv_sec, time.tv_nsec, (int)event, data);
    _log_bytes_written += strlen(buf);
}

static inline void
_log_info(FILE *logfile, const char *label, const char *text)
{
    char *buf = _get_next_logline();
    sprintf(buf, "# %s: %s\n", label, text);
    _log_bytes_written += strlen(buf);
}

static inline void
_log_info_amount(FILE *logfile, const char *label, long value, const char *units)
{
    char *buf = _get_next_logline();
    sprintf(buf, "# %s: %ld %s\n", label, value, units);
    _log_bytes_written += strlen(buf);
}

static inline void
_log_info_clock(FILE *logfile, const char *label, struct timespec ts)
{
    char *buf = _get_next_logline();
    long nsec = ts.tv_nsec;
    while (nsec < 10000000) {
        nsec *= 10;
    }
    sprintf(buf, "# %s: %ld.%ld s (on clock)\n", label, ts.tv_sec, nsec);
    _log_bytes_written += strlen(buf);
}

static inline void
_flush_log(FILE *logfile, int record)
{
    struct timespec before = _clock_now();
    fprintf(logfile, "%s", _log);
    _log_bytes_written = 0;
    struct timespec after = _clock_now();
    struct timespec elapsed = timespec_sub(after, before);

    if (record) {
        char *buf = _get_next_logline();
        sprintf(buf, "# log written: %ld.%09ld s\n", elapsed.tv_sec, elapsed.tv_nsec);
        _log_bytes_written += strlen(buf);
    }
}

static inline void
_flush_log_if_full(FILE *logfile)
{
    if (_log_bytes_written + MAX_LOG_LINE_LEN > MAX_LOG_LEN) {
        _flush_log(logfile, 1);
    }
}

static FILE * _trace_file = NULL;

//======================
// the public API
//======================

void
_PyPerf_Trace(_PyPerf_Event event)
{
    if (_trace_file) {
        _log_event(_trace_file, event);
        _flush_log_if_full(_trace_file);
    }
}

void
_PyPerf_TraceToFile(_PyPerf_Event event)
{
    if (_trace_file) {
        _log_event(_trace_file, event);
        _flush_log(_trace_file, 1);
    }
}

void
_PyPerf_TraceOp(int op)
{
    if (_trace_file) {
        _log_event_with_data(_trace_file, CEVAL_OP, op);
        _flush_log_if_full(_trace_file);
    }
}

void
_PyPerf_TraceFrameEnter(PyFrameObject *f)
{
    if (_trace_file) {
        const char *funcname = _get_frame_name(f);
        _log_info(_trace_file, "func", funcname);
        _flush_log_if_full(_trace_file);
        // XXX Differentiate generators?
        _log_event(_trace_file, CEVAL_ENTER);
        _flush_log_if_full(_trace_file);
    }
}

void
_PyPerf_TraceFrameExit(PyFrameObject *f)
{
    if (_trace_file) {
        const char *funcname = _get_frame_name(f);
        _log_info(_trace_file, "func", funcname);
        _flush_log_if_full(_trace_file);
        // XXX Differentiate generators?
        _log_event(_trace_file, CEVAL_EXIT);
        _flush_log_if_full(_trace_file);
    }
}

static long _endtime_pos = -1;

void
_PyPerf_TraceInit(_PyArgv *args)
{
    time_t started = time(NULL);
    struct timespec started_clock = _clock_now();

    const char *filename = _get_filename("eval_loop", started);
    _trace_file = fopen(filename, "w");
    assert(_trace_file != NULL);

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
    _log_info_amount(_trace_file, "start time", started, "s (since epoch)");
    _flush_log(_trace_file, 0);
    _log_info_clock(_trace_file, "start clock", started_clock);
    _flush_log(_trace_file, 0);
    // We will fill in the end time (12 digits) when we are done.
    _endtime_pos = ftell(_trace_file);
    _log_info_clock(_trace_file, "end clock", started_clock);
    _flush_log(_trace_file, 0);
    fprintf(_trace_file, "\n");  // Add the end-of-header marker (a blank line).

    // Log the first event.
    _log_event(_trace_file, MAIN_INIT);
    _flush_log(_trace_file, 0);
}

void
_PyPerf_TraceFini(void)
{
    if (_trace_file == NULL) {
        return;
    }

    // Log the very last event.
    _log_event(_trace_file, MAIN_FINI);
    _flush_log(_trace_file, 0);

    // Update the header.
    if (_endtime_pos >= 0) {
        fseek(_trace_file, _endtime_pos, SEEK_SET);
        _endtime_pos = 0;
        struct timespec ended_clock = _clock_now();
        _log_info_clock(_trace_file, "end clock", ended_clock);
        _flush_log(_trace_file, 0);
    }

    fclose(_trace_file);
    _trace_file = NULL;
}
