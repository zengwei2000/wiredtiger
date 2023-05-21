# gdb pretty printer for WiredTiger types that are populated by #define constants. For example:
#   #define WT_REF_FLAG_INTERNAL 0x1u /* Page is an internal page */
#   #define WT_REF_FLAG_LEAF 0x2u     /* Page is a leaf page */
#   #define WT_REF_FLAG_READING 0x4u  /* Page is being read in */
#                                     /* AUTOMATIC FLAG VALUE GENERATION STOP 8 */
#   typedef uint8_t __wt_ref_flags;
# 
# These #defines make for easier reading of source code, however they get converted to raw 
# magic numbers at compile time which makes debugging harder. This script will find the mapping 
# between the raw integers and their #defines name, then report this alongside the value 
# when the type is printed in gdb. Changing
#   (gdb) p *ref
#   $1 = {
#   ...
#   flags = 6 '\006', 
#   state = 2 '\002', 
# to
#   (gdb) p *ref
#   $2 = {
#   ...
#   flags = 0x6 (WT_REF_FLAG_READING | WT_REF_FLAG_LEAF), 
#   state = 2 (WT_REF_LOCKED), 
#  
# Note this depends on a few things:
# - The WiredTiger sharedlibrary must be loaded in gdb at the time we call this script.
# - WiredTiger must have been compiled with macro information enabled. This requires -g3 or -ggdb3
# - Types must be of the form listed above: A typedef for a __wt_* type that is directly preceded by a list of #defines
# - The #defines must be contiguous. A gap between #defines of more than one line will stop loading definitions
#
# To use this script load it inside gdb:
#     source wiredtiger/tools/gdb/pretty_printer.py

import gdb
import re

def typedef_location(type_name: str):
    """
    Parse gdb's `info types` output to get the file and location of a typedef in the source code.
    e.g. typedef_location("__wt_ref_type") 
        ('/home/ubuntu/src/wiredtiger/src/include/btmem.h', 925)
    """
    typedef_info = gdb.execute(f"info types {type_name}", to_string=True)
    message_regex = re.compile(
        f"All types matching regular expression \"{type_name}\":\n"
        f"\n"
        f"File (.*):\n"
        f"(\d+):\s+typedef .* {type_name};"
    )
    if m := message_regex.match(typedef_info):
        return (m[1], int(m[2]))

def get_wt_defines_and_locations(info_macros: str):
    """
    Parse gdb's `info macros` output and return a mapping from each macros location and definition.
    e.g. {('/home/ubuntu/src/wiredtiger/src/include/btmem.h', 1635): ('WT_VRFY_DISK_EMPTY_PAGE_OK', '0x2')}
    """
    macro_defs = {}
    defined_regex = re.compile("Defined at (.*):(\d+)")
    definition_regex = re.compile("#define (WT_\w+) ((?:0x)?[0-9]+)(?:u)?")

    cur_file = ""
    for line in info_macros.split('\n'):
        if match := defined_regex.match(line):
            cur_file, cur_line = match[1], int(match[2])
        elif match := definition_regex.match(line):
            macro_defs[(cur_file, cur_line)] = (match[1], match[2])
        else:
            # Skipping line describing the include path
            pass
    return macro_defs

def macros_for_typedef(type_name: str):
    """
    Given the name of a __wt_* typedef return associated macro definitions. 
    e.g. macros_for_typedef("__wt_ref_flags")
         {'0x04': 'WT_REF_FLAG_READING', '0x02': 'WT_REF_FLAG_LEAF', '0x01': 'WT_REF_FLAG_INTERNAL'}
    """
    info_macros = gdb.execute("info macros main", to_string=True)
    macro_locations = get_wt_defines_and_locations(info_macros)
    typedef_file_name, typedef_line = typedef_location(type_name)

    typedef_macros = {}

    # Check the lines preceding the typedef definition for all associated #defines.
    # Groups of #defines should be contiguous, but we allow a one line gap for when 
    # comments overflow or "AUTOMATIC FLAG VALUE GENERATION" annotiations
    line_num = typedef_line - 1
    gap_size = 0
    while gap_size < 2 and line_num >= 0:
        file_and_line = (typedef_file_name, line_num)
        if file_and_line in macro_locations:
            macro_name, macro_val = macro_locations[file_and_line]
            typedef_macros[macro_val] = macro_name
            gap_size = 0
        else:
            gap_size += 1
        line_num -= 1
    return typedef_macros

def wtMacroFieldPrinter(td_name: str):
    class wtMacroFieldPrinter:
        """
        Pretty print fields that are populated by WT_* macros.
        If the field stores bitflags report as 
            0x101A (WT_READ_IGNORE_CACHE_SIZE | WT_READ_NO_GEN | WT_READ_NO_SPLIT | WT_READ_WONT_NEED)
        If the field stores a single value report as
            2 (WT_REF_LOCKED)
        """
        typedef_name = td_name

        def __init__(self, val):
            self.val = val

        def storing_bitflags(self, macros_used):
            return list(macros_used.keys())[0].startswith("0x")

        def to_string(self):
            v = int(self.val)
            macros_used = macros_for_typedef(self.typedef_name)
            try:
                if self.storing_bitflags(macros_used):
                    enabled_flags = [name for val, name in macros_used.items() if v & int(val, 16) == int(val, 16)]
                    enabled_flag_list = f"({' | '.join(enabled_flags)})"
                    return f"{hex(v)} {enabled_flag_list}"
                else:
                    return f"{v} ({macros_used[str(v)]})"
            except Exception as e:
                # If this fails fall back on printing the normal value. Please see the comment at the 
                # top of the file (in particular the contiguous requirement) for requirements that must be met.
                return f"{v} (Failed to load pretty print details for {self.typedef_name})"
    return wtMacroFieldPrinter

def wiredtiger_pretty_printers():
    """
    Find all typedefs preceded by a list of #defines and register them for pretty printing
    """
    printers = gdb.printing.RegexpCollectionPrettyPrinter("wt_pretty_printers")

    info_macros = gdb.execute("info macros main", to_string=True)
    macro_locations = get_wt_defines_and_locations(info_macros)

    if len(macro_locations) == 0:
        # If we won't have macro definitions we can't build pretty print information
        print("Can't find macro definitions for WiredTiger. The WiredTiger sharedlibrary "
              "may not be loaded yet, or not compiled with -ggdb3")
        return printers

    types_info = gdb.execute(f"info types __wt_", to_string=True)
    typedef_regex = re.compile("\d+:\ttypedef.* (.*);")
    for line in types_info.split('\n'):
        if match := typedef_regex.match(line):
            type_name = match[1]
            typedef_file_name, typedef_line = typedef_location(type_name)
            # Quick sanity check. Only register for pretty printing if there's a macro 
            # definition two lines above. We might've found a typedef not preceded by 
            # macros and therefore doesn't need pretty printing.
            if (typedef_file_name, typedef_line - 2) in macro_locations:
                printers.add_printer(type_name, f"^{type_name}$", wtMacroFieldPrinter(type_name))

    return printers

if __name__ == '__main__':
    # Register our pretty printers with gdb
    gdb.printing.register_pretty_printer(None, wiredtiger_pretty_printers(), replace=True)
