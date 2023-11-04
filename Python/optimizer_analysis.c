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
static int globals_watcher;


int builtins_watcher_callback(PyDict_WatchEvent event, PyObject* dict, PyObject* key, PyObject* new_value)
{
    if (event != PyDict_EVENT_CLONED) {
        _Py_Executors_InvalidateAll(_PyInterpreterState_GET());
    }
    return 0;
}

static void
remove_globals(_PyUOpInstruction *buffer, int buffer_size)
{
    PyInterpreterState *interp = _PyInterpreterState_GET();
    int builtins_are_builtins = -1;
    for (int pc = 0; pc < buffer_size; pc++) {
        int opcode = buffer[pc].opcode;
        if (opcode == _GUARD_BUILTINS_VERSION) {
            assert(PyDict_CheckExact(interp->builtins));
            builtins_are_builtins =
                (((PyDictObject *)interp->builtins)->ma_keys->dk_version ==
                buffer[pc].operand);
            if (builtins_are_builtins <= 0) {
                continue;
            }
            if (builtins_watcher < 0) {
                builtins_watcher = PyDict_AddWatcher(builtins_watcher_callback);
                if (builtins_watcher < 0) {
                    PyErr_Clear();
                    return;
                }
                PyDict_Watch(builtins_watcher, interp->builtins);
            }
            buffer[pc].opcode = NOP;
        }
        /* TO DO -- Need access to the function, so that we can get the globals to watch it */
        else if (opcode == _LOAD_GLOBAL_BUILTINS) {
            if (builtins_are_builtins <= 0) {
                continue;
            }
            PyDictObject *bdict = (PyDictObject *)interp->builtins;
            PyDictUnicodeEntry *entries = DK_UNICODE_ENTRIES(bdict->ma_keys);
            PyObject *res = entries[buffer[pc].operand].me_value;
            if (res == NULL) {
                continue;
            }
            if (_Py_IsImmortal(res)) {
                buffer[pc].opcode = (buffer[pc].oparg & 1) ? _INLINE_IMMORTAL_CONSTANT_WITH_NULL : _INLINE_IMMORTAL_CONSTANT;
            }
            else {
                buffer[pc].opcode = (buffer[pc].oparg & 1) ? _INLINE_CONSTANT_WITH_NULL : _INLINE_CONSTANT;
            }
            buffer[pc].operand = (uint64_t)res;
        }
        else if (opcode == _JUMP_TO_TOP || opcode == _EXIT_TRACE) {
            break;
        }
    }
}

static void
remove_unneeded_uops(_PyUOpInstruction *buffer, int buffer_size)
{
    // Note that we don't enter stubs, those SET_IPs are needed.
    int last_set_ip = -1;
    bool need_ip = true;
    bool maybe_invalid = false;
    for (int pc = 0; pc < buffer_size; pc++) {
        int opcode = buffer[pc].opcode;
        if (opcode == _SET_IP) {
            if (!need_ip && last_set_ip >= 0) {
                buffer[last_set_ip].opcode = NOP;
            }
            need_ip = false;
            last_set_ip = pc;
        }
        else if (opcode == _CHECK_VALIDITY) {
            if (maybe_invalid) {
                /* Exiting the trace requires that IP is correct */
                need_ip = true;
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
            // If opcode has ERROR or DEOPT, set need_ip to true
            if (_PyOpcode_opcode_metadata[opcode].flags & (HAS_ERROR_FLAG | HAS_DEOPT_FLAG) || opcode == _PUSH_FRAME) {
                need_ip = true;
            }
            if (_PyOpcode_opcode_metadata[opcode].flags & HAS_ESCAPES_FLAG) {
                maybe_invalid = true;
            }
        }
    }
}


int
_Py_uop_analyze_and_optimize(
    PyCodeObject *co,
    _PyUOpInstruction *buffer,
    int buffer_size,
    int curr_stacklen
)
{
    remove_globals(buffer, buffer_size);
    remove_unneeded_uops(buffer, buffer_size);
    return 0;
}
