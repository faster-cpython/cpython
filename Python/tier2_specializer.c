#include "Python.h"
#include "pycore_uops.h"
#include "pycore_uop_ids.h"

#define op(name, ...) /* NAME is ignored */

typedef struct _SpecializerValue SpecializerValue;
typedef struct _SpecializerSpace SpecializerSpace;
typedef struct _SpecializerFrame SpecializerFrame;

extern SpecializerValue *get_local(SpecializerFrame *frame, int index);
extern void set_local(SpecializerFrame *frame, int index, SpecializerValue *value);

extern SpecializerValue *new_constant(PyObject *k, SpecializerSpace *space);
extern SpecializerValue *new_null(SpecializerSpace *space);
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


static int
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

    op(_LOAD_FAST, (-- value)) {
        value = get_local(frame, oparg);
        if (value == NULL) goto fail;
        promote_to_not_null(value);
    }

    op(_STORE_FAST, (value --)) {
        set_local(frame, oparg, value);
    }

    op(_PUSH_NULL, (-- res)) {
        res = new_null(space);
        if (res == NULL) goto fail;
    }

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
        if (value == NULL) {
            goto fail;
        }
    }

    op(_LOAD_CONST_INLINE, (ptr/4 -- value)) {
        PyObject *k = ptr;
        value = new_constant(k, space);
        if (value == NULL) goto fail;
    }

    op(_LOAD_CONST_INLINE_BORROW, (ptr/4 -- value)) {

        PyObject *k = ptr;
        value = new_constant(k, space);
        if (value == NULL) goto fail;
    }

    op(_LOAD_CONST_INLINE_WITH_NULL, (ptr/4 -- value, null)) {
        PyObject *k = ptr;
        value = new_constant(k, space);
        if (value == NULL) goto fail;
        null = new_null(space);
        if (null == NULL) goto fail;
    }

    op(_LOAD_CONST_INLINE_BORROW_WITH_NULL, (ptr/4 -- value, null)) {
        PyObject *k = ptr;
        value = new_constant(k, space);
        if (value == NULL) goto fail;
        null = new_null(space);
        if (null == NULL) goto fail;
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
        promote_to_const(value, Py_None);
        res = new_constant(Py_False, space);
        if (res == NULL) goto fail;
    }

    op(_TO_BOOL_ALWAYS_TRUE, (value -- res)) {
        res = new_constant(Py_True, space);
        if (res == NULL) goto fail;
    }

    op(_TO_BOOL_LIST, (value -- res)) {
        promote_to_type(value, &PyList_Type);
        res = new_from_type(&PyBool_Type, space);
        if (res == NULL) goto fail;
    }

    op(_TO_BOOL_STR, (value -- res)) {
        promote_to_type(value, &PyUnicode_Type);
        res = new_from_type(&PyBool_Type, space);
        if (res == NULL) goto fail;
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

    op(_IS_OP, (left, right -- b)) {
        if (is_constant(left) && is_constant(right)) {
            PyObject *o = get_constant(left) == get_constant(right) ? Py_True : Py_False;
            b = new_constant(o, space);
        }
        else {
            b = new_from_type(&PyBool_Type, space);
        }
        if (b == NULL) goto fail;
    }

    op(_COMPARE_OP_FLOAT, (left, right -- res)) {
        promote_to_type(left, &PyFloat_Type);
        promote_to_type(right, &PyFloat_Type);
        res = new_from_type(&PyBool_Type, space);
        if (res == NULL) goto fail;
    }

    op(_COMPARE_OP_INT, (left, right -- res)) {
        promote_to_type(left, &PyLong_Type);
        promote_to_type(right, &PyLong_Type);
        res = new_from_type(&PyBool_Type, space);
        if (res == NULL) goto fail;
    }

    op(_COMPARE_OP_STR, (left, right -- res)) {
        promote_to_type(left, &PyUnicode_Type);
        promote_to_type(right, &PyUnicode_Type);
        res = new_from_type(&PyBool_Type, space);
        if (res == NULL) goto fail;
    }

    op(_CONTAINS_OP, (left, right -- b)) {
        b = new_from_type(&PyBool_Type, space);
        if (b == NULL) goto fail;
    }

    op(_ITER_NEXT_RANGE, (iter -- iter, next)) {
        next = new_from_type(&PyLong_Type, space);
        if (next == NULL) goto fail;
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

    op(_UNPACK_EX, (seq -- values[oparg & 0xFF], unused, unused[oparg >> 8])) {
        /* This has to be done manually */
        int totalargs = (oparg & 0xFF) + (oparg >> 8) + 1;
        for (int i = 0; i < totalargs; i++) {
            values[i] = new_unknown(space);
            if (values[i] == NULL) {
                goto fail;
            }
        }
    }

    op(_CHECK_ATTR_CLASS, (type_version/2, owner -- owner)) {
        if (is_constant(owner)) {
            PyObject *tp = get_constant(owner);
            if (PyType_Check(tp) && ((PyTypeObject *)tp)->tp_version_tag == type_version) {
                if (PyType_Watch(TYPE_WATCHER_ID, tp)) {
                    return -1;
                }
                _Py_BloomFilter_Add(dependencies, tp);
                this_instr[-1].opcode = _NOP;
            }
        }
    }

    op(_LOAD_ATTR_CLASS, (descr/4, owner -- attr, null if (oparg & 1))) {
        if (this_instr[-1].opcode == _NOP) {
            this_instr[-1].opcode = _POP_TOP;
            global_to_const(this_instr, descr);
            attr = new_constant(descr, space);
        }
        else {
            // We don't know if descr is still a valid object,
            // so we cannot treat it as a constant,
            // even though it would be if we reached this point.
            attr = new_unknown(space);
        }
        if (attr == NULL) goto fail;
        null = new_null(space);
        if (null == NULL) goto fail;
    }


    op(_JUMP_TO_TOP, (--)) {
        return modified;
    }

    op(_EXIT_TRACE, (--)) {
        return modified;
    }



// END BYTECODES //

}

