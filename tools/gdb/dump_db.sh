#!/bin/bash
# This script executes gdb and dumps information about the database state.
# First we execute gdb with a basic command to find a session pointer.
# Arg1: executable name Arg2: Core file
#echo $1 $2
gdb -ex='t a a bt' $1 $2 -ex=quit > traces
session_pointer=`cat traces | grep 'session=0x' | cut -d '=' -f 2 | cut -d ',' -f 1 | cut -d ')' -f 1 | head -1`
#echo $session_pointer

gdb -x "dump_core.gdb" -ex "walk_dhandle_queue $session_pointer" -ex "quit" $1 $2 > btree_pointer_array
btree_pointers=`cat btree_pointer_array | grep "#GREPME#" | sort -u -t',' -k2 | cut -d ',' -f 3`
for i in $btree_pointers
do
    echo $i
    gdb -x "dump_btree.gdb" -ex "dump_inmem_btree $i" -ex "quit" $1 $2 > tree_dump
    exit 0
done


#echo -e "$session_list"
#session_pointer=`echo $session_list | head -1`
#echo $session_pointer
#gdb -x "dump_core.gdb" -ex "dump_db $session_pointer" -ex "quit" $1 $2 > db