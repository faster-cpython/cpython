#include "Python.h"
#include "opcode.h"
#include "pycore_interp.h"
#include "pycore_opcode_metadata.h"
#include "pycore_opcode_utils.h"
#include "pycore_pystate.h"       // _PyInterpreterState_GET()
#include "pycore_uops.h"
#include "pycore_dict.h"
#include "pycore_long.h"
#include "cpython/optimizer.h"
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include "pycore_optimizer.h"

/* TO DO -- make these per interpreter */
static int builtins_watcher = -1;
static int globals_watcher = -1;


int builtins_watcher_callback(PyDict_WatchEvent event, PyObject* dict, PyObject* key, PyObject* new_value)
{
    if (event != PyDict_EVENT_CLONED) {
        _Py_Executors_InvalidateAll(_PyInterpreterState_GET());
    }
    return 0;
}

int globals_watcher_callback(PyDict_WatchEvent event, PyObject* dict, PyObject* key, PyObject* new_value)
{
    if (event != PyDict_EVENT_CLONED) {
        _Py_Executors_InvalidateDependency(_PyInterpreterState_GET(), dict);
    }
    /* TO DO -- Mark the dict, as a warning for future optimization attempts */
    return 0;
}

static void
global_to_const(_PyUOpInstruction *inst, PyObject *obj)
{
    assert(inst->opcode == _LOAD_GLOBAL_MODULE || inst->opcode == _LOAD_GLOBAL_BUILTINS);
    assert(PyDict_CheckExact(obj));
    PyDictObject *dict = (PyDictObject *)obj;
    assert(dict->ma_keys->dk_kind == DICT_KEYS_UNICODE);
    PyDictUnicodeEntry *entries = DK_UNICODE_ENTRIES(dict->ma_keys);
    assert(inst->operand < dict->ma_used);
    assert(inst->operand <= UINT16_MAX);
    PyObject *res = entries[inst->operand].me_value;
    if (res == NULL) {
        return;
    }
    if (_Py_IsImmortal(res)) {
        inst->opcode = (inst->oparg & 1) ? _INLINE_IMMORTAL_CONSTANT_WITH_NULL : _INLINE_IMMORTAL_CONSTANT;
    }
    else {
        inst->opcode = (inst->oparg & 1) ? _INLINE_CONSTANT_WITH_NULL : _INLINE_CONSTANT;
    }
    inst->operand = (uint64_t)res;
}

static int
check_globals_version(_PyUOpInstruction *inst, PyObject *obj)
{
    if (!PyDict_CheckExact(obj)) {
        return -1;
    }
    PyDictObject *dict = (PyDictObject *)obj;
    if (dict->ma_keys->dk_version != inst->operand) {
        return -1;
    }
    return 0;
}


