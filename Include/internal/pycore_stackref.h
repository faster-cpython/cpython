#ifndef Py_INTERNAL_STACKREF_H
#define Py_INTERNAL_STACKREF_H
#ifdef __cplusplus
extern "C" {
#endif

#ifndef Py_BUILD_CORE
#  error "this header requires Py_BUILD_CORE define"
#endif

#include "pycore_object_deferred.h"

#include <stddef.h>
#include <stdbool.h>

/*
  This file introduces a new API for handling references on the stack, called
  _PyStackRef. This API is inspired by HPy.

  There are 3 main operations, that convert _PyStackRef to PyObject* and
  vice versa:

    1. Borrow (discouraged)
    2. Steal
    3. New

  Borrow means that the reference is converted without any change in ownership.
  This is discouraged because it makes verification much harder. It also makes
  unboxed integers harder in the future.

  Steal means that ownership is transferred to something else. The total
  number of references to the object stays the same.

  New creates a new reference from the old reference. The old reference
  is still valid.

  With these 3 API, a strict stack discipline must be maintained. All
  _PyStackRef must be operated on by the new reference operations:

    1. DUP
    2. CLOSE

   DUP is roughly equivalent to Py_NewRef. It creates a new reference from an old
   reference. The old reference remains unchanged.

   CLOSE is roughly equivalent to Py_DECREF. It destroys a reference.

   Note that it is unsafe to borrow a _PyStackRef and then do normal
   CPython refcounting operations on it!
*/

typedef union _PyStackRef {
    uintptr_t bits;
} _PyStackRef;


#ifdef Py_GIL_DISABLED

#define Py_TAG_DEFERRED (1)

#define Py_TAG_PTR      ((uintptr_t)0)
#define Py_TAG_BITS     ((uintptr_t)1)


static const _PyStackRef PyStackRef_NULL = { .bits = Py_TAG_DEFERRED};
#define PyStackRef_IsNull(stackref) ((stackref).bits == PyStackRef_NULL.bits)
#define PyStackRef_True ((_PyStackRef){.bits = ((uintptr_t)&_Py_TrueStruct) | Py_TAG_DEFERRED })
#define PyStackRef_False ((_PyStackRef){.bits = ((uintptr_t)&_Py_FalseStruct) | Py_TAG_DEFERRED })
#define PyStackRef_None ((_PyStackRef){.bits = ((uintptr_t)&_Py_NoneStruct) | Py_TAG_DEFERRED })

static inline PyObject *
PyStackRef_AsPyObjectBorrow(_PyStackRef stackref)
{
    PyObject *cleared = ((PyObject *)((stackref).bits & (~Py_TAG_BITS)));
    return cleared;
}

#define PyStackRef_IsDeferred(ref) (((ref).bits & Py_TAG_BITS) == Py_TAG_DEFERRED)

static inline PyObject *
PyStackRef_NotDeferred_AsPyObject(_PyStackRef stackref)
{
    assert(!PyStackRef_IsDeferred(stackref));
    return (PyObject *)stackref.bits;
}

static inline PyObject *
PyStackRef_AsPyObjectSteal(_PyStackRef stackref)
{
    assert(!PyStackRef_IsNull(stackref));
    if (PyStackRef_IsDeferred(stackref)) {
        return Py_NewRef(PyStackRef_AsPyObjectBorrow(stackref));
    }
    return PyStackRef_AsPyObjectBorrow(stackref);
}

static inline _PyStackRef
_PyStackRef_FromPyObjectSteal(PyObject *obj)
{
    assert(obj != NULL);
    // Make sure we don't take an already tagged value.
    assert(((uintptr_t)obj & Py_TAG_BITS) == 0);
    unsigned int tag = _Py_IsImmortal(obj) ? (Py_TAG_DEFERRED) : Py_TAG_PTR;
    return ((_PyStackRef){.bits = ((uintptr_t)(obj)) | tag});
}
#   define PyStackRef_FromPyObjectSteal(obj) _PyStackRef_FromPyObjectSteal(_PyObject_CAST(obj))

static inline _PyStackRef
PyStackRef_FromPyObjectNew(PyObject *obj)
{
    // Make sure we don't take an already tagged value.
    assert(((uintptr_t)obj & Py_TAG_BITS) == 0);
    assert(obj != NULL);
    if (_Py_IsImmortal(obj) || _PyObject_HasDeferredRefcount(obj)) {
        return (_PyStackRef){ .bits = (uintptr_t)obj | Py_TAG_DEFERRED };
    }
    else {
        return (_PyStackRef){ .bits = (uintptr_t)(Py_NewRef(obj)) | Py_TAG_PTR };
    }
}
#define PyStackRef_FromPyObjectNew(obj) PyStackRef_FromPyObjectNew(_PyObject_CAST(obj))

static inline _PyStackRef
PyStackRef_FromPyObjectImmortal(PyObject *obj)
{
    // Make sure we don't take an already tagged value.
    assert(((uintptr_t)obj & Py_TAG_BITS) == 0);
    assert(obj != NULL);
    assert(_Py_IsImmortal(obj));
    return (_PyStackRef){ .bits = (uintptr_t)obj | Py_TAG_DEFERRED };
}
#define PyStackRef_FromPyObjectImmortal(obj) PyStackRef_FromPyObjectImmortal(_PyObject_CAST(obj))

#define PyStackRef_CLOSE(REF)                                        \
        do {                                                            \
            _PyStackRef _close_tmp = (REF);                             \
            assert(!PyStackRef_IsNull(_close_tmp));                     \
            if (!PyStackRef_IsDeferred(_close_tmp)) {                   \
                Py_DECREF(PyStackRef_AsPyObjectBorrow(_close_tmp));     \
            }                                                           \
        } while (0)

static inline _PyStackRef
PyStackRef_DUP(_PyStackRef stackref)
{
    assert(!PyStackRef_IsNull(stackref));
    if (PyStackRef_IsDeferred(stackref)) {
        assert(_Py_IsImmortal(PyStackRef_AsPyObjectBorrow(stackref)) ||
               _PyObject_HasDeferredRefcount(PyStackRef_AsPyObjectBorrow(stackref))
        );
        return stackref;
    }
    Py_INCREF(PyStackRef_AsPyObjectBorrow(stackref));
    return stackref;
}

static inline int
PyStackRef_IsHeapSafe(_PyStackRef ref)
{
    return 1;
}

static inline _PyStackRef
PyStackRef_HeapSafe(_PyStackRef ref)
{
    return ref;
}

// Convert a possibly deferred reference to a strong reference.
static inline _PyStackRef
PyStackRef_AsStrongReference(_PyStackRef stackref)
{
    return PyStackRef_FromPyObjectSteal(PyStackRef_AsPyObjectSteal(stackref));
}

#define PyStackRef_XCLOSE(stackref) \
    do {                            \
        _PyStackRef _tmp = (stackref); \
        if (!PyStackRef_IsNull(_tmp)) { \
            PyStackRef_CLOSE(_tmp); \
        } \
    } while (0);

#define PyStackRef_CLEAR(op) \
    do { \
        _PyStackRef *_tmp_op_ptr = &(op); \
        _PyStackRef _tmp_old_op = (*_tmp_op_ptr); \
        if (!PyStackRef_IsNull(_tmp_old_op)) { \
            *_tmp_op_ptr = PyStackRef_NULL; \
            PyStackRef_CLOSE(_tmp_old_op); \
        } \
    } while (0)

#else // Py_GIL_DISABLED

// With GIL

#define Py_TAG_BITS 1
#define Py_TAG_REFCNT 1
#define BITS_TO_PTR(REF) ((PyObject *)((REF).bits))
#define BITS_TO_PTR_MASKED(REF) ((PyObject *)(((REF).bits) & (~Py_TAG_BITS)))

#define PyStackRef_NULL_BITS Py_TAG_REFCNT
static const _PyStackRef PyStackRef_NULL = { .bits = PyStackRef_NULL_BITS };

#define PyStackRef_IsNull(ref) ((ref).bits == PyStackRef_NULL_BITS)
#define PyStackRef_True ((_PyStackRef){.bits = ((uintptr_t)&_Py_TrueStruct) | Py_TAG_REFCNT })
#define PyStackRef_False ((_PyStackRef){.bits = ((uintptr_t)&_Py_FalseStruct) | Py_TAG_REFCNT })
#define PyStackRef_None ((_PyStackRef){.bits = ((uintptr_t)&_Py_NoneStruct) | Py_TAG_REFCNT })

static inline int
PyStackRef_HasCount(_PyStackRef ref)
{
    return ref.bits & Py_TAG_REFCNT;
}

static inline PyObject *
PyStackRef_AsPyObjectBorrow(_PyStackRef ref)
{
    return BITS_TO_PTR_MASKED(ref);
}

static inline PyObject *
PyStackRef_AsPyObjectSteal(_PyStackRef ref)
{
    if (PyStackRef_HasCount(ref)) {
        PyObject *obj = BITS_TO_PTR_MASKED(ref);
        assert(Py_REFCNT(obj) > 0);
        return Py_NewRef(obj);
    }
    else {
        return BITS_TO_PTR(ref);
    }
}

/* We will want to extend this to a larger set of objects in the future */
#define _Py_IsDeferrable _Py_IsImmortal

static inline _PyStackRef
PyStackRef_FromPyObjectSteal(PyObject *obj)
{
    assert(obj != NULL);
    unsigned int tag = _Py_IsDeferrable(obj) ? Py_TAG_REFCNT : 0;
    _PyStackRef ref = ((_PyStackRef){.bits = ((uintptr_t)(obj)) | tag});
    return ref;
}

static inline _PyStackRef
_PyStackRef_FromPyObjectNew(PyObject *obj)
{
    if (_Py_IsDeferrable(obj)) {
        return (_PyStackRef){ .bits = ((uintptr_t)obj) | Py_TAG_REFCNT};
    }
    assert(Py_REFCNT(obj) > 0);
    Py_INCREF(obj);
    _PyStackRef ref = (_PyStackRef){ .bits = (uintptr_t)obj };
    return ref;
}
#define PyStackRef_FromPyObjectNew(obj) _PyStackRef_FromPyObjectNew(_PyObject_CAST(obj))

/* Create a new reference from an object with an embedded reference count */
static inline _PyStackRef
_PyStackRef_FromPyObjectWithCount(PyObject *obj)
{
    assert(Py_REFCNT(obj) > 0);
    return (_PyStackRef){ .bits = (uintptr_t)obj | Py_TAG_REFCNT};
}
#define PyStackRef_FromPyObjectWithCount(obj) _PyStackRef_FromPyObjectWithCount(_PyObject_CAST(obj))

#define PyStackRef_FromPyObjectImmortal PyStackRef_FromPyObjectWithCount


static inline _PyStackRef
PyStackRef_DUP(_PyStackRef ref)
{
    assert(!PyStackRef_IsNull(ref));
    if (!PyStackRef_HasCount(ref)) {
        PyObject *obj = BITS_TO_PTR(ref);
        assert(Py_REFCNT(obj) > 0);
        Py_INCREF_MORTAL(obj);
    }
    return ref;
}

static inline int
PyStackRef_IsHeapSafe(_PyStackRef ref)
{
    return (
        PyStackRef_IsNull(ref) ||
        !PyStackRef_HasCount(ref) ||
        _Py_IsImmortal(PyStackRef_AsPyObjectBorrow(ref))
    );
}

static inline _PyStackRef
PyStackRef_HeapSafe(_PyStackRef ref)
{
    if (PyStackRef_HasCount(ref)) {
        PyObject *obj = BITS_TO_PTR_MASKED(ref);
        if (obj != NULL && !_Py_IsImmortal(obj)) {
            assert(Py_REFCNT(obj) > 0);
            Py_INCREF_MORTAL(obj);
            ref.bits = (uintptr_t)obj;
        }
    }
    return ref;
}

static inline void
PyStackRef_CLOSE(_PyStackRef ref)
{
    assert(!PyStackRef_IsNull(ref));
    if (!PyStackRef_HasCount(ref)) {
        Py_DECREF_MORTAL(BITS_TO_PTR(ref));
    }
}

static inline void PyStackRef_CLOSE_SPECIALIZED(_PyStackRef ref, destructor destruct)
{
    assert(!PyStackRef_IsNull(ref));
    if (!PyStackRef_HasCount(ref)) {
        Py_DECREF_MORTAL_SPECIALIZED(BITS_TO_PTR(ref), destruct);
    }
}

static inline void
PyStackRef_XCLOSE(_PyStackRef ref)
{
    assert(ref.bits != 0);
    if (!PyStackRef_HasCount(ref)) {
        assert(!PyStackRef_IsNull(ref));
        Py_DECREF_MORTAL(BITS_TO_PTR(ref));
    }
}

#define PyStackRef_CLEAR(REF) \
    do { \
        _PyStackRef *_tmp_op_ptr = &(REF); \
        _PyStackRef _tmp_old_op = (*_tmp_op_ptr); \
        *_tmp_op_ptr = PyStackRef_NULL; \
        PyStackRef_XCLOSE(_tmp_old_op); \
    } while (0)

#endif // Py_GIL_DISABLED

// Note: this is a macro because MSVC (Windows) has trouble inlining it.

#define PyStackRef_Is(a, b) (((a).bits & (~Py_TAG_BITS)) == ((b).bits & (~Py_TAG_BITS)))

// Converts a PyStackRef back to a PyObject *, converting the
// stackref to a new reference.
#define PyStackRef_AsPyObjectNew(stackref) Py_NewRef(PyStackRef_AsPyObjectBorrow(stackref))

#define PyStackRef_TYPE(stackref) Py_TYPE(PyStackRef_AsPyObjectBorrow(stackref))



// StackRef type checks

static inline bool
PyStackRef_GenCheck(_PyStackRef stackref)
{
    return PyGen_Check(PyStackRef_AsPyObjectBorrow(stackref));
}

static inline bool
PyStackRef_BoolCheck(_PyStackRef stackref)
{
    return PyBool_Check(PyStackRef_AsPyObjectBorrow(stackref));
}

static inline bool
PyStackRef_LongCheck(_PyStackRef stackref)
{
    return PyLong_Check(PyStackRef_AsPyObjectBorrow(stackref));
}

static inline bool
PyStackRef_ExceptionInstanceCheck(_PyStackRef stackref)
{
    return PyExceptionInstance_Check(PyStackRef_AsPyObjectBorrow(stackref));
}

static inline bool
PyStackRef_CodeCheck(_PyStackRef stackref)
{
    return PyCode_Check(PyStackRef_AsPyObjectBorrow(stackref));
}

static inline bool
PyStackRef_FunctionCheck(_PyStackRef stackref)
{
    return PyFunction_Check(PyStackRef_AsPyObjectBorrow(stackref));
}

#ifdef __cplusplus
}
#endif
#endif /* !Py_INTERNAL_STACKREF_H */
