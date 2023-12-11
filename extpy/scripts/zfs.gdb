# ------------------------------------------------------------------------------
# Two way to load the script:
#     1. paste the contents in ~/.gdbinit by vi and re-start crash-tool
#     2. load the script by crash command: crash> source zfs.gdb
# ------------------------------------------------------------------------------

set confirm off
set pagination off

define offsetof
    if $argc < 2 || $argc > 3
        printf "Usage: offsetof <type> <member> [$var]\n"
    end
    if $argc == 2
        print (uintptr_t)&(($arg0*)0)->$arg1
    end
    if $argc == 3
        eval "set $arg2 = (uintptr_t)&(($arg0*)0)->$arg1"
    end
end

define gdb_avl_first
    if $argc < 1 || $argc > 3
        printf "Usage: gdb_avl_first <avl_tree_t*> [$var] [node_type]\n"
    end
    
    if $argc >= 1 && $argc <= 3
        set $_tree = $arg0
        set $_prev = (avl_node_t*)0
        set $_off = $_tree->avl_offset
        set $_node = $_tree->avl_root
        
        while $_node != 0
            set $_prev = $_node
            set $_node = $_node->avl_child[0]
        end
        
        set $_ret = (void*)0
        if $_prev
            set $_ret = (void*)((uintptr_t)$_prev - $_off)
        end
        
        if $argc == 1
            print $_ret
        end
        
        if $argc == 2
            eval "set $arg1 = $_ret"
        end
        
        if $argc == 3
            eval "set $arg1 = ($arg2*)$_ret"
        end
    end
end

define gdb_avl_next
    if $argc < 2 || $argc > 4
        printf "Usage: gdb_avl_next <avl_tree_t*> <curr_node> [$var] [node_type]\n"
    end
    
    if $argc >= 2 && $argc <= 4
        set $_ret = (void*)0
        if $arg1 != 0
            set $_tree = $arg0
            set $_off = $_tree->avl_offset
            set $_curr = (struct avl_node*)((uintptr_t)$arg1 + $_off)
            
            if $_curr->avl_child[1] != 0
                set $_node = $_curr->avl_child[1]
                while $_node->avl_child[0] != 0
                    set $_node = $_node->avl_child[0]
                end
            end
            
            if $_curr->avl_child[1] == 0
                set $_node = $_curr
                while 1
                    set $_was_child = (uintptr_t)($_node->avl_pcb >> 2) & 1
                    set $_node = (struct avl_node*)($_node->avl_pcb & ~7)
                    if $_node == 0
                        loop_break
                    end
                    if $_was_child == 0
                        loop_break
                    end
                end
            end
            
            if $_node != 0
                set $_ret = (void*)((uintptr_t)$_node - $_off)
            end
        end
        
        if $argc == 2
            print $_ret
        end
        
        if $argc == 3
            eval "set $arg2 = $_ret"
        end
        
        if $argc == 4
            eval "set $arg2 = ($arg3*)$_ret"
        end
    end
end

define ls_spa
    set $_tree = (avl_tree_t*)$__CTypeAddrOf_spa_namespace_avl
    gdb_avl_first $_tree $_node
    while $_node
        set $_spa = (spa_t*)$_node
        printf "name=%s,spa=%p\n", $_spa->spa_name, $_spa
        gdb_avl_next $_tree $_node $_node
    end
end

define ls_rr_abd
    if $argc != 1
        printf "Usage: ls_rr_abd <addrOf(raidz_row_t)>\n"
    end
    
    if $argc == 1
        set $__rr = (raidz_row_t*)$arg0
        set $__ndx = 0
        while $__ndx < $__rr->rr_cols
            set $__rrc = $__rr->rr_col[$__ndx]
            if $__rrc.rc_abd
                if $__rrc.rc_abd->abd_flags & ABD_FLAG_LINEAR
                    printf "[%2d] %p %p\n", $__ndx, $__rrc.rc_abd, $__rrc.rc_abd->abd_u.abd_linear.abd_buf
                end
                if !($__rrc.rc_abd->abd_flags & ABD_FLAG_LINEAR)
                    printf "[%2d] rc_abd is not linear\n", $__ndx
                end
            end
            if $__rrc.rc_abd == 0
                printf "[%2d] rc_abd = null\n", $__ndx
            end
            set $__ndx = $__ndx + 1
        end
    end
end

define ls_zio_ai_info
    if $argc != 1
        printf "Usage: ls_zio_ai_info <addrOf(zio)>\n"
    end
    if $argc == 1
        set $__zio = (zio_t*)$arg0
        printf "aio(%p), isRoot(%d), order(%d)\n", $__zio->io_aggre_io, $__zio->io_aggre_root, $__zio->io_aggre_order
    end
end

define ls_ai
    if $argc != 2
        printf "Usage: ls_ai <addrOf(aggre_io)> <member:ai_buf_array|ai_dbuf_array|ai_zio_array>\n"
    end
    if $argc == 2
        set $__ai = (aggre_io_t*)$arg0
        set $__idx = 0
        while $__idx < $__ai->ai_together
            printf "[%2d] %p\n", $__idx, $__ai->$arg1[$__idx]
            set $__idx = $__idx + 1
        end
    end
end

define ls_ai_abd
    if $argc != 1
        printf "Usage: ls_ai <addrOf(aggre_io)>\n"
    end
    if $argc == 1
        set $__ai = (aggre_io_t*)$arg0
        set $__idx = 0
        while $__idx < $__ai->ai_together
            set $__zio = (zio_t*)$__ai->ai_zio_array[$__idx]
            set $__abd = (abd_t*)$__ai->ai_buf_array[$__idx]
            printf "[%2d] %p %p; %p %p\n", $__idx, $__zio->io_abd, $__zio->io_abd->abd_u.abd_linear.abd_buf, $__abd, $__abd->abd_u.abd_linear.abd_buf
            set $__idx = $__idx + 1
        end
    end
end

define ls_sbd_lu
    set $__idx = 0
    set $__sbd_lu_list = *(sbd_lu_t**)$__CTypeAddrOf_sbd_lu_list
    while $__sbd_lu_list
        printf "[%02d]lu=%p,name=%s\n", $__idx, $__sbd_lu_list, $__sbd_lu_list->sl_name
        set $__idx = $__idx + 1
        set $__sbd_lu_list = $__sbd_lu_list->sl_next
    end
end