static void
remove_globals(_PyUOpInstruction *buffer, int buffer_size,
               _PyInterpreterFrame *frame, _PyBloomFilter *dependencies)
{
    PyInterpreterState *interp = _PyInterpreterState_GET();
    PyFunctionObject *func = (PyFunctionObject *)frame->f_funcobj;
    assert(PyFunction_Check(func));
    PyObject *builtins = frame->f_builtins;
    PyObject *globals = frame->f_globals;
    assert(func->func_builtins == builtins);
    assert(func->func_globals == globals);
    /* In order to treat globals and builtins as a constant we
     * need to verify that the function version is as expected */
    if (interp->builtins != builtins) {
        return;
    }
    bool builtins_is_guarded = false;
    bool globals_is_guarded = false;
    for (int pc = 0; pc < buffer_size; pc++) {
        _PyUOpInstruction *inst = &buffer[pc];
        int opcode = inst->opcode;
        switch(opcode) {
            case _GUARD_BUILTINS_VERSION:
                if (builtins_watcher < 0) {
                    builtins_watcher = PyDict_AddWatcher(builtins_watcher_callback);
                    if (builtins_watcher < 0) {
                        PyErr_Clear();
                        return;
                    }
                    PyDict_Watch(builtins_watcher, builtins);
                }
                if (check_globals_version(inst, builtins)) {
                    continue;
                }
                if (builtins_is_guarded) {
                    buffer[pc].opcode = NOP;
                }
                else {
                    buffer[pc].opcode = _GUARD_BUILTINS_DICT;
                    buffer[pc].operand = (uint64_t)builtins;
                    builtins_is_guarded = true;
                }
                break;
            case _GUARD_GLOBALS_VERSION:
                if (globals_watcher < 0) {
                    globals_watcher = PyDict_AddWatcher(globals_watcher_callback);
                    if (globals_watcher < 0) {
                        PyErr_Clear();
                        return;
                    }
                }
                if (check_globals_version(&buffer[pc], globals)) {
                    continue;
                }
                if (globals_is_guarded) {
                    buffer[pc].opcode = NOP;
                }
                else {
                    _Py_BloomFilter_Add(dependencies, globals);
                    PyDict_Watch(globals_watcher, globals);
                    buffer[pc].opcode = _GUARD_GLOBALS_DICT;
                    buffer[pc].operand = (uint64_t)globals;
                    globals_is_guarded = true;
                }
                break;
            case _LOAD_GLOBAL_BUILTINS:
                if (globals_is_guarded && builtins_is_guarded) {
                    global_to_const(inst, builtins);
                }
                break;
            case _LOAD_GLOBAL_MODULE:
                if (globals_is_guarded) {
                    global_to_const(inst, globals);
                }
                break;
            case _CHECK_PEP_523:
                if (interp->eval_frame == NULL) {
                    buffer[pc].opcode = NOP;
                }
                break;
            case LOAD_CONST:
            {
                PyObject *consts = ((PyCodeObject *)frame->f_executable)->co_consts;
                PyObject *val = PyTuple_GET_ITEM(consts, inst->oparg);
                if (_Py_IsImmortal(val)) {
                    inst->opcode = _INLINE_IMMORTAL_CONSTANT;
                }
                else {
                    inst->opcode = _INLINE_CONSTANT;
                }
                inst->operand = (uint64_t)val;
                break;
            }
            case _JUMP_TO_TOP:
            case _EXIT_TRACE:
                goto done;
            case _PUSH_FRAME:
            case _POP_FRAME:
                goto done;
        }
    }
    done:
        return;
}

static void
remove_unneeded_uops(_PyUOpInstruction *buffer, int buffer_size)
{
    // Note that we don't enter stubs, those SET_IPs are needed.
    int last_set_ip = -1;
    bool maybe_invalid = true;
    for (int pc = 0; pc < buffer_size; pc++) {
        int opcode = buffer[pc].opcode;
        if (opcode == _SET_IP) {
            buffer[pc].opcode = NOP;
            last_set_ip = pc;
        }
        else if (opcode == _CHECK_VALIDITY) {
            if (maybe_invalid) {
                maybe_invalid = false;
            }
            else {
                buffer[pc].opcode = NOP;
            }
        }
        else if (opcode == _JUMP_TO_TOP || opcode == _EXIT_TRACE) {
            break;
        }
        else {
            if (OPCODE_HAS_ESCAPES(opcode)) {
                maybe_invalid = true;
                if (last_set_ip >= 0) {
                    buffer[last_set_ip].opcode = _SET_IP;
                }
            }
            if (OPCODE_HAS_ERROR(opcode) || opcode == _PUSH_FRAME) {
                if (last_set_ip >= 0) {
                    buffer[last_set_ip].opcode = _SET_IP;
                }
            }
        }
    }
}


static const char *
uop_name(int index) {
    if (index <= MAX_REAL_OPCODE) {
        return _PyOpcode_OpName[index];
    }
    return _PyOpcode_uop_name[index];
}

static void dump_uops(_PyUOpInstruction *buffer, int buffer_size)
{
    printf("-------------------------------\n");
    for (int i = 0; i < buffer_size; i++) {
        int opcode = buffer[i].opcode;
        printf("%d: %s, %d\n", i, uop_name(opcode), buffer[i].oparg);
        if (opcode == _JUMP_TO_TOP || opcode == _EXIT_TRACE) {
            break;
        }
    }
}

int
_Py_uop_analyze_and_optimize(
    _PyInterpreterFrame *frame,
    _PyUOpInstruction *buffer,
    int buffer_size,
    PyObject **stack_pointer,
    _PyBloomFilter *dependencies
)
{
    // dump_uops(buffer, buffer_size);
    remove_globals(buffer, buffer_size, frame, dependencies);
    // printf(" -->\n");
    // dump_uops(buffer, buffer_size);
    remove_unneeded_uops(buffer, buffer_size);
    return 0;
}
