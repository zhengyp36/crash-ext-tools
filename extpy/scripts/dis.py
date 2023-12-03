#!/usr/bin/python
# -*- coding: utf-8 -*-

import os
import sys
import extpy

class Disassemble(object):
    def __init__(self, crash):
        self.crash = crash
        self._cache = self.crash.cache('dis')
        os.system('mkdir -p ' + self._cache)
        
        fd = os.popen('uname -p')
        self.platform = fd.read().split('\n')[0]
        fd.close()
    
    def usage(self):
        print('Usage: extpy %s [opts] <symbol_name>' %
            os.path.basename(sys.argv[0]))
    
    def cache(self, relative_path=''):
        if relative_path:
            return self._cache + '/' + relative_path
        else:
            return self._cache
    
    def doDis(self, args):
        path = '.'.join(args)
        cmdArgs = ' '.join(args)
        regCache = self.cache('reg.' + path)
        disCache = self.cache('dis.' + path)
        
        if os.path.exists(regCache) and os.path.exists(disCache):
            return True
        return self.doDisImpl(cmdArgs, regCache, disCache)
    
    def doDisImpl(self, cmdArgs, regCache, disCache):
        cmd = 'NON_GDB dis ' + cmdArgs
        ok,output = self.crash.run(cmd)
        if not ok:
            print('Error: *** run cmd(%s) error.' % cmd)
            self.crash.dump(output)
            return False
        
        fd = open(disCache, 'w')
        for line in output:
            fd.write(line + '\n')
        fd.close()
        
        limit=20
        print('NOTES: dump %d lines at most and check %s for more.' % (
            limit, disCache))
        self.crash.dump(output, limit=limit)
        
        self.parseRegs(output, regCache)
        return True
    
    def parseRegs(self, disContents, regCache):
        if self.platform != 'aarch64':
            os.system('touch ' + regCache)
            return
        
        os.system('touch ' + regCache)
        # TODO: parse first instructions and tell which regs are saved: x19...

def Main(crash, args):
    disassembler = Disassemble(crash)
    if len(args) == 0:
        disassembler.usage()
        return
    
    disassembler.doDis(args)

if __name__ == '__main__':
    Main(extpy.crashInstance(), sys.argv[1:])
