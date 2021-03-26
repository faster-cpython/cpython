#ifndef Py_INTERNAL_PERF_H
#define Py_INTERNAL_PERF_H
#ifdef __cplusplus
extern "C" {
#endif

#ifndef Py_BUILD_CORE
#  error "this header requires Py_BUILD_CORE define"
#endif

#include "pycore_initconfig.h"  // _PyArgv

typedef enum _PyPerf_Event {
    MAIN_INIT,
    MAIN_FINI,
    CEVAL_ENTER,
    CEVAL_EXIT,
    CEVAL_LOOP_ENTER,
    CEVAL_LOOP_EXIT,
    CEVAL_LOOP_EXCEPTION,
    CEVAL_LOOP_ERROR,
    CEVAL_OP,
} _PyPerf_Event;

extern void _PyPerf_Trace(_PyPerf_Event);
extern void _PyPerf_TraceOp(int op);
extern void _PyPerf_TraceFrameEnter(PyFrameObject *);
extern void _PyPerf_TraceFrameExit(PyFrameObject *);
extern void _PyPerf_TraceInit(_PyArgv *);
extern void _PyPerf_TraceFini(void);

#ifdef __cplusplus
}
#endif
#endif /* !Py_INTERNAL_PERF*/
