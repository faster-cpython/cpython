#ifndef Py_INTERNAL_OBJECT_STATE_H
#define Py_INTERNAL_OBJECT_STATE_H
#ifdef __cplusplus
extern "C" {
#endif

#ifndef Py_BUILD_CORE
#  error "this header requires Py_BUILD_CORE define"
#endif

#include "pycore_freelist_state.h"  // _Py_freelists
#include "pycore_hashtable.h"       // _Py_hashtable_t
#include "pycore_obmalloc.h"        // NB_SMALL_SIZE_CLASSES


/* Reference tracer state */
struct _reftracer_runtime_state {
    PyRefTracer tracer_func;
    void* tracer_data;
};


struct _py_object_runtime_state {
#ifdef Py_REF_DEBUG
    Py_ssize_t interpreter_leaks;
#endif
    int _not_used;
};

struct _Py_freelists {
    struct _Py_freelist by_size[NB_SMALL_SIZE_CLASSES];
    struct _Py_freelist floats;
    struct _Py_freelist ints;
    struct _Py_freelist tuples[PyTuple_MAXSAVESIZE];
    struct _Py_freelist lists;
    struct _Py_freelist list_iters;
    struct _Py_freelist tuple_iters;
    struct _Py_freelist dicts;
    struct _Py_freelist dictkeys;
    struct _Py_freelist slices;
    struct _Py_freelist contexts;
    struct _Py_freelist async_gens;
    struct _Py_freelist async_gen_asends;
    struct _Py_freelist futureiters;
    struct _Py_freelist object_stack_chunks;
    struct _Py_freelist unicode_writers;
    struct _Py_freelist pymethodobjects;
};

struct _py_object_state {
#if !defined(Py_GIL_DISABLED)
    struct _Py_freelists freelists;
#endif
#ifdef Py_REF_DEBUG
    Py_ssize_t reftotal;
#endif
#ifdef Py_TRACE_REFS
    // Hash table storing all objects. The key is the object pointer
    // (PyObject*) and the value is always the number 1 (as uintptr_t).
    // See _PyRefchain_IsTraced() and _PyRefchain_Trace() functions.
    _Py_hashtable_t *refchain;
#endif
    int _not_used;
};


#ifdef __cplusplus
}
#endif
#endif /* !Py_INTERNAL_OBJECT_STATE_H */
