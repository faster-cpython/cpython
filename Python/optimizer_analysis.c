#include "Python.h"
#include "opcode.h"
#include "pycore_interp.h"
#include "pycore_opcode_metadata.h"
#include "pycore_opcode_utils.h"
#include "pycore_pystate.h"       // _PyInterpreterState_GET()
#include "pycore_uop_metadata.h"
#include "pycore_long.h"
#include "cpython/optimizer.h"
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include "pycore_optimizer.h"

extern int
_Py_Tier2_Specialize(PyCodeObject *code,_PyUOpInstruction *buffer,
           int buffer_size, int curr_stackdepth,
           _PyBloomFilter* dependencies);


static void
peephole_opt(PyCodeObject *co, _PyUOpInstruction *buffer, int buffer_size)
{
    for (int pc = 0; pc < buffer_size; pc++) {
        int opcode = buffer[pc].opcode;
        switch(opcode) {
            case _LOAD_CONST: {
                assert(co != NULL);
                PyObject *val = PyTuple_GET_ITEM(co->co_consts, buffer[pc].oparg);
                buffer[pc].opcode = _Py_IsImmortal(val) ? _LOAD_CONST_INLINE_BORROW : _LOAD_CONST_INLINE;
                buffer[pc].operand = (uintptr_t)val;
                break;
            }
            case _CHECK_PEP_523:
            {
                /* Setting the eval frame function invalidates
                 * all executors, so no need to check dynamically */
                if (_PyInterpreterState_GET()->eval_frame == NULL) {
                    buffer[pc].opcode = _NOP;
                }
                break;
            }
            case _PUSH_FRAME:
            case _POP_FRAME:
                co = (PyCodeObject *)buffer[pc].operand;
                break;
            case _JUMP_TO_TOP:
            case _EXIT_TRACE:
                return;
        }
    }
}

static void
remove_unneeded_uops(_PyUOpInstruction *buffer, int buffer_size)
{
    int last_set_ip = -1;
    bool maybe_invalid = false;
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
            if (_PyUop_Flags[opcode] & HAS_ESCAPES_FLAG) {
                maybe_invalid = true;
                if (last_set_ip >= 0) {
                    buffer[last_set_ip].opcode = _SET_IP;
                }
            }
            if ((_PyUop_Flags[opcode] & HAS_ERROR_FLAG) || opcode == _PUSH_FRAME) {
                if (last_set_ip >= 0) {
                    buffer[last_set_ip].opcode = _SET_IP;
                }
            }
        }
    }
}

static void
dump_uops(_PyUOpInstruction *buffer, int n)
{
    printf("TRACE:\n");
    for (int i = 0; i < n; i++) {
        int opcode = buffer[i].opcode;
        int oparg = buffer[i].oparg;
        int operand = buffer[i].operand;
        printf("    %s oparg: %d, operand: %d\n",
               _PyOpcode_uop_name[opcode],
               oparg, operand);
        if (opcode == _EXIT_TRACE || opcode == _JUMP_TO_TOP) {
            break;
        }
    }
    printf("------------------\n\n");
}

int
_Py_uop_analyze_and_optimize(
    PyCodeObject *co,
    _PyUOpInstruction *buffer,
    int buffer_size,
    int curr_stackdepth,
    _PyBloomFilter* dependencies
)
{
    dump_uops(buffer, buffer_size);
    peephole_opt(co, buffer, buffer_size);
    int mod = _Py_Tier2_Specialize(co, buffer, buffer_size, curr_stackdepth, dependencies);
    if (mod) {
        printf("MODIFIED ");
        dump_uops(buffer, buffer_size);
    }
    remove_unneeded_uops(buffer, buffer_size);
    return 0;
}
