#include "Python.h"

#include "pycore_ceval.h"
#include "pycore_frame.h"
#include "pycore_jit.h"

#include "jit.h"

_Py_CODEUNIT *
_JIT_ENTRY(
    _PyExecutorObject *exec, _PyInterpreterFrame *frame, _PyStackRef *stack_pointer, PyThreadState *tstate
) {
    int cached = exec->vm_data.tos_cache;
    _PyStackRef tos0 = cached ? stack_pointer[-cached] : PyStackRef_ZERO_BITS;
    _PyStackRef tos1 = stack_pointer[-1-(cached&1)]; /* Correct value for 2 or 3, harmless junk for 0 or 1 */
    _PyStackRef tos2 = stack_pointer[-1]; /* Correct value for 3, harmless junk otherwise */
    tstate->jit_exit = NULL;
    jit_func_preserve_none jitted = (jit_func_preserve_none)exec->jit_code;
    return jitted(frame, stack_pointer-cached, tstate, tos0, tos1, tos2);
}
