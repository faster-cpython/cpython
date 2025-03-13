#ifndef Py_INTERNAL_OBJECT_ALLOC_H
#define Py_INTERNAL_OBJECT_ALLOC_H

#include "pycore_object.h"      // _PyType_HasFeature()
#include "pycore_pystate.h"     // _PyThreadState_GET()
#include "pycore_tstate.h"      // _PyThreadStateImpl
#include "pycore_freelist.h"     // _PyFreeList_Pop()

#ifdef __cplusplus
extern "C" {
#endif

#ifndef Py_BUILD_CORE
#  error "this header requires Py_BUILD_CORE define"
#endif

#ifdef Py_GIL_DISABLED
static inline mi_heap_t *
_PyObject_GetAllocationHeap(_PyThreadStateImpl *tstate, PyTypeObject *tp)
{
    struct _mimalloc_thread_state *m = &tstate->mimalloc;
    if (_PyType_HasFeature(tp, Py_TPFLAGS_PREHEADER)) {
        return &m->heaps[_Py_MIMALLOC_HEAP_GC_PRE];
    }
    else if (_PyType_IS_GC(tp)) {
        return &m->heaps[_Py_MIMALLOC_HEAP_GC];
    }
    else {
        return &m->heaps[_Py_MIMALLOC_HEAP_OBJECT];
    }
}
#endif

// Sets the heap used for PyObject_Malloc(), PyObject_Realloc(), etc. calls in
// Py_GIL_DISABLED builds. We use different heaps depending on if the object
// supports GC and if it has a pre-header. We smuggle the choice of heap
// through the _mimalloc_thread_state. In the default build, this simply
// calls PyObject_Malloc().
static inline void *
_PyObject_MallocWithType(PyTypeObject *tp, size_t size)
{
#ifdef Py_GIL_DISABLED
    _PyThreadStateImpl *tstate = (_PyThreadStateImpl *)_PyThreadState_GET();
    struct _mimalloc_thread_state *m = &tstate->mimalloc;
    m->current_object_heap = _PyObject_GetAllocationHeap(tstate, tp);
#endif
    void *mem = PyObject_Malloc(size);
#ifdef Py_GIL_DISABLED
    m->current_object_heap = &m->heaps[_Py_MIMALLOC_HEAP_OBJECT];
#endif
    return mem;
}

static inline void *
_PyObject_ReallocWithType(PyTypeObject *tp, void *ptr, size_t size)
{
#ifdef Py_GIL_DISABLED
    _PyThreadStateImpl *tstate = (_PyThreadStateImpl *)_PyThreadState_GET();
    struct _mimalloc_thread_state *m = &tstate->mimalloc;
    m->current_object_heap = _PyObject_GetAllocationHeap(tstate, tp);
#endif
    void *mem = PyObject_Realloc(ptr, size);
#ifdef Py_GIL_DISABLED
    m->current_object_heap = &m->heaps[_Py_MIMALLOC_HEAP_OBJECT];
#endif
    return mem;
}

static inline PyObject *
_PyObject_NewTstate(PyThreadState *ts, PyTypeObject *tp, size_t presize, size_t size)
{
    size += presize;
    assert(size > 0);
    if (size <= SMALL_REQUEST_THRESHOLD) {
        int size_cls = (size - 1) >> ALIGNMENT_SHIFT;
        struct _Py_freelist *fl = &ts->interp->object_state.freelists.by_size[size_cls];
        char *mem = _PyFreeList_Pop(fl);
        PyObject *op = (PyObject *)
        if (mem != NULL) {
            PyObject *op = (PyObject *)(mem + presize);
            Py_SET_TYPE(op, tp);
            op->ob_refcnt = 1;
            return op;
        }
    }
    return ts->interp->alloc(tp, presize, size);
    char *mem = PyObject_Malloc(size);
    if (mem == NULL) {
        return PyErr_NoMemory();
    }
    PyObject *op = (PyObject *)(PyObject *)(mem + presize);
    _PyObject_Init(op, tp);
    return op;
}

static inline void
_PyMem_FreeTstate(PyThreadState *ts, PyObject *obj, size_t presize, size_t size)
{
    size += presize;
    assert(size > 0);
    char *mem = ((char *)obj) - presize;
    if (size <= SMALL_REQUEST_THRESHOLD) {
        int size_cls = (size - 1) >> ALIGNMENT_SHIFT;
        struct _Py_freelist *fl = &ts->interp->object_state.freelists.by_size[size_cls];
        if (_PyFreeList_Push(fl, mem)) {
            return;
        }
    }
    OBJECT_STAT_INC(frees);
    ts->interp->free(obj, presize);
    _PyRuntime.allocators.standard.obj.free(_PyRuntime.allocators.standard.obj.ctx, mem);
}

static inline void
_PyObject_FreeTstate(PyThreadState *ts, PyObject *obj, size_t size)
{
    assert(!PyObject_IS_GC(obj));
    _PyMem_FreeTstate(ts, obj, 0, size);
}

#ifdef __cplusplus
}
#endif
#endif  // !Py_INTERNAL_OBJECT_ALLOC_H
