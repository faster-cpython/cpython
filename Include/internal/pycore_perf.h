#ifndef Py_INTERNAL_PERF_H
#define Py_INTERNAL_PERF_H
#ifdef __cplusplus
extern "C" {
#endif

#ifndef Py_BUILD_CORE
#  error "this header requires Py_BUILD_CORE define"
#endif

#include "pycore_initconfig.h"  // _PyArgv

extern void _PyPerf_Trace(const char *id);
extern void _PyPerf_TraceOp(int op);
extern void _PyPerf_TraceFrameEnter(PyFrameObject *);
extern void _PyPerf_TraceFrameExit(PyFrameObject *);
extern void _PyPerf_TraceInit(_PyArgv *args);
extern void _PyPerf_TraceFini(void);

#ifdef __cplusplus
}
#endif
#endif /* !Py_INTERNAL_PERF*/
