#include "Python.h"
#include "pycore_uops.h"
#include "pycore_uop_ids.h"

#define op(name, ...) /* NAME is ignored */

#define LINKAGE extern
#include "pycore_specialize_support.h"

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

// BEGIN BYTECODES //

    op(_GUARD_BOTH_INT, (left, right -- left, right)) {
        if (is_int(left)) {
            this_instr->opcode = is_int(right) ? _NOP : _GUARD_TOS_INT;
        }
        else if (is_int(right)) {
            this_instr->opcode = _GUARD_NOS_INT;
        }
        promote_to_type(left, &PyLong_Type);
        promote_to_type(right, &PyLong_Type);
    }

    op(_GUARD_BOTH_FLOAT, (left, right -- left, right)) {
        if (is_float(left)) {
            this_instr->opcode = is_float(right) ? _NOP : _GUARD_TOS_FLOAT;
        }
        else if (is_float(right)) {
            this_instr->opcode = _GUARD_NOS_FLOAT;
        }
        promote_to_type(left, &PyFloat_Type);
        promote_to_type(right, &PyFloat_Type);
    }

    op(_LOAD_CONST, (-- value)) {
        PyObject *k = PyTuple_GET_ITEM(code->co_consts, oparg);
        value = new_constant(k, space);
        this_instr->opcode = _Py_IsImmortal(k) ?
            _LOAD_INLINE_IMMORTAL_CONST :
            _LOAD_INLINE_CONST;
        this_instr->operand = (uintptr_t)k;
    }

    op(_LOAD_FAST, (-- value)) {
        value = get_local(frame, oparg);
        assert(value != NULL);
        INCREF(value);
    }

    op(_LOAD_FAST_CHECK, (-- value)) {
        value = get_local(frame, oparg);
        assert(value != NULL);
        INCREF(value);
    }

    op(_STORE_FAST, (value --)) {
        SpecializerValue *tmp = get_local(frame, oparg);
        set_local(frame, oparg, value);
        DECREF(tmp);
    }

    op(_UNARY_NOT, (value -- res)) {
        if (is_constant(value)) {
            if (get_constant(value) == Py_True) {
                res = new_constant(Py_False, space);
            }
            else {
                assert(get_constant(value) == Py_False);
                res = new_constant(Py_True, space);
            }
        }
        else {
            assert(is_bool(value));
            res = value;
        }
    }

    op(_TO_BOOL_BOOL, (value -- value)) {
        if (is_bool(value)) {
            this_instr->opcode = _NOP;
        }
        else {
            promote_to_type(value, &PyBool_Type);
        }
    }

    op(_TO_BOOL_NONE, (value -- res)) {
        if (get_constant(value) == Py_None) {
            this_instr->opcode = _NOP;
        }
        res = new_constant(Py_False, space);
    }

    op(_TO_BOOL_ALWAYS_TRUE, (version/2, value -- res)) {
        PyTypeObject *tp = get_type(value);
        if (tp && tp->tp_version_tag == version) {
            add_type_watcher(tp, version, executor);
            this_instr->opcode = _NOP;
        }
        res = new_constant(Py_True, space);
    }

    op(UNARY_NEGATIVE, (value -- res)) {
        if (is_int(value)) {
            res = new_from_type(&PyLong_Type, space);
        }
        else if (is_float(value)) {
            res = new_from_type(&PyFloat_Type, space);
        }
        else {
            res = new_unknown(space);
        }
        DECREF(value);
    }

    op(_BUILD_STRING, (pieces[oparg] -- str)) {
        DECREF_INPUTS();
        str = new_from_type(&PyUnicode_Type, space);
    }

    op(_BUILD_TUPLE, (values[oparg] -- tup)) {
        DECREF_INPUTS();
        tup = new_from_type(&PyTuple_Type, space);
    }

    op(_BUILD_LIST, (values[oparg] -- list)) {
        DECREF_INPUTS();
        list = new_from_type(&PyList_Type, space);
    }

    op (_GUARD_IS_TRUE_POP, (flag -- )) {
        if (get_constant(flag) == Py_True) {
            this_instr->opcode = _NOP;
        }
        else {
            promote_to_const(flag, Py_True);
        }
        DECREF(flag);
    }

    op (_GUARD_IS_FALSE_POP, (flag -- )) {
        if (get_constant(flag) == Py_False) {
            this_instr->opcode = _NOP;
        }
        else {
            promote_to_const(flag, Py_False);
        }
        DECREF(flag);
    }

    op (_GUARD_IS_NONE_POP, (flag -- )) {
        if (get_constant(flag) == Py_None) {
            this_instr->opcode = _NOP;
        }
        else {
            promote_to_const(flag, Py_None);
        }
        DECREF(flag);
    }

    op (_GUARD_IS_NOT_NONE_POP, (flag -- )) {
        if (is_not_none(flag)) {
            this_instr->opcode = _NOP;
        }
        else {
            promote_to_not_none(flag);
        }
        DECREF(flag);
    }

    op(_BINARY_OP_ADD_INT,  (left, right -- res)) {
        res = binary_op(PyNumber_Add, &PyLong_Type, left, right, space);
    }

    op(_BINARY_OP_ADD_FLOAT,  (left, right -- res)) {
        res = binary_op(PyNumber_Add, &PyFloat_Type, left, right, space);
    }

    op(_BINARY_OP_SUBTRACT_INT,  (left, right -- res)) {
        res = binary_op(PyNumber_Subtract, &PyLong_Type, left, right, space);
    }

    op(_BINARY_OP_SUBTRACT_FLOAT,  (left, right -- res)) {
        res = binary_op(PyNumber_Subtract, &PyFloat_Type, left, right, space);
    }

    op(_BINARY_OP_MULTIPLY_INT,  (left, right -- res)) {
        res = binary_op(PyNumber_Multiply, &PyLong_Type, left, right, space);
    }

    op(_BINARY_OP_MULTIPLY_FLOAT,  (left, right -- res)) {
        res = binary_op(PyNumber_Multiply, &PyFloat_Type, left, right, space);
    }

    op(_JUMP_TO_TOP, (--)) {
        clear_frames(frame);
        return;
    }

    op(_EXIT_TRACE, (--)) {
        clear_frames(frame);
        return;
    }

    op(_COMPARE_OP_STR, (left, right -- res)) {
        promote_to_type(left, &PyUnicode_Type);
        promote_to_type(right, &PyUnicode_Type);
        DECREF(left);
        DECREF(right);
        res = new_from_type(&PyBool_Type, space);
    }

    op(_ITER_CHECK_LIST, (iter -- iter)) {
        promote_to_type(iter, &PyListIter_Type);
    }

    op(_COPY, (bottom, unused[oparg-1] -- bottom, unused[oparg-1], top)) {
        assert(oparg > 0);
        INCREF(bottom);
        top = bottom;
    }

    op(_SWAP, (bottom, unused[oparg-2], top --
                    top, unused[oparg-2], bottom)) {
    }

    op(_INIT_CALL_PY_EXACT_ARGS, (callable, self_or_null, args[oparg] -- new_frame: SpecializerFrame*)) {
        int argcount = oparg;
        if (!is_constant(callable)) {
            clear_frames(frame);
            return;
        }
        PyFunctionObject *func = (PyFunctionObject *)callable;
        PyCodeObject *code = (PyCodeObject *)func->func_code;
        // Need tests to see is self_or_null is NULL, not NULL or unknown
        if (!is_null(self_or_null)) {
            args--;
            argcount++;
        }
        new_frame = make_frame(code, space, args, argcount);
    }

    op(_PUSH_FRAME, (new_frame: SpecializerFrame* -- unused if (0))) {
        new_frame->back = frame;
        frame = new_frame;
    }

    op(_POP_FRAME, (retval -- res)) {
        STORE_SP();
        SpecializerFrame *old_frame = frame;
        frame = frame->back;
        destroy_frame(old_frame);
        LOAD_SP();
        res = retval;
    }


// END BYTECODES //

}
