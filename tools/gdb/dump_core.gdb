# A helper script to dump row store internal page keys.
# Usage: dump_row_int_keys <page>
# Dump keys from a row store internal page.
# Has one argument which is the page.
define dump_row_int_keys
  set pagination off
  set $i = 0
  set $page = $arg0
  while $i < $page->u->intl->__index.entries
    set $ikey = $page->u->intl->__index->index[$i].key.ikey
    set $v = (uintptr_t)$ikey

    set $keyp = 0
    set $size = 0
    set $key_location = ""

    if $v & 0x1
      set $key_location = "DSK:"
      set $key_offset = (($v) & 0xFFFFFFFF) >> 1
      set $keyp = ((void *)((uint8_t *)($page->dsk) + $key_offset))
      set $size = ($v) >> 32
    else
      set $key_location = "MEM:"
      set $keyp = ((void *)((uint8_t *)($ikey) + 8))
      set $size = ((WT_IKEY*)$ikey)->size
    end
    set $sz_pad = ""
    if $size < 10
      set $sz_pad = "  "
    else
      if $size < 100
        set $sz_pad = " "
      end
    end
    set $off_pad = ""
    if $i < 10
      set $off_pad = "   "
    else
      if $i < 100
        set $off_pad = "  "
      else
        if $i < 1000
          set $off_pad = " "
        end
      end
    end
    printf "%s%u:%s%s%u: ", $off_pad, $i, $key_location, $sz_pad, $size
    set $j = 0
    while $j < $size
      printf "%c", ((char*)$keyp)[$j]
      set $j = $j + 1
    end
    printf "\n"

    set $i = $i + 1   
  end
end

define dump_txn_shared_list
  set $conn = $arg0
  set $txn_global = $arg1
  set $session_count = $conn->session_cnt
  set $i = 0
  p "### Dumping the transaction shared list ###"
  while $i < $session_count
    p $txn_global.txn_shared_list[$i]
    set $i = $i + 1
  end
end

define dump_txn_global
  set $conn = $arg0
  set $txn_global = $conn->txn_global

  dump_txn_shared_list $conn $txn_global
  p "### Dumping the txn_global structure ###"
  p $txn_global
end

define dump_btree_list
  set $conn = $arg0

end

define dump_connection
  set $session = $arg0
  set $session = (WT_SESSION_IMPL*)$session
  set $conn = (WT_CONNECTION_IMPL*)$session.iface.connection
  #p *$session
  #p *$conn

  # We have the session and connection dump some useful metadata.
  # The below function dumps the txn_global state, and the associated txn_shared_list.
  #dump_txn_global $conn
  #dump_btree_list $conn
end

define dump_db
  dump_connection $arg0
end

define dump_inmem_btree
  set $root = $arg0

end

define walk_dhandle_queue
  set $session = $arg0
  set $session = (WT_SESSION_IMPL*)$session
  set $conn = (WT_CONNECTION_IMPL*)$session.iface.connection
  set $dhqh = $conn->dhqh
  set $tqh_current = $dhqh.tqh_first
  while $tqh_current != 0x0
    if $tqh_current->type == WT_DHANDLE_TYPE_BTREE
      set $btree = (WT_BTREE *)$tqh_current->handle
      if $btree.root.page != 0x0
        printf "#GREPME#,%s,%p\n",(char*)$tqh_current->name, $btree
      end
    end
    set $tqh_current = $tqh_current->q->tqe_next
  end
end