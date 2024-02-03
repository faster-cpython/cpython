#include "Python.h"
#include "opcode.h"
#include "pycore_dict.h"
#include "pycore_object.h"
#include "pycore_uop_ids.h"
#include "pycore_uop_metadata.h"
#include "pycore_specialize_support.h"

/* If not a constant then the object pointer points to the *type* of the object */
#define IS_CONSTANT 1
/* For special value "not None" */
#define NOT_NONE 2
/* For special value "not NULL" */
#define NOT_NULL 4
/* For special value top */
#define TOP 8

typedef struct _SpecializerValue SpecializerValue;
typedef struct _SpecializerSpace SpecializerSpace;
typedef struct _SpecializerFrame SpecializerFrame;

/* All references to PyObjects are borrowed,
 * as we are only dealling with constants,
 * types of constants or a few standard types
 */
struct _SpecializerValue {
    uint8_t flags;
    PyObject *object;
};

struct _SpecializerFrame {
    struct _SpecializerFrame *previous;
    PyCodeObject *code;
    int stack_top;
    int nlocals;
    SpecializerValue *localsplus[1];
};

struct _SpecializerSpace {
    char *base;
    int size;
    char *allocated_values;
    char *stack_pointer;
};

static SpecializerValue *
new_value(PyObject *object, int flags, SpecializerSpace *space)
{
    char *ptr = space->allocated_values - sizeof(SpecializerValue);
    if (ptr <= space->stack_pointer) {
        return NULL;
    }
    space->allocated_values = ptr;
    SpecializerValue *res = (SpecializerValue *)ptr;
    res->object = object;
    res->flags = flags;
    return res;
}

static SpecializerValue *
new_unknown(SpecializerSpace *buffer)
{
    return new_value(NULL, 0, buffer);
}

static SpecializerValue *
new_null(SpecializerSpace *buffer)
{
    return new_value(NULL, IS_CONSTANT, buffer);
}

static int
is_null(SpecializerValue *o)
{
    return o->flags == IS_CONSTANT && o->object == NULL;
}

static int
is_not_null(SpecializerValue *o)
{
    return o->flags != IS_CONSTANT || o->object != NULL;
}

static void
to_top(SpecializerValue *o)
{
    o->flags = TOP;
    o->object = NULL;
}

static void
promote_to_not_null(SpecializerValue *o)
{
    if (is_null(o)) {
        to_top(o);
    }
    if (o->object == NULL) {
        if (o->flags == 0) {
            o->flags = NOT_NULL;
        }
    }
}

static void
promote_to_null(SpecializerValue *o)
{
    if (is_not_null(o)) {
        to_top(o);
    }
    if (o->object == NULL) {
        if (o->flags == 0) {
            o->flags = IS_CONSTANT;
        }
    }
}

static SpecializerFrame *
make_frame(PyCodeObject *code, SpecializerSpace *space, int unknowns)
{
    int nlocalsplus = code->co_nlocals + code->co_stacksize;
    size_t size = sizeof(SpecializerFrame) + sizeof(SpecializerValue *) * nlocalsplus;
    if (space->stack_pointer + size >= space->allocated_values) {
        return NULL;
    }
    SpecializerFrame *frame = (SpecializerFrame *)space->stack_pointer;
    frame->previous = NULL;
    frame->code = code;
    space->stack_pointer += size;
    frame->stack_top = code->co_nlocals;
    for (int i = 0; i < unknowns; i++) {
        SpecializerValue *val = new_unknown(space);
        if (val == NULL) {
            return NULL;
        }
        frame->localsplus[i] = val;
    }
    return frame;
}

static SpecializerFrame *
pop_frame(SpecializerFrame *frame, SpecializerSpace *space)
{
    assert(frame->previous != NULL);
    space->stack_pointer = (char *)frame;
    return frame->previous;
}

SpecializerValue **
get_stack_pointer(SpecializerFrame *frame)
{
    return frame->localsplus + frame->stack_top;
}

void
set_stack_pointer(SpecializerFrame *frame, SpecializerValue **sp)
{
    frame->stack_top = sp - frame->localsplus;
}

SpecializerValue *
get_local(SpecializerFrame *frame, int index)
{
    return frame->localsplus[index];
}

void
set_local(SpecializerFrame *frame, int index, SpecializerValue *value)
{
    frame->localsplus[index] = value;
}

static SpecializerValue *
new_constant(PyObject *k, SpecializerSpace *buffer)
{
    return new_value(k,  IS_CONSTANT, buffer);
}

static PyObject *
get_constant(SpecializerValue *o)
{
    if (o->flags & IS_CONSTANT) {
        return o->object;
    }
    else {
        return NULL;
    }
}

static int
is_constant(SpecializerValue *o)
{
    return (o->flags & IS_CONSTANT) != 0 && o->object != NULL;
}

static PyTypeObject *
get_type(SpecializerValue *o)
{
    if (o->flags & (NOT_NULL | NOT_NONE | TOP)) {
        return NULL;
    }
    if (o->flags & IS_CONSTANT) {
        return Py_TYPE(o->object);
    }
    else {
        assert(o->object == NULL || PyType_Check(o->object));
        return (PyTypeObject *)o->object;
    }
}

static int
is_int(SpecializerValue *o)
{
    return get_type(o) == &PyLong_Type;
}

static int
is_bool(SpecializerValue *o)
{
    return get_type(o) == &PyBool_Type;
}

static int
is_float(SpecializerValue *o)
{
    return get_type(o) == &PyFloat_Type;
}

static SpecializerValue *
new_from_type(PyTypeObject *t, SpecializerSpace *buffer)
{
    return new_value((PyObject *)t, 0, buffer);
}

static void
promote_to_const(SpecializerValue *o, PyObject *k)
{
    if (o->flags & TOP) {
        return;
    }
    if (o->flags & IS_CONSTANT) {
        if (o->object != k) {
            to_top(o);
            return;
        }
    }
    else {
        if (o->object != NULL) {
            if (Py_TYPE(k) != (PyTypeObject *)o->object) {
                to_top(o);
                return;
            }
        }
        o->flags = IS_CONSTANT;
    }
    o->object = k;
}

static void
promote_to_type(SpecializerValue *o, PyTypeObject *t)
{
    if (o->flags & TOP) {
        return;
    }
    if (o->flags & IS_CONSTANT) {
        if (Py_TYPE(o->object) != t) {
            to_top(o);
            return;
        }
    }
    else {
        if (o->object != NULL) {
            if (t != (PyTypeObject *)o->object) {
                to_top(o);
                return;
            }
        }
        else {
            o->object = (PyObject *)t;
        }
    }
}

static int
is_none(SpecializerValue *o)
{
    if (o->flags & TOP) {
        return 0;
    }
    if (o->flags & IS_CONSTANT) {
        return (o->object == Py_None);
    }
    return 0;
}

static int
is_not_none(SpecializerValue *o)
{
    if (o->flags & TOP) {
        return 0;
    }
    if (o->flags & IS_CONSTANT) {
        return (o->object != Py_None);
    }
    else {
        return (o->flags & NOT_NONE) != 0;
    }
}

static void
promote_to_not_none(SpecializerValue *o)
{
    if (o->flags & TOP) {
        return;
    }
    if (o->flags & IS_CONSTANT) {
        if (o->object != Py_None) {
            to_top(o);
        }
    }
    else {
        if (o->object != NULL) {
            assert((PyTypeObject *)o->object != &_PyNone_Type);
        }
        else {
            o->flags = NOT_NONE;
        }
    }
}

static void
initialize_space(SpecializerSpace *space, char *memory, int size)
{
    assert(((size_t)memory) % sizeof(long double) == 0);
    assert(size % sizeof(long double) == 0);
    space->base = memory;
    space->size = size;
    space->stack_pointer = memory;
    space->allocated_values = memory + size;
}

