#ifndef Py_INTERNAL_PERF_H
#define Py_INTERNAL_PERF_H
#ifdef __cplusplus
extern "C" {
#endif

#ifndef Py_BUILD_CORE
#  error "this header requires Py_BUILD_CORE define"
#endif

extern void _PyPerf_Trace(const char *id);
extern void _PyPerf_TraceOp(int op);
extern void _PyPerf_TraceInit(void);
extern void _PyPerf_TraceFini(void);

#ifdef __cplusplus
}
#endif
#endif /* !Py_INTERNAL_PERF*/
