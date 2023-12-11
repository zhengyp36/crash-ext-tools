--------------------------------------------------------------------------------
  ExtPy工具使用说明
--------------------------------------------------------------------------------
 
 -------------------------------------------------------------------------------
  1. 解决什么问题
 -------------------------------------------------------------------------------
     在Linux内核代码开发过程中, 经常会遇到panic问题, 需要分析原因. crash工具本身
  不能直接执行脚本, 分析过程中, 只能逐条命令手动去敲, 分析效率比较低.
     但是, crash工具提供了扩展其命令的接口和方法, 基于这些接口和方法, 对crash工
  具命令进行了扩展, 扩展功能包括:
     + extpy: 在crash命令行中运行python脚本, python脚本中可以自行决定执行哪些
              crash命令, 并处理这些命令的执行结果;
     + runso: 在crash命令行中导入动态链接库(*.so), 并调用so中的__main__函数,
              __main__函数中可调用一些接口, 直接访问vmcore中的复杂结构(如avl树,
              链表等), 对vmcore进行分析.
 
 -------------------------------------------------------------------------------
  2. 工具编译与安装
 -------------------------------------------------------------------------------
  2.1 下载工具源码包到目标环境上
      $ git clone git@192.168.20.110:ypzheng/crash-ext-tools.git
      如果环境无法连接代码服务器, 可以先把代码下载到本地, 然后再拷贝到目标环境
  2.2 源码编译与安装
      $ cd crash-ext-tools/
      $ bash build.sh local
      $ bash build.sh local install
 
 -------------------------------------------------------------------------------
  3. 工具使用方法
 -------------------------------------------------------------------------------
  3.1 打开crash命令行
      $ crash
      $ crash <vmlinux_path> <vmcore>
  3.2 加载extpy.so, 并初始化工具包(zfs.py setup)
      crash> extend extpy.so
      crash> extpy zfs.py setup
  3.3 执行自定义的python脚本
      crash> extpy <path/script.py> [args...]
      说明:
      1) path/script.py为自定义python脚本的路径, args为该脚本所需参数;
      2) python脚本中调用crash命令的方法如下:
      --------------------------------------------------------------------------
      # 导入模块
      import extpy
      
      # 连接crash进程: 一个脚本中, 该语句只允许执行一次, 不要多次执行!
      crash = extpy.crashInstance()
      
      # 执行crash命令, 并获得命令命令输出:
      ok,output = crash.run('bt')
      # 说明: ok=True表示命令执行成功, False表示失败
      #       output为crash执行bt命令的输出
      
      # 打印命令结果与输出
      if ok:
          print("Info: run 'bt' success")
      else:
          print("Error: *** run 'bt' failure")
      for line in output:
          print(line)
      --------------------------------------------------------------------------
  3.4 编写gdb脚本
      1) gdb自带脚本功能, 有其自身的语法, zfs.gdb为示例代码, 位于代码目录:
         data_verify/extpy/scripts, 安装后位于/usr/local/extpy/gdb;
      2) gdb脚本中可以定义和扩展gdb命令
      3) zfs.gdb脚本中实现了: offsetof, gdb_avl_first, gdb_avl_next, ls_spa
         >> offsetof: 计算结构体中某个成员变量的偏移量;
         >> gdb_avl_first: 找到avl树中最小的节点;
         >> gdb_avl_next: 找到avl树中某个节点的下一个节点;
         >> ls_spa: 指定spa_avl树的地址, 遍历avl树中的所有节点, 即所有spa,
                    打印pool的名字和spa指针;
      4) gdb脚本函数导入方法: 以下命令导入zfs.gdb脚本
         crash> source /usr/local/extpy/gdb/zfs.gdb
  3.5 runso使用举例
      1) 编写.c文件: 可参考extmm/src/bug12269.c, 编写.c文件的说明:
         + 访问内核数据结构的方法(以spa_t为例):
           - 在/usr/local/extpy/inc/auto/zfs_crash.in中增加需要访问的结构: spa_t
           - 在crash中导入模块(spa_t在zfs.ko中定义):
             crash> mod -s zfs
           - 生成头文件/usr/local/extpy/inc/auto/zfs_crash.h:
             crash> extpy ctyp.py /usr/local/extpy/inc/auto/zfs_crash.in
           - 在.c文件中包含头文件:
             #include <auto/zfs_crash.h>
         + 访问内核全局变量的方法(以dbuf_hash_table为例):
           - dbuf_hash_table的类型为: dbuf_hash_table_t, 在zfs_crash.in中添加该
             类型, 并重新生成头文件zfs_crash.h;
           - 在C代码中导入变量:
             KVAR_IMPORT(dbuf_hash_table_t, dbuf_hash_table);
           - 把变量地址导入到用户态地址空间(该变量为内核变量, 导入用户态再访问):
             int rc = EXTAS_IMPORT(dbuf_hash_table); // rc==0表明导入成功
           - 使用KSYM_REF宏访问变量:
             KSYM_REF(dbuf_hash_table).hash_table_mask; // 直接访问变量
             dbuf_hash_table_t *h = &KSYM_REF(dbuf_hash_table); // 获取变量地址
         + 访问内核地址空间的内存:
           - 假设需要访问的内核地址为: void *addr; // addr地址应为0xffff...格式
           - 假设需要访问的内存大小为: size_t size = 2048; // 单位: Byte
           - 把内核态地址转换成用户态地址: void *uaddr = EXTAS->k2u(addr, size);
           - 如果uaddr!=NULL, 说明转换成功, 可以直接访问uaddr; 否则说明转换失败
      2) 编译.c文件生成.zfs.so:
         + 假设bug12269.c位于crash当前目录, 则编译方法如下:
             crash> extpy zfs.py compile bug12269.c
           如果.c文件位于其他目录下, 请给出具体路径, 如当前目录的子目录debug/:
             crash> extpy zfs.py compile debug/bug12269.c
           或给出绝对路径: /dump/nas/dump-12269/debug/bug12269.c
             crash> extpy zfs.py compile /dump/nas/dump-12269/debug/bug12269.c
         + 编译成功后, 生成bug12269.zfs.so, 该文件会拷贝到当前目录下(执行crash命
           令的目录);
      3) 运行bug12269.zfs.so.zfs.so, 不需要跟路径(因为已经拷贝到当前目录):
         crash> runso bug12269.zfs.so.zfs.so
      4) 内核态地址转换为用户态地址的说明:
         + 默认情况下: runso工具根据buf_hash_table和buf_hash_table.ht_table分别
           确定内核态的bss地址和heap地址范围, 在某些环境上可能出现错误, 导致内核
           地址转换失败;
         + 当内核地址转换失败时, 会在当前目录生成日志extmm.log, 在日志中查看错误
           信息, 把无法转换的地址空间加入到当前目录下的extmm.conf文件中, 重新运
           行so即可;
         + 例如: extmm.conf
           ---------------------------------------------------------------------
                        KBASE              UBASE               SIZE
           0xffff400000000000 0x0000200000000000 0x00000f0000000000
           0xfffff00000000000 0x0000300000000000 0x00000f0000000000
           ---------------------------------------------------------------------
         + 如下错误日志表明ffffef319c3f1ff0转换失败, 该地址不在extmm.conf中:
           ---------------------------------------------------------------------
           [ERROR][K2U]convert ffffef319c3f1ff0 error[mm_convert,397]
           ---------------------------------------------------------------------
           修改extmm.conf如下:
           ---------------------------------------------------------------------
                        KBASE              UBASE               SIZE
           0xffff400000000000 0x0000200000000000 0x00000f0000000000
           0xfffff00000000000 0x0000300000000000 0x00000f0000000000
           0xffffe00000000000 0x0000400000000000 0x0000100000000000
           ---------------------------------------------------------------------
           重新运行so, 即可解决上述转换错误:
           crash> runso bug12269.zfs.so.zfs.so
 -------------------------------------------------------------------------------
  4. 已封装的命令介绍
 -------------------------------------------------------------------------------
     现已封装少量命令, 作为编写脚本的示例代码, 这些代码位于代码目录:
  data_verify/extpy/scripts, 安装后位于/usr/local/extpy/bin; 放在安装目录下的
  python脚本, 运行时可以不指定路径.
 -------------------------------------------------------------------------------
  4.1 导入内核模块
      crash> extpy mod.py zfs spl
      --------------------------------------------------------------------------
      >>> load zfs ...
           MODULE       NAME                     SIZE  OBJECT FILE
      ffff0000033c57c0  zfs                   5570560  /lib/modules/4.19.90-25.10.v2101.ky10.aarch64/extra/zfs/zfs/zfs.ko 
      
      >>> load spl ...
           MODULE       NAME                     SIZE  OBJECT FILE
      ffff000002960780  spl                    262144  /lib/modules/4.19.90-25.10.v2101.ky10.aarch64/extra/zfs/spl/spl.ko 
      --------------------------------------------------------------------------
  
  4.2 解析内核符号信息
      crash> extpy sym.py spa_namespace_avl kcf_sreq_cache kcf_context_cache
      --------------------------------------------------------------------------
      >>> parse {spa_namespace_avl} ...
      [Module] zfs
      [Name  ] spa_namespace_avl
      [Type  ] avl_tree_t
      [Addr  ] 0xffff0000034b5ca0
      [Value ] {
        avl_root = 0xffff808165ae8108, 
        avl_compar = 0xffff000003193f30 <spa_name_compare>, 
        avl_offset = 264, 
        avl_numnodes = 1, 
        avl_size = 16824
      ...
      
      >>> parse {kcf_sreq_cache} ...
      [Module] icp
      [Name  ] kcf_sreq_cache
      [Type  ] spl_kmem_cache_t *
      [Addr  ] 0xffff000002b922a8
      [Value ] (spl_kmem_cache_t *) 0xffff848349860e00
      
      
      >>> parse {kcf_context_cache} ...
      [Module] icp
      [Name  ] kcf_context_cache
      [Type  ] spl_kmem_cache_t *
      [Addr  ] 0xffff000002b92298
      [Value ] (spl_kmem_cache_t *) 0xffff848349861e00
      --------------------------------------------------------------------------
  
  4.3 加载zfs模块并导入自定义gdb命令
      crash> extpy zfs.py setup
      --------------------------------------------------------------------------
      Load modules: zfs spl zstmf zstmf_sbd ziscsit zidm
      Source GDB scripts: zfs.gdb
      --------------------------------------------------------------------------