static int
guard_bool(_PyUOpInstruction *this_instr, SpecializerValue *flag, PyObject *b)
{
    if (is_constant(flag)) {
        assert(Py_TYPE(get_constant(flag)) == &PyBool_Type);
        if (get_constant(flag) == b) {
            this_instr->opcode = _POP_TOP;
        }
        else {
            // Guaranteed failure
            this_instr[1].opcode = _EXIT_TRACE;
            this_instr[1].target = this_instr->target;
        }
        return 1;
    }
    else {
        promote_to_const(flag, b);
        return 0;
    }
}

static int
guard_none(_PyUOpInstruction *this_instr, SpecializerValue *flag, int test_is_none)
{
    int outcome = 0; // 1 guaranteed success, -1 guaranteed failure
    if (is_not_none(flag)) {
        outcome = test_is_none ? -1 : 1;
    }
    else if (is_none(flag)) {
        outcome = test_is_none ? 1 : -1;
    }
    if (test_is_none) {
        promote_to_const(flag, Py_None);
    }
    else {
        promote_to_not_none(flag);
    }
    if (outcome == 1) {
        this_instr->opcode = _POP_TOP;
        return 1;
    }
    if (outcome == -1) {
        // Guaranteed failure
        this_instr->opcode = _EXIT_TRACE;
        return 1;
    }
    return 0;
}

#define UNKNOWN()   new_unknown(space)
#define NULL_VALUE() new_null(space)
#define DEBUG_PRINTF(...) ((void)0)

#define REPLACE_OPCODE(OP) \
    do { \
        this_instr->opcode = (OP); \
        modified = 1; \
    } while (0)

#define DECREF(VAL) ((void)(VAL))

/* Copied from optimizer analysis. TO DO -- DRY. */
static void
global_to_const(_PyUOpInstruction *inst, PyObject *val)
{
    if (val == NULL) {
        return;
    }
    if (_Py_IsImmortal(val)) {
        inst->opcode = (inst->oparg & 1) ? _LOAD_CONST_INLINE_BORROW_WITH_NULL : _LOAD_CONST_INLINE_BORROW;
    }
    else {
        inst->opcode = (inst->oparg & 1) ? _LOAD_CONST_INLINE_WITH_NULL : _LOAD_CONST_INLINE;
    }
    inst->operand = (uint64_t)val;
}


static int
type_callback(PyTypeObject * t)
{
    _Py_Executors_InvalidateDependency(_PyInterpreterState_GET(), t);
    return 0;
}

#define TYPE_WATCHER_ID 0

int
_Py_Tier2_Specialize(PyCodeObject *code, _PyUOpInstruction *buffer,
           int buffer_size, int curr_stackdepth,
           _PyBloomFilter* dependencies)
{
    /* Highly unlikely to have more variables than instructions */
    char memory[buffer_size * sizeof(SpecializerValue)];
    SpecializerSpace allocated_space;
    SpecializerSpace *space = &allocated_space;
    initialize_space(space, memory, buffer_size * sizeof(SpecializerValue));
    SpecializerFrame *frame = make_frame(code, space, code->co_nlocals + curr_stackdepth);
    if (frame == NULL) {
        return 0;
    }
    _PyInterpreterState_GET()->type_watchers[TYPE_WATCHER_ID] = type_callback;
    SpecializerValue **stack_pointer = get_stack_pointer(frame) + curr_stackdepth;
    int modified = 0;
    for (_PyUOpInstruction *this_instr = buffer; ; this_instr++) {
        int oparg = this_instr->oparg;
        switch(this_instr->opcode) {
#include "generated_specializer.c.h"
        }
        assert(this_instr < buffer + buffer_size);
    }
fail: // "fail" just means we run out of space, not that there was an error.
    return modified;
}
