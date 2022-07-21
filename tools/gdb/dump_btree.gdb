define dump_row_int_page

end

define dump_page_metadata

end

define dump_row_skip
    set $ins_head = (WT_INSERT_HEAD*)$arg0
    set $ins = $ins_head->head[0]
    while $ins != 0x0

        set $ins = $ins->next[0]
    end
end

define dump_row_leaf_page
    set $page = $arg0
    set $i = 0

    dump_page_metadata $page

    if $page->modify != 0x0
        # WiredTiger hash defines mod_row_insert
        if $page->modify->u2.row_leaf.insert != 0x0
            dump_row_skip $page->modify->u2.row_leaf.insert[$page->entries]
        end
    end

end

define walk_row_int_page
    set $page = $arg0
    set $index = $page.u.intl.__index
    set $entries = $index.entries
    set $i = 0
    printf "Walking row internal page {%p} with %d entries.", $page, $entries
    while $i < $entries
        set $ref = $index.index[$i]
        dump_ref_state $ref
        if $ref->page != 0x0
            set $type = $ref->page->type
            if $type == WT_PAGE_ROW_INT
                p "Walking child internal page."
                walk_row_int_page $ref->page
            end
            if $type == WT_PAGE_ROW_LEAF
                p "Dumping row_leaf page"
                dump_row_leaf_page $ref->page
            end
            if $type != WT_PAGE_ROW_INT && $type != WT_PAGE_ROW_LEAF
                p "Not a rowstore btree"
            end
        else
            p "Found evicted page"
        end
        set $i = $i + 1
    end
    dump_row_int_page $page
end

define walk_btree
    set $root_ref = $arg0
    set $page = $root_ref->page
    if $page->type == WT_PAGE_ROW_INT
        walk_row_int_page $page
    end
end

define dump_inmem_btree
    set $btree = (WT_BTREE*)$arg0
    set $root_ref = &$btree->root
    printf "Dumping btree: %s\n", $btree->dhandle->name
    if $root_ref->page != 0x0
        walk_btree $root_ref
    end
end