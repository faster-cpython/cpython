"""Generate the cases for the tier 2 interpreter.
Reads the instruction definitions from bytecodes.c.
Writes the cases to executor_cases.c.h, which is #included in ceval.c.
"""

import argparse
import os.path
import sys

from analyzer import (
    Analysis,
    Instruction,
    Uop,
    Part,
    analyze_files,
    Skip,
    StackItem,
    analysis_error,
)
from generators_common import (
    DEFAULT_INPUT,
    ROOT,
    write_header,
    emit_tokens,
    emit_to,
    REPLACEMENT_FUNCTIONS,
)
from cwriter import CWriter
from typing import TextIO, Iterator
from lexer import Token
from stack import StackOffset, Stack, SizeMismatch


def validate_uop(override: Uop, uop: Uop) -> None:
    # To do
    pass

def type_name(var: StackItem, prefix: str) -> str:
    if var.is_array():
        return f"{prefix}Value **"
    if var.type:
        return var.type
    return f"{prefix}Value *"


def declare_variables(uop: Uop, name: str, out: CWriter, peeks: bool) -> None:
    variables = {"unused"}
    for var in reversed(uop.stack.inputs):
        if var.peek and not peeks:
            continue
        if var.name not in variables:
            variables.add(var.name)
            if var.condition:
                out.emit(f"{type_name(var, name)}{var.name} = NULL;\n")
            else:
                out.emit(f"{type_name(var, name)}{var.name};\n")
    for var in uop.stack.outputs:
        if var.peek and not peeks:
            continue
        if var.name not in variables:
            variables.add(var.name)
            if var.condition:
                out.emit(f"{type_name(var, name)}{var.name} = NULL;\n")
            else:
                out.emit(f"{type_name(var, name)}{var.name};\n")

def decref_all_inputs(
    out: CWriter,
    uop: Uop
) -> None:
    for var in uop.stack.inputs:
        if var.name == "unused" or var.peek:
            continue
        if var.size != "1":
            out.emit(f"for (int _i = {var.size}; --_i >= 0;) {{\n")
            out.emit(f"DECREF({var.name}[_i]);\n")
            out.emit("}\n")
        else:
            if var.condition:
                out.emit(f"if ({var.name} != NULL) ")
            out.emit(f"DECREF({var.name});\n")

def decref_inputs(
    out: CWriter,
    tkn: Token,
    tkn_iter: Iterator[Token],
    uop: Uop,
    stack: Stack,
    inst: Instruction | None,
) -> None:
    next(tkn_iter)
    next(tkn_iter)
    next(tkn_iter)
    out.emit_at("", tkn)
    decref_all_inputs(out, uop)

def emit_default(out: CWriter, uop: Uop) -> None:
    decref_all_inputs(out, uop)
    for i, var in enumerate(uop.stack.outputs):
        if var.name != "unused" and not var.peek:
            if var.is_array():
                out.emit(f"for (int _i = {var.size}; --_i >= 0;) {{\n")
                out.emit(f"{var.name}[_i]  = UNKNOWN();\n")
                out.emit(f"if ({var.name}[_i] == NULL) goto fail;\n")
                out.emit("}\n")
            elif var.name == "null":
                out.emit(f"{var.name} = NULL_VALUE();\n")
                out.emit(f"if ({var.name} == NULL) goto fail;\n")
            else:
                out.emit(f"{var.name} = UNKNOWN();\n")
                out.emit(f"if ({var.name} == NULL) goto fail;\n")

def write_uop(
    override: Uop | None, uop: Uop, out: CWriter, stack: Stack, debug: bool
) -> None:
    try:
        prototype = override if override else uop
        peeks = override is not None
        out.start_line()
        for var in reversed(prototype.stack.inputs):
            if not var.peek or peeks:
                out.emit(stack.pop(var))
        if not prototype.properties.stores_sp:
            for i, var in enumerate(prototype.stack.outputs):
                if not var.peek or peeks:
                    out.emit(stack.push(var))
        if debug:
            args = []
            for var in prototype.stack.inputs:
                if not var.peek or peeks:
                    args.append(var.name)
            out.emit(f'DEBUG_PRINTF({", ".join(args)});\n')
        if override:
            for cache in uop.caches:
                if cache.name != "unused":
                    if cache.size == 4:
                        type = cast = "PyObject *"
                    else:
                        type = f"uint{cache.size*16}_t "
                        cast = f"uint{cache.size*16}_t"
                    out.emit(f"{type}{cache.name} = ({cast})this_instr->operand;\n")
            emit_tokens(out, override, stack, None, {"DECREF_INPUTS": decref_inputs})
        else:
            emit_default(out, uop)
        if prototype.properties.stores_sp:
            for i, var in enumerate(prototype.stack.outputs):
                if not var.peek or peeks:
                    out.emit(stack.push(var))
        out.start_line()
        stack.flush(out)
    except SizeMismatch as ex:
        raise analysis_error(ex.args[0], uop.body[0])


SKIPS = ("_EXTENDED_ARG",)


def generate_abstract_interpreter(
    filenames: list[str], abstract: Analysis, base: Analysis, name: str, outfile: TextIO, debug: bool
) -> None:
    write_header(__file__, filenames, outfile)
    out = CWriter(outfile, 2, False)
    out.emit("\n")
    for uop in base.uops.values():
        override: Uop | None = None
        if uop.name in abstract.uops:
            override = abstract.uops[uop.name]
            validate_uop(override, uop)
        if uop.properties.tier_one_only:
            continue
        if uop.is_super():
            continue
        if not uop.is_viable():
            out.emit(f"/* {uop.name} is not a viable micro-op for tier 2 */\n\n")
            continue
        out.emit(f"case {uop.name}: {{\n")
        if override:
            declare_variables(override, name, out, True)
        else:
            declare_variables(uop, name, out, False)
        stack = Stack()
        write_uop(override, uop, out, stack, debug)
        out.start_line()
        out.emit("break;\n")
        out.emit("}")
        out.emit("\n\n")


arg_parser = argparse.ArgumentParser(
    description="Generate the code for the tier 2 interpreter.",
    formatter_class=argparse.ArgumentDefaultsHelpFormatter,
)

arg_parser.add_argument(
    "-o", "--output", type=str, help="Generated code"
)

arg_parser.add_argument(
    "-n", "--name", help="Name of the abstract interpreter", default="abstract"
)

arg_parser.add_argument(
    "input", nargs=1, help="Abstract interpreter definition file"
)

arg_parser.add_argument(
    "base", nargs=argparse.REMAINDER, help="The base instruction definition file(s)"
)

arg_parser.add_argument(
    "-d", "--debug", help="Insert debug calls", action="store_true"
)

if __name__ == "__main__":
    args = arg_parser.parse_args()
    if len(args.base) == 0:
        args.input.append(DEFAULT_INPUT)
    abstract = analyze_files(args.input)
    base = analyze_files(args.base)
    with open(args.output, "w") as outfile:
        generate_abstract_interpreter(args.input, abstract, base, args.name, outfile, args.debug)
