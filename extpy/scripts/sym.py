#!/usr/bin/python
# -*- coding: utf-8 -*-

import os
import sys
import extpy

class SymbolParser(object):
    def __init__(self, crash):
        self.crash = crash
    
    def usage(self):
        print('Usage: extpy %s [--all] <symbol_name> [...]' %
            os.path.basename(sys.argv[0]))
        print('Where --all means dump all infos if symbol value is too many.')
    
    def lookupSym(self, sym):
        fd = os.popen('cat /proc/kallsyms | grep -w ' + sym)
        symInfo = fd.read().split('\n')[0].split()
        fd.close()
        
        if len(symInfo) < 3 or symInfo[2] != sym:
            return None
        
        if len(symInfo) == 4:
            mod = symInfo[3].split('[')[1].split(']')[0]
        else:
            mod = ''
        
        addr = '0x' + symInfo[0]
        return [mod, addr]
    
    def parseSymType(self, sym):
        ok,output = self.crash.run('gdb whatis ' + sym)
        if not ok or len(output) == 0:
            print("Error: *** parse type of '%s' error." % sym)
            self.crash.dump(output)
            return ''
        
        prefix = 'type = '
        typeInfo = output[0]
        if not typeInfo.startswith(prefix):
            print("Error: *** type of '%s' is not startswith(%s)" % (
                sym, prefix))
            self.crash.dump(output)
            return ''
        
        typeInfo = typeInfo.split(prefix)[1].split('{')[0].strip()
        if not typeInfo:
            print("Error: *** parse type of '%s' error." % sym)
            self.crash.dump(output)
            return ''
        
        extra = ''
        if '}' in output[-1]:
            # Such as 'struct X {...} *' where mark-star means pointer
            extra = output[-1].split('}')[-1].strip()
        
        return typeInfo + extra
    
    def parseSym(self, sym, dump_all=False):
        symInfo = self.lookupSym(sym)
        if not symInfo:
            print('Error: *** symbol(%s) is not found in /proc/kallsyms' % sym)
            return
        
        mod,addr = symInfo
        if mod:
            ok,output = self.crash.run('mod -s ' + mod)
            if not ok:
                print('Error: *** load module(%s) error' % mod)
                self.crash.dump(output)
                return
        
        typeInfo = self.parseSymType(sym)
        if not typeInfo:
            return
        
        tmpVar = '$________'
        cmd = 'gdb set %s = (%s*)%s' % (tmpVar, typeInfo, addr)
        ok,output = self.crash.run(cmd)
        if not ok:
            print('Error: *** cmd(%s) run error' % cmd)
            self.crash.run(output)
            return
        
        newCmd = 'gdb print *' + tmpVar
        ok,newOutput = self.crash.run(newCmd)
        if not ok:
            print('Error: *** cmd(%s) run error' % newCmd)
            self.crash.dump(newOutput)
            print('But cmd(%s) run ok' % cmd)
            self.crash.dump(output)
            return
        
        if len(newOutput) > 5 and not dump_all:
            value = '\n'.join(newOutput[:6] + ['...'])
        else:
            value = '\n'.join(newOutput)
        pos = value.find('= ')
        if pos > 0:
            value = value[pos+2:]
        
        print('[Module] ' + mod)
        print('[Name  ] ' + sym)
        print('[Type  ] ' + typeInfo)
        print('[Addr  ] ' + addr)
        print('[Value ] ' + value)

def Main(crash, args):
    parser = SymbolParser(crash)
    
    options = ['--all']
    dump_all = False
    
    symTbl = []
    for sym in args:
        if sym.startswith('-'):
            if sym not in options:
                print('Error: *** invalid option(%s).' % sym)
                return
            if sym == '--all':
                dump_all = True
        elif sym not in symTbl:
            symTbl.append(sym)
    
    for sym in symTbl:
        print('>>> parse {%s} ...' % sym)
        parser.parseSym(sym, dump_all)
        print('')
    
    if len(symTbl) == 0:
        parser.usage()

if __name__ == '__main__':
    Main(extpy.crashInstance(), sys.argv[1:])
