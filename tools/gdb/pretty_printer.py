# gdb pretty printer for WiredTiger types that are populated by _e or _f structs. For example:
# enum __wt_ref_flags_f {
#     /* AUTOMATIC FLAG VALUE GENERATION START 0 */
#     WT_REF_FLAG_INTERNAL = 0x1u, /* Page is an internal page */
#     WT_REF_FLAG_LEAF = 0x2u,     /* Page is a leaf page */
#     WT_REF_FLAG_READING = 0x4u,  /* Page is being read in */
#     /* AUTOMATIC FLAG VALUE GENERATION STOP 8 */
# };
# typedef uint8_t __wt_ref_flags;
# 
# TODO - think about where to describe why we need this pattern
# These enum items make for easier reading of source code, however they get converted to raw 
# magic numbers at compile time which makes debugging harder. This script will find the mapping 
# between the raw integers and their enum item name, then report this alongside the value 
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
# TODO - check these requirements still hold
# - The WiredTiger sharedlibrary must be loaded in gdb at the time we call this script.
#     - TODO this is still an issue on spawn hosts where the .so path is wrong
# - WiredTiger must have been compiled with macro information enabled. This requires -g3 or -ggdb3
# - Types must be of the form listed above: A typedef for a __wt_* type that has an associated __wt_*_f or 
#   __wt_*_e enum containing the values that populate the type.
#
# To use this script load it inside gdb:
#     source wiredtiger/tools/gdb/pretty_printer.py

import gdb
import re
class enumPrinter:
    """
    Pretty print fields that are populated by an _e or _f enum.
    If the field if populated by an _f (bitflag) enum report as 
        0x101A (WT_READ_IGNORE_CACHE_SIZE | WT_READ_NO_GEN | WT_READ_NO_SPLIT | WT_READ_WONT_NEED)
    If the field if populated by an _e (single value) enum report as 
        2 (WT_REF_LOCKED)
    """

    def __init__(self, val):
        self.val = val

    def enum_string(self, e_type):
        for f in e_type.fields():
            if int(self.val) == f.enumval:
                return f"{int(self.val)} ({f.name})"
        return f"Couldn't find value {self.val} in enum {e_type.name}"

    def bitflag_string(self, f_type):
        active_flags = []
        for f in f_type.fields():
            if f.enumval & int(self.val) == f.enumval:
                active_flags.append(f.name)
        return f"{hex(self.val)} ({' | '.join(active_flags)})"

    def to_string(self):
        # The type name can also includes qualifiers like volatile. 
        # We only want the actual type which is at the end of the string.
        type_name = str(self.val.type).split(' ')[-1]
        # TODO - Is there a cleaner way to do this other than multiple levels of try catch?
        try:
            enum_type = gdb.lookup_type(f"enum {type_name}_e")
            return self.enum_string(enum_type)
        except gdb.error:
            try:
                f_type = gdb.lookup_type(f"enum {type_name}_f")
                return self.bitflag_string(f_type)
            except gdb.error:
                return f"Can't find populating enum for {type_name}"

def wt_pretty_printers():
    """ 
    Find all typedefs matching the patter `typedef .* __wt_.*`. We assume they're 
    paired with either a {typename}_e or an {typename}_f enum which lists that values 
    that can be stored in the type.
    Register these types in the pretty printer.
    """
    printers = gdb.printing.RegexpCollectionPrettyPrinter("wt_pretty_printers")

    types_info = gdb.execute(f"info types __wt_", to_string=True)
    typedef_regex = re.compile("\d+:\ttypedef.* (.*);")
    for line in types_info.split('\n'):
        if match := typedef_regex.match(line):
            type_name = match[1]
            # _f and _e denote helper enums we use to populate the actual fields.
            # Don't register them for pretty printing.
            if not type_name.endswith("_f") and not type_name.endswith("_e"):
                printers.add_printer(type_name, f"^{type_name}$", enumPrinter)
    return printers

if __name__ == '__main__':
    # TODO - Add some error handling
    #      - Not compiled w/ debug output
    #      - No matching _e or _f enums
    wt_printers = gdb.printing.RegexpCollectionPrettyPrinter("wt_pretty_printers")
    gdb.printing.register_pretty_printer(None, wt_pretty_printers(), replace=True)

# TODO - figure out how we fix s_define