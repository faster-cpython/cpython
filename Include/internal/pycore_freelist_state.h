#ifndef Py_INTERNAL_FREELIST_STATE_H
#define Py_INTERNAL_FREELIST_STATE_H
#ifdef __cplusplus
extern "C" {
#endif

#ifndef Py_BUILD_CORE
#  error "this header requires Py_BUILD_CORE define"
#endif

#  define PyTuple_MAXSAVESIZE 20     // Largest tuple to save on freelist
#  define Py_tuple_MAXFREELIST 2000  // Maximum number of tuples of each size to save
#  define Py_lists_MAXFREELIST 80
#  define Py_list_iters_MAXFREELIST 10
#  define Py_tuple_iters_MAXFREELIST 10
#  define Py_dicts_MAXFREELIST 80
#  define Py_dictkeys_MAXFREELIST 80
#  define Py_floats_MAXFREELIST 100
#  define Py_ints_MAXFREELIST 100
#  define Py_slices_MAXFREELIST 1
#  define Py_ranges_MAXFREELIST 6
#  define Py_range_iters_MAXFREELIST 6
#  define Py_contexts_MAXFREELIST 255
#  define Py_async_gens_MAXFREELIST 80
#  define Py_async_gen_asends_MAXFREELIST 80
#  define Py_futureiters_MAXFREELIST 255
#  define Py_object_stack_chunks_MAXFREELIST 4
#  define Py_unicode_writers_MAXFREELIST 1
#  define Py_pycfunctionobject_MAXFREELIST 16
#  define Py_pycmethodobject_MAXFREELIST 16
#  define Py_pymethodobjects_MAXFREELIST 20

// A generic freelist of either PyObjects or other data structures.
struct _Py_freelist {
    // Entries are linked together using the first word of the object.
    // For PyObjects, this overlaps with the `ob_refcnt` field or the `ob_tid`
    // field.
    void *freelist;
    // The remaining space in this freelist;
    uint32_t available;
    // The maximum number of items this freelist is allowed to hold
    uint32_t capacity;
    struct _Py_freelist ranges;
    struct _Py_freelist range_iters;
    struct _Py_freelist pycfunctionobject;
    struct _Py_freelist pycmethodobject;
};

#ifdef __cplusplus
}
#endif
#endif /* !Py_INTERNAL_FREELIST_STATE_H */
