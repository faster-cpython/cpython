
import argparse
import sys

from analyzer import (
    Analysis,
    Instruction,
    Uop,
    analyze_files,
    StackItem,
    analysis_error,
)
from typing import Iterator
from lexer import Token, LPAREN, IDENTIFIER
from generators_common import DEFAULT_INPUT

NON_ESCAPING_FUNCTIONS = (
    "PyDict_New",
    "Py_INCREF",
    "_PyManagedDictPointer_IsValues",
    "_PyObject_GetManagedDict",
    "_PyObject_ManagedDictPointer",
    "_PyObject_InlineValues",
    "_PyDictValues_AddToInsertionOrder",
    "_Py_DECREF_SPECIALIZED",
    "DECREF_INPUTS_AND_REUSE_FLOAT",
    "_PyLong_IsZero",
    "Py_ARRAY_LENGTH",
    "Py_Unicode_GET_LENGTH",
    "PyUnicode_READ_CHAR",
    "_Py_SINGLETON",
    "PyUnicode_GET_LENGTH",
    "_PyLong_IsCompact",
    "_PyLong_IsNonNegativeCompact",
    "_PyLong_CompactValue",
    "_PyLong_DigitCount",
    "PyLong_AsLong",
    "_Py_NewRef",
    "_Py_IsImmortal",
    "PyLong_FromLong",
    "_Py_STR",
    "_PyLong_Add",
    "_PyLong_Multiply",
    "_PyLong_Subtract",
    "PyLong_FromSsize_t",
    "Py_NewRef",
    "PyList_SET_ITEM",
    "_PyList_ITEMS",
    "_PyTuple_ITEMS",
    "_PyList_AppendTakeRef",
    "_Py_atomic_load_uintptr_relaxed",
    "_PyFrame_GetCode",
    "_PyThreadState_HasStackSpace",
    "_PyUnicode_Equal",
    "_PyFrame_SetStackPointer",
    "_PyType_HasFeature",
    "PyUnicode_Concat",
    "_PyList_FromArray",
    "_PyTuple_FromArray",
    "PySlice_New",
    "_Py_LeaveRecursiveCallPy",
    "maybe_lltrace_resume_frame",
    "_PyUnicode_JoinArray",
    "_PyEval_FrameClearAndPop",
    "_PyFrame_StackPush",
    "PyCell_New",
    "PyCell_GetRef",
    "PyCell_SwapTakeRef",
    "PyFloat_AS_DOUBLE",
    "_PyFrame_PushUnchecked",
    "Py_FatalError",
    "assert",
    "Py_Is",
    "Py_IsTrue",
    "Py_IsNone",
    "Py_IsFalse",
    "_PyFrame_GetStackPointer",
    "_PyCode_CODE",
    "PyCFunction_GET_FLAGS",
    "_PyErr_Occurred",
    "_Py_LeaveRecursiveCallTstate",
    "_Py_EnterRecursiveCallTstateUnchecked",
    "_PyFunction_SetVersion",
    "PyStackRef_DUP",
    "PyStackRef_CLOSE",
    "PyStackRef_CLEAR",
    "PyStackRef_AsPyObjectBorrow",
    "PyStackRef_FromPyObjectSteal",
    "PyStackRef_AsPyObjectSteal",
    "PyStackRef_Is",
    "PyStackRef_IsNull",
    "PyTuple_GetItem",
    "PyStackRef_FromPyObjectNew",
    "PyStackRef_FromPyObjectImmortal",
    "PyStackRef_AsPyObjectNew",
    "PyStackRef_TYPE",
    "_PyFrame_PushTrampolineUnchecked",
    "_PyType_NewManagedObject",
    "read_u32",
    "_PyObject_GC_TRACK",
    "_PyObject_GC_IS_TRACKED",
    "_PyObject_GC_MAY_BE_TRACKED",
    "_PyGen_GetGeneratorFromFrame",
    "_PyFrame_IsIncomplete",
)

FLOW_CONTROL = {
    "ESCAPING_CALL",
    "ERROR_IF",
    "DEOPT_IF",
    "ERROR_NO_POP",
}

DECREFS = {
    "Py_DECREF",
    "Py_XDECREF",
    "Py_CLEAR",
    "DECREF_INPUTS",
}

def check_escaping_call(tkn_iter: Iterator[Token]) -> bool:
    res = 0
    parens = 1
    for tkn in tkn_iter:
        if tkn.kind == "LPAREN":
            parens += 1
        elif tkn.kind == "RPAREN":
            parens -= 1
            if parens == 0:
                return
        elif tkn.kind == "GOTO":
            print(f"`goto` in 'ESCAPING_CALL' on line {tkn.line}")
            res = 1
        elif tkn.kind == IDENTIFIER:
            if tkn.text in FLOW_CONTROL:
                print(f"Exiting flow control in 'ESCAPING_CALL' on line {tkn.line}")
                res = 1
            if tkn.text in DECREFS:
                print(f"DECREF in 'ESCAPING_CALL' on line {tkn.line}")
                res = 1
    return res

def is_macro_name(name: str) -> bool:
    if name[0] == "_":
        name = name[1:]
    if name.startswith("Py"):
        name = name[2:]
    return name == name.upper()

def is_getter(name: str) -> bool:
    return "GET" in name

def check_for_unmarked_escapes(uop: Uop) -> None:
    res = 0
    tkns = iter(uop.body)
    for tkn in tkns:
        if tkn.kind != IDENTIFIER:
            continue
        try:
            next_tkn = next(tkns)
        except StopIteration:
            return False
        if next_tkn.kind != LPAREN:
            continue
        if tkn.text == "ESCAPING_CALL":
            if next_tkn.kind != "LPAREN":
                print(f"Expected '(', got '{tkn.text}' on line {tkn.line}")
                res = 1
            if check_escaping_call(tkns):
                res = 1
        if is_macro_name(tkn.text):
            continue
        if is_getter(tkn.text):
            continue
        if tkn.text.endswith("Check") or tkn.text.endswith("CheckExact"):
            continue
        if "backoff_counter" in tkn.text:
            continue
        if tkn.text not in NON_ESCAPING_FUNCTIONS:
            print(f"Unmarked escaping function '{tkn.text}' on line {tkn.line}")
            res = 1
    return res

def verify_uop(uop: Uop) -> None:
    return check_for_unmarked_escapes(uop)

def verify(analysis: Analysis) -> None:
    res = 0
    for uop in analysis.uops.values():
        res |= verify_uop(uop)
    return res


arg_parser = argparse.ArgumentParser(
    description="Verify the bytecode description file.",
    formatter_class=argparse.ArgumentDefaultsHelpFormatter,
)

arg_parser.add_argument(
    "input", nargs=argparse.REMAINDER, help="Instruction definition file(s)"
)

if __name__ == "__main__":
    args = arg_parser.parse_args()
    if len(args.input) == 0:
        args.input.append(DEFAULT_INPUT)
    sys.exit(verify(analyze_files(args.input)))
