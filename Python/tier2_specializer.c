#include "Python.h"
#include "pycore_uops.h"
#include "pycore_uop_ids.h"

#define op(name, ...) /* NAME is ignored */

typedef struct _SpecializerValue SpecializerValue;
typedef struct _SpecializerSpace SpecializerSpace;
typedef struct _SpecializerFrame SpecializerFrame;

extern SpecializerValue *new_constant(PyObject *k, SpecializerSpace *space);
extern PyObject *get_constant(SpecializerValue *o);
extern PyTypeObject *get_type(SpecializerValue *o);

extern int is_constant(SpecializerValue *o);
extern int is_int(SpecializerValue *o);
extern int is_bool(SpecializerValue *o);
extern int is_float(SpecializerValue *o);

extern int is_not_none(SpecializerValue *o);
extern int is_none(SpecializerValue *o);

extern SpecializerValue *new_unknown(SpecializerSpace *space);
extern SpecializerValue *new_from_type(PyTypeObject *t, SpecializerSpace *space);

extern void promote_to_const(SpecializerValue *o, PyObject *k);
extern void promote_to_type(SpecializerValue *o, PyTypeObject *t);

extern SpecializerSpace *initialize_space(void *memory, int size);


static void
dummy_func(void) {

    PyCodeObject *code;
    int oparg;
    SpecializerValue *flag;
    SpecializerValue *left;
    SpecializerValue *right;
    SpecializerValue *value;
    SpecializerValue *res;
    SpecializerValue *iter;
    SpecializerValue *top;
    SpecializerValue *bottom;
    SpecializerFrame *frame;
    SpecializerSpace *space;
    _PyUOpInstruction *this_instr;
    _PyBloomFilter *dependencies;
    int modified;

// BEGIN BYTECODES //

    op(_GUARD_BOTH_INT, (left, right -- left, right)) {
        if (is_int(left)) {
            REPLACE_OPCODE(is_int(right) ? _NOP : _GUARD_TOS_INT);
        }
        else if (is_int(right)) {
            REPLACE_OPCODE(_GUARD_NOS_INT);
        }
        promote_to_type(left, &PyLong_Type);
        promote_to_type(right, &PyLong_Type);
    }

    op(_GUARD_BOTH_FLOAT, (left, right -- left, right)) {
        if (is_float(left)) {
            REPLACE_OPCODE(is_float(right) ? _NOP : _GUARD_TOS_FLOAT);
        }
        else if (is_float(right)) {
            REPLACE_OPCODE(_GUARD_NOS_FLOAT);
        }
        promote_to_type(left, &PyFloat_Type);
        promote_to_type(right, &PyFloat_Type);
    }

    op(_LOAD_CONST, (-- value)) {
        PyObject *k = PyTuple_GET_ITEM(code->co_consts, oparg);
        value = new_constant(k, space);
        REPLACE_OPCODE(_Py_IsImmortal(k) ?
            _LOAD_INLINE_IMMORTAL_CONST :
            _LOAD_INLINE_CONST);
        this_instr->operand = (uintptr_t)k;
    }

    op(_TO_BOOL_BOOL, (value -- value)) {
        if (is_bool(value)) {
            REPLACE_OPCODE(_NOP);
        }
        else {
            promote_to_type(value, &PyBool_Type);
        }
    }

    op(_TO_BOOL_NONE, (value -- res)) {
        if (get_constant(value) == Py_None) {
            REPLACE_OPCODE(_POP_TOP);
        }
        res = new_constant(Py_False, space);
    }

    op (_GUARD_IS_TRUE_POP, (flag -- )) {
        modified |= guard_bool(this_instr, flag, Py_True);
    }

    op (_GUARD_IS_FALSE_POP, (flag -- )) {
        modified |= guard_bool(this_instr, flag, Py_False);
    }

    op (_GUARD_IS_NONE_POP, (flag -- )) {
        modified |= guard_none(this_instr, flag, 1);
    }

    op (_GUARD_IS_NOT_NONE_POP, (flag -- )) {
        modified |= guard_none(this_instr, flag, 0);
    }

    op(_BINARY_OP_ADD_INT,  (left, right -- res)) {
        res = new_from_type(&PyLong_Type, space);
    }

    op(_BINARY_OP_SUBTRACT_INT,  (left, right -- res)) {
        res = new_from_type(&PyLong_Type, space);
    }

    op(_BINARY_OP_MULTIPLY_INT,  (left, right -- res)) {
        res = new_from_type(&PyLong_Type, space);
    }

    op(_BINARY_OP_ADD_FLOAT,  (left, right -- res)) {
        res = new_from_type(&PyFloat_Type, space);
    }

    op(_BINARY_OP_SUBTRACT_FLOAT,  (left, right -- res)) {
        res = new_from_type(&PyFloat_Type, space);
    }

    op(_BINARY_OP_MULTIPLY_FLOAT,  (left, right -- res)) {
        res = new_from_type(&PyFloat_Type, space);
    }


    op(_COPY, (bottom, unused[oparg-1] -- bottom, unused[oparg-1], top)) {
        assert(oparg > 0);
        top = bottom;
    }

    op(_SWAP, (bottom, unused[oparg-2], top --
                    top, unused[oparg-2], bottom)) {
    }

    op(_JUMP_TO_TOP, (--)) {
        return modified;
    }

    op(_EXIT_TRACE, (--)) {
        return modified;
    }

    // Because this has type annotations we need to override it
    op(_INIT_CALL_PY_EXACT_ARGS, (callable, self_or_null, args[oparg] -- new_frame)) {
        (void)callable;
        (void)self_or_null;
        (void)args;
        return modified;
    }

    op(_POP_FRAME, (retval --)) {
        (void)retval;
        return modified;
    }

    op(_PUSH_FRAME, (new_frame -- unused if (0))) {
        (void)new_frame;
        return modified;
    }


// END BYTECODES //

}

