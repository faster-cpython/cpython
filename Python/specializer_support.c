#include "Python.h"
#include "opcode.h"
#include "pycore_object.h"
#include "pycore_uops.h"
#include "pycore_uop_ids.h"
#include "pycore_uop_metadata.h"
#include "pycore_specialize_support.h"

/* If not a constant then the object pointer points to the *type* of the object */
#define IS_CONSTANT 1
/* For special value "not None" */
#define NOT_NONE 2

typedef struct _SpecializerValue SpecializerValue;
typedef struct _SpecializerSpace SpecializerSpace;
typedef struct _SpecializerFrame SpecializerFrame;

/* All references to PyObjects are borrowed,
 * as we are only dealling with constants,
 * types of constants or a few standard types
 */
struct _SpecializerValue {
    uint8_t flags;
    union {
        PyObject *object;
        struct _SpecializerValue *next;
    };
};

struct _SpecializerSpace {
    int size;
    SpecializerValue *free;
    SpecializerValue values[1];
};

static SpecializerValue *
new_value(PyObject *object, int flags, SpecializerSpace *buffer)
{
    assert(buffer->free != NULL);
    SpecializerValue *val = buffer->free;
    buffer->free++;
    assert(val != NULL);
    assert(buffer->free < &buffer->values[buffer->size]);
    val->object = object;
    val->flags = flags;
    return val;
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
    return (o->flags & IS_CONSTANT) != 0;
}

static PyTypeObject *
get_type(SpecializerValue *o)
{
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
new_unknown(SpecializerSpace *buffer)
{
    return new_value(NULL, 0, buffer);
}

static SpecializerValue *
new_from_type(PyTypeObject *t, SpecializerSpace *buffer)
{
    return new_value((PyObject *)t, 0, buffer);
}

static void
promote_to_const(SpecializerValue *o, PyObject *k)
{
    if (o->flags & IS_CONSTANT) {
        assert(o->object == k);
    }
    else {
        if (o->object != NULL) {
            assert(Py_TYPE(k) == (PyTypeObject *)o->object);
        }
    }
    o->flags = IS_CONSTANT;
    o->object = k;
}

static void
promote_to_type(SpecializerValue *o, PyTypeObject *t)
{
    if (o->flags & IS_CONSTANT) {
        assert(Py_TYPE(o->object) == t);
    }
    else {
        if (o->object != NULL) {
            assert((PyTypeObject *)o->object == t);
        }
        else {
            o->object = (PyObject *)t;
        }
    }
}

static int
is_not_none(SpecializerValue *o)
{
    if (o->flags & IS_CONSTANT) {
        return (o->object != Py_None);
    }
    else {
        if (o->object != NULL) {
            return ((PyTypeObject *)o->object != &_PyNone_Type);
        }
        else {
            return (o->flags & NOT_NONE) != 0;
        }
    }
}

static void
promote_to_not_none(SpecializerValue *o)
{
    if (o->flags & IS_CONSTANT) {
        assert(o->object != Py_None);
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

static SpecializerSpace *
initialize_space(void *memory, int size)
{
    SpecializerSpace *space = memory;
    int count = size/sizeof(SpecializerValue) - 1;
    space->size = count;
    space->values[0].next = NULL;
    for (int i = 1; i < count; i++) {
        space->values[i].next = &space->values[i-1];
    }
    space->free = &space->values[count-1];
    return space;
}

static int
guard_bool(_PyUOpInstruction *this_instr, SpecializerValue *flag, PyObject *b)
{
    if (is_constant(flag)) {
        assert(Py_TYPE(get_constant(flag)) == &PyBool_Type);
        this_instr->opcode = _POP_TOP;
        if (get_constant(flag) != b) {
            // Guaranteed failure
            this_instr[1].opcode = _EXIT_TRACE;
            this_instr[1].target = this_instr->target;
        }
        return 1;
    }
    else {
        promote_to_const(flag, Py_True);
        return 0;
    }
}

static int
guard_none(_PyUOpInstruction *this_instr, SpecializerValue *flag, int test_is_none)
{
    if (is_constant(flag)) {
        this_instr->opcode = _POP_TOP;
        int val_is_none = get_constant(flag) == Py_None;
        if (test_is_none != val_is_none) {
            // Guaranteed failure
            this_instr[1].opcode = _EXIT_TRACE;
            this_instr[1].target = this_instr->target;
        }
        return 1;
    }
    else if (is_not_none(flag)) {
        this_instr->opcode = _POP_TOP;
        if (test_is_none) {
            // Guaranteed failure
            this_instr[1].opcode = _EXIT_TRACE;
            this_instr[1].target = this_instr->target;
        }
        return 1;
    }
    else {
        promote_to_const(flag, Py_True);
        return 0;
    }
}

#define UNKNOWN()   new_unknown(space)
#define NULL_VALUE() new_unknown(space)
#define DEBUG_PRINTF(...) ((void)0)

#define REPLACE_OPCODE(OP) \
    do { \
        printf("Changing opcode '%s' to '%s'\n", \
         _PyOpcode_uop_name[this_instr->opcode], _PyOpcode_uop_name[OP]); \
        this_instr->opcode = (OP); \
        modified = 1; \
    } while (0)

#define DECREF(VAL) ((void)(VAL))

int
_Py_Tier2_Specialize(PyCodeObject *code, _PyUOpInstruction *buffer,
           int buffer_size, int curr_stackdepth,
           _PyBloomFilter* dependencies)
{
    /* We cannot have more variables than instructions */
    SpecializerValue memory[buffer_size];
    SpecializerSpace *space = initialize_space(&memory, buffer_size);
    SpecializerValue *stack[32];
    if (code->co_stacksize > 31) {
        return 0;
    }
    for (int i = 0; i < curr_stackdepth; i++) {
        stack[i] = new_unknown(space);
    }
    SpecializerValue **stack_pointer = &stack[curr_stackdepth];
    int modified = 0;
    for (_PyUOpInstruction *this_instr = buffer; ; this_instr++) {
        int oparg = this_instr->oparg;
        switch(this_instr->opcode) {
#include "generated_specializer.c.h"
        }
        assert(this_instr < buffer + buffer_size);
    }
    return modified;
}
