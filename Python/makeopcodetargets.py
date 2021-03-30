#! /usr/bin/env python
"""Generate C code for the jump table of the threaded code interpreter
(for compilers supporting computed gotos or "labels-as-values", such as gcc).
"""

import os
import sys


try:
    from importlib.machinery import SourceFileLoader
except ImportError:
    import imp

    def find_module(modname):
        """Finds and returns a module in the local dist/checkout.
        """
        modpath = os.path.join(
            os.path.dirname(os.path.dirname(__file__)), "Lib")
        return imp.load_module(modname, *imp.find_module(modname, [modpath]))
else:
    def find_module(modname):
        """Finds and returns a module in the local dist/checkout.
        """
        modpath = os.path.join(
            os.path.dirname(os.path.dirname(__file__)), "Lib", modname + ".py")
        return SourceFileLoader(modname, modpath).load_module()


def write_targets(f):
    opcode = find_module('opcode')
    targets = [f'_unknown_opcode_{i}' for i in range(256)]
    for opname, op in opcode.opmap.items():
        targets[op] = "TARGET_%s" % opname
    f.write("static void *opcode_targets[256] = {\n")
    f.write(",\n".join(["    &&%s" % s for s in targets]))
    f.write("\n};\n")

def write_unknowns(f):
    opcode = find_module('opcode')
    unknowns = [ True ] * 256
    for opname, op in opcode.opmap.items():
        unknowns[op] = False
    for i, unknown in enumerate(unknowns):
        if unknown:
            f.write(f"    UNKNOWN_OPCODE({i}):\n")
            f.write(f"        oparg = {i};\n")
            f.write(f"        goto _unknown_opcode;\n")
    f.write("\n")

def main():
    if len(sys.argv) >= 4:
        sys.exit(f"Too many arguments: {len(sys.argv)}")
    output = None
    opt = None
    for arg in sys.argv[1:]:
        if arg.startswith("--"):
            opt = arg
        else:
            output = arg
    if opt == "--targets":
        if output is None:
            output = "Python/opcode_targets.h"
        with open(output, "w") as f:
            write_targets(f)
        print("Jump table written into %s" % output)
    elif opt == "--unknowns":
        if output is None:
            output = "Python/unknown_opcodes.h"
        with open(output, "w") as f:
            write_unknowns(f)
        print("Unknown opcodes written into %s" % output)
    else:
        sys.exit("Unknwown option " + opt)

if __name__ == "__main__":
    main()
