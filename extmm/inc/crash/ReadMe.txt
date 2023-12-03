>>> 确定内核地址空间
    查看变量: spa_namespace_avl
    变量地址: 内核全局变量地址空间
    变量内容: kmem_zalloc分配的地址位置

-------------------------------------
>>> [2.2.4.114]
crash> rd spa_namespace_avl
ffff4e760fc45ba0:  fffff13184580108
=> 0xFFFF40... ~ 0xFFFF4F... => 0x000010
=> 0xFFFFF1... ~ 0xFFFFFF... => 0x000020

crash> rd dbuf_hash_table 8
ffff4e760fbe23e8:  0000000000ffffff fffff13148000000
-------------------------------------
>>> [192.168.16.241]
crash> rd spa_namespace_avl
ffff000004bd5cb0:  ffff808279bb0108
=> 0xFFFF00... ~ 0xFFFF0F... => 0x000010
=> 0xFFFF80... ~ 0xFFFF8F... => 0x000020

crash> rd dbuf_hash_table 8
ffff000004b724e8:  0000000000ffffff ffff848318000000
-------------------------------------
