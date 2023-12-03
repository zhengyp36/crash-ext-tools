#!/usr/bin/python
# -*- coding: utf-8 -*-

import os
import sys
import extpy

class ModuleLoader(object):
    def __init__(self, crash, simpleInfo=False):
        self.crash = crash
        self.simpleInfo = simpleInfo
    
    def usage(self):
        print('Usage: extpy %s <mod> [mod1 ...]' %
            os.path.basename(sys.argv[0]))
    
    def wrscreen(self, msg):
        sys.stdout.write(msg)
        sys.stdout.flush()
    
    def loadStart(self):
        if self.simpleInfo:
            self.wrscreen('Load modules:')
    
    def loadModStart(self, mod):
        if self.simpleInfo:
            self.wrscreen(' %s' % mod)
        else:
            print('>>> load %s ...' % mod)
    
    def loadModDone(self, mod, ok, output):
        if self.simpleInfo:
            if not ok:
                self.wrscreen('[err]')
        else:
            if not ok:
                print('Error: *** load %s error' % mod)
            self.crash.dump(output)
    
    def loadDone(self):
        if self.simpleInfo:
            self.wrscreen('\n')
    
    def load(self, mods):
        self.loadStart()
        for mod in mods:
            self.loadModStart(mod)
            ok,output = self.crash.run('mod -s ' + mod)
            self.loadModDone(mod, ok, output)
        self.loadDone()

def Main(crash, mods, simpleInfo=False):
    loader = ModuleLoader(crash, simpleInfo)
    if len(mods) == 0:
        loader.usage()
    else:
        loader.load(mods)

if __name__ == '__main__':
    Main(extpy.crashInstance(), sys.argv[1:])
