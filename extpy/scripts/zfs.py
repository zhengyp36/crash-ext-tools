#!/usr/bin/python
# -*- coding: utf-8 -*-

import os
import sys
import extpy
import mod
import ctyp

appname = os.path.basename(sys.argv[0])
crash = extpy.crashInstance()

def genIncHeader():
    zfs_crash_in = '/usr/local/extpy/inc/auto/zfs_crash.in'
    if os.path.exists(zfs_crash_in):
        wrscreen('Generate zfs_crash.h:\n')
        wrscreen('-' * 80 + '\n')
        ctyp.Main(ctyp.Crash(crash), ['ctyp.py', zfs_crash_in]).run()
        wrscreen('-' * 80 + '\n')

def cmd_compile(args, genInc=True):
    dir_extpy = '/usr/local/extpy'
    if len(args) < 1:
        print('Usage: extpy %s compile <*.c> ...' % appname)
    
    if genInc:
        genIncHeader()
    
    targets = []
    for f in args:
        if not f.endswith('.c'):
            print('Error: *** %s is not a C-code' % f)
            return
        
        fname = os.path.basename(f).split('.c')[0]
        dst = '%s/src/%s.c' % (dir_extpy, fname)
        
        if not os.path.exists(f):
            if os.path.exists(dst):
                f = dst
            else:
                print('Error: *** %s does not exist' % f)
                return
        
        realpath = os.path.realpath(f)
        if realpath != dst:
            rc = os.system('/bin/cp %s %s' % (f, dst))
            if rc != 0:
                print('Error: *** copy %s error' % f)
                return
        
        targets.append(fname)
    
    for fname in targets:
        bin = 'release/%s.zfs.so' % fname
        cmd = 'make -C %s %s' % (dir_extpy, bin)
        
        print('>>> Compile %s.c => %s' % (fname, bin))
        print(cmd)
        
        rc = os.system(cmd)
        if rc != 0:
            print('Error: *** compile %s error' % bin)
            return
        
        rc = os.system('/bin/cp %s/%s ./' % (dir_extpy, bin))
        if rc != 0:
            print('Error: *** copy %s error' % bin)
            return

class ZfsCrashConf(object):
    MOD_LIST = ['zfs', 'spl', 'zstmf', 'zstmf_sbd', 'ziscsit', 'zidm']
    GDB_VARS = ['spa_namespace_avl', 'sbd_lu_list']
    RUNSO_LS = ['ls_zvol.c', 'rwlock.c']
    
    def __init__(self, crash):
        self.crash = crash
    
    def run(self, cmd):
        ok,output = self.crash.run(cmd)
        if not ok:
            infos = ['Error: *** run-cmd(%s) error' % cmd]
            infos += output
            raise Exception('\n'.join(infos))
        return output
    
    def setGdbVar(self, name):
        try:
            output = self.run('rd ' + name)
            addr = '0x' + output[0].split(':')[0].strip()
            self.run('gdb set $__CTypeAddrOf_%s = (void*)%s' % (name, addr))
        except:
            print('Error: *** init variable(%s) error' % name)
    
    def setup(self):
        mod.Main(crash, type(self).MOD_LIST, simpleInfo=True)
        
        genIncHeader()
        
        for var in type(self).GDB_VARS:
            self.setGdbVar(var)
        
        print('>>> Generate runso builtin commands:')
        cmd_compile(type(self).RUNSO_LS, genInc=False)

def usage():
    print('Usage: extpy %s setup' % appname)
    print('       extpy %s compile' % appname)

def wrscreen(msg):
    sys.stdout.write(msg)
    sys.stdout.flush()

def cmd_setup(args):
    if len(args) > 0:
        print('Usage: extpy %s setup' % appname)
        return
    
    ZfsCrashConf(crash).setup()
    
    gdbScriptPath = '/usr/local/extpy/gdb'
    gdbScripts = ['zfs.gdb']
    
    wrscreen('Source GDB scripts:')
    for script in gdbScripts:
        wrscreen(' ' + script)
        ok,output = crash.run('gdb source %s/%s' % (gdbScriptPath, script))
        if not ok:
            wrscreen('[err]')
    wrscreen('\n')
    
    wrscreen('Extend runso.so ')
    ok,output = crash.run('extend runso.so')
    if not ok:
        wrscreen('error:\n')
        crash.dump(output)
    else:
        wrscreen('success.\n')


if __name__ == '__main__':
    commands = {
        'setup'   : cmd_setup,
        'compile' : cmd_compile,
    }
    
    if len(sys.argv) > 1 and sys.argv[1] == 'DEBUG':
        os.environ['EXTPY_DEBUG'] = 'true'
        sys.argv = [sys.argv[0]] + sys.argv[2:]
    
    if len(sys.argv) == 1 or sys.argv[1] not in commands:
        usage()
    else:
        commands[sys.argv[1]](sys.argv[2:])
