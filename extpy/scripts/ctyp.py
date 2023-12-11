#!/usr/bin/python
# -*- coding: utf-8 -*-

import os
import sys
import time
import extpy

class Crash(object):
    def __init__(self, crash):
        self.crash = crash
    
    def strip(self, arr):
        while arr:
            if not arr[0]:
                arr = arr[1:]
            elif not arr[-1]:
                arr = arr[:-1]
            else:
                break
        return arr
    
    def exit(self, rc=0):
        sys.exit(rc)
    
    def run(self, cmd):
        ok,output = self.crash.run(cmd)
        return [ok, self.strip(output)]
    
    def dump(self, args):
        self.crash.dump(args)
    
    def runTypeCmd(self, typeCmd, typeName):
        cmd = '%s %s' % (typeCmd, typeName)
        ok,output = self.run(cmd)
        if not ok:
            raise Exception('\n\t'.join(
                ['\n\tRun crash-cmd {%s} error:' % cmd] + output
            ))
        
        output[0] = output[0].split('type = ')[1]
        return output
    
    def gdbVar(self, name):
        return '$__ctyp_' + name
    
    def setFlag(self, name, value=1):
        cmd = 'gdb set %s = %d' % (self.gdbVar(name), value)
        ok, output = self.run(cmd)
        if not ok:
            raise Exception('\n\t'.join(
                ['\n\tRun crash-cmd {%s} error:' % cmd] + output
            ))
    
    def clrFlag(self, name):
        self.setFlag(name, value=0)
    
    def chkFlag(self, name):
        cmd = 'gdb p %s' % self.gdbVar(name)
        ok, output = self.run(cmd)
        if not ok:
            raise Exception('\n\t'.join(
                ['\n\tRun crash-cmd {%s} error:' % cmd] + output
            ))
        return output[0].split('=')[1].strip() == '1'
    
class Progress(object):
    def __init__(self, showProgress=False):
        self.showProgress = showProgress
        if showProgress:
            self.ts = time.time()
            self.idx = 0
            self.charList = ['-','\\','|','/']
    
    def show(self, msg):
        if self.showProgress:
            if time.time() - self.ts >= 0.2:
                sys.stdout.write(self.charList[self.idx] + '\r')
                sys.stdout.flush()
                self.idx = (self.idx + 1) % len(self.charList)
                self.ts = time.time()
        else:
            print(msg)
    
class CTypeParser(object):
    DEBUG = False
    @classmethod
    def isDebug(cls):
        key = 'EXTPY_DEBUG'
        debug = False
        if key in os.environ:
            debugStr = os.environ[key]
            if debugStr in ['true', 'True', 'yes', 'Yes']:
                debug = True
        
        return debug or cls.DEBUG
    
    # Notes:
    #       1. Place the enum-type-alias before enum-type-itself
    #       2. How to handle typedef-lists: A=>B=>C... ?
    
    def __init__(self, crash):
        if type(self).isDebug():
            showProgress = False
        else:
            showProgress = True
        
        self.crash = crash
        self.progress = Progress(showProgress=showProgress)
        self.track = []
        
        self.map = {
            # Basic types
            'void'      : 'base',
            'signed'    : 'base',
            'unsigned'  : 'base',
            'char'      : 'base',
            'short'     : 'base',
            'int'       : 'base',
            'long'      : 'base',
            'float'     : 'base',
            'double'    : 'base',
            
            # Builtin types defined in stdint.h, stddef.h, ...
            'int8_t'    : 'builtin',
            'int16_t'   : 'builtin',
            'int32_t'   : 'builtin',
            'int64_t'   : 'builtin',
            'uint8_t'   : 'builtin',
            'uint16_t'  : 'builtin',
            'uint32_t'  : 'builtin',
            'uint64_t'  : 'builtin',
            'intptr_t'  : 'builtin',
            'uintptr_t' : 'builtin',
            'size_t'    : 'builtin',
            'ssize_t'   : 'builtin',
            
            # Combinational types
            'struct'    : 'struct',
            'union'     : 'union',
            'enum'      : 'enum',
            
            # Other types
            #<one-word> : 'alias',     # defined by typedef
            #<one-word> : 'function',  # defined by typedef
        }
        
        self.hasBuiltin = False
        self.names = set()
        self.excludeNames = set()
        
        self.contents = []
        self.index = {}
    
    def whatis(self, typeName):
        return self.crash.runTypeCmd('whatis', typeName)
    
    def ptype(self, typeName):
        return self.crash.runTypeCmd('ptype', typeName)
    
    def parse(self, infile):
        fd = open(infile)
        lines = fd.read().split('\n')
        fd.close()
        
        for line in lines:
            line = line.strip()
            if line and not line.startswith('#'):
                if line.startswith('-'):
                    self.excludeNames.add(line[1:].strip())
                else:
                    self.parseName(line, True)
    
    def dump(self, macro, outfile):
        print('Start to dump ...')
        
        contents = [
            '#ifndef %s' % macro,
            '#define %s' % macro,
            '',
        ]
        
        if self.hasBuiltin:
            contents += [
                '#ifndef EXCLUDE_BUILTIN_HEADER',
                '#include <stddef.h>',
                '#include <stdint.h>',
                '#endif // EXCLUDE_BUILTIN_HEADER',
                '',
            ]
        
        contents += [
            '#ifdef __cplusplus',
            'extern "C" {',
            '#endif',
            '',
        ]
        
        dumpRec = set()
        for nameTag in self.contents:
            if nameTag not in dumpRec:
                dumpRec.add(nameTag)
                curr_macro = self.tag2Macro(nameTag)
                contents.append('/* %s */' % nameTag)
                contents.append('#ifndef ' + curr_macro)
                contents += self.index[nameTag]
                contents.append('#endif // ' + curr_macro)
                contents.append('')
        
        contents += [
            '#ifdef __cplusplus',
            '// extern "C" ',
            '#endif',
            '#endif // %s' % macro
        ]
        
        fd = open(outfile, 'w')
        for line in contents:
            inv_def = '<no data fields>'
            if inv_def in line:
                line = line.replace(inv_def, 'char inv_def[1]; // %s' % inv_def)
            fd.write(line + '\n')
        fd.close()
        
        print('Dump into %s success' % outfile)
    
    def genNameTag(self, name, strong):
        return {
            True  : 'STRONG: ',
            False : 'WEAK: ',
        }[strong] + name
    
    def tag2Macro(self, nameTag):
        return 'EXCLUDE_' + nameTag.replace(' ', '_').replace(':','_').upper()
    
    def addName(self, names, strong, ptype=None):
        if not isinstance(names, list):
            names = [names]
        
        nameTagIndex = []
        for nm in names:
            nameTag = self.genNameTag(nm,strong)
            if nameTag not in self.names:
                self.names.add(nameTag)
                if nameTag in self.index:
                    nameTagIndex.append(nameTag)
        
        assert(len(nameTagIndex) <= 1)
        if not nameTagIndex and ptype:
            nameTag = self.genNameTag(names[0], strong)
            self.contents.append(nameTag)
            ptype[-1] += ';'
            self.index[nameTag] = ptype
    
    def nameAdded(self, name, strong):
        if self.genNameTag(name,True) in self.names:
            return True
        elif not strong:
            return self.genNameTag(name,False) in self.names
        else:
            return False
    
    def trackName(self, name, strong, func, type_=None):
        if type_:
            type_ = str(type_) + ':'
        else:
            type_ = ''
        
        strongStr = {
            True  : 'strong',
            False : 'weak'
        }[strong]
        
        if func:
            func = ' @' + str(func)
        else:
            func = ''
        
        return type_ + strongStr + ':{' + str(name) + '}' + func
    
    def trackInit(self, name, strong, type_=None):
        self.track = [self.trackName(name, strong, '', type_)]
    
    def trackAppend(self, name, strong, func, type_=None):
        self.track.append(self.trackName(name, strong, func, type_))
    
    def trackPop(self):
        if len(self.track) > 0:
            self.track.pop()
    
    def track2str(self):
        if self.track:
            return '\n\t-> '.join(self.track)
        else:
            return '<emtpy>'
    
    def parseName(self, name, strong, initTrack=True):
        if name in self.excludeNames:
            return
        if self.nameAdded(name, strong):
            return
        if initTrack:
            self.trackInit(name, strong)
        else:
            self.trackAppend(name, strong, 'parseName', type_='parseName')
        
        tmp,arr = name.split(),[]
        for s in tmp:
            if s not in ['const', 'volatile']:
                arr.append(s)
        
        newName = ' '.join(arr)
        if newName != name:
            showNewName = ' => %s' % newName
            self.addName(name, strong)
        else:
            showNewName = ''
        
        self.progress.show('Parse[%s] %s%s ...' % (
            {
                True  : 'Strong',
                False : 'Weak  ',
            }[strong], name, showNewName
        ))
        
        if arr[0] in self.map:
            self.parseNameWithType(newName, strong, self.map[arr[0]])
        else:
            self.parseOneWordName(newName, strong)
        
        self.trackPop()
    
    def parseNameWithType(self, name, strong, type_):
        self.trackAppend(name, strong, 'parseNameWithType', type_)
        
        if type_ == 'base':
            self.addName(name, strong)
        elif type_ == 'builtin':
            self.addName(name, strong)
            self.hasBuiltin = True
        elif type_ in ['struct','union']:
            self.parseCombinationType(name, strong, type_)
        elif type_ == 'enum':
            self.parseEnum(name)
        else:
            raise Exception('invalid type(%s)' % type_)
        
        self.trackPop()
    
    def parseCombinationDepends(self, impl):
        assert('{' in impl[0] and '}' in impl[-1])
        impl = impl[1:-1]
        
        for line in impl:
            line = line.strip()
            if '(' in line:
                self.parseFunctionTypeDepends([line])
            elif '...' in line:
                self.progress.show('Parse[Skip  ] %s' % line)
            elif '{' in line:
                arr = line.replace('{','').split()
                assert(arr[0] in ['struct','union','enum'])
                if not len(arr) in [1,2]:
                    print(arr)
                assert(len(arr) in [1,2])
                if len(arr) == 2:
                    # TODO: how to handle inner-combinational-types
                    # self.parseName(' '.join(arr), True, initTrack=False)
                    pass
            elif '}' not in line:
                if not line.endswith(';'):
                    if '<no data fields>' in line:
                        continue
                    else:
                        raise Exception('\n'.join(
                            ["Error: *** line(%s) is not endswith ';'" % line]
                            + impl
                            + ['>>> dump type-track:']
                            + [self.track2str()]
                        ))
                line = line[:-1]
                arr = line.split() # remove member-name
                strong = '*' not in arr[-1]
                self.parseName(' '.join(arr[:-1]), strong, initTrack=False)
    
    def parseCombinationType(self, name, strong, type_):
        self.trackAppend(name, strong, 'parseCombinationType', type_)
        
        if strong:
            impl = self.ptype(name)
            self.parseCombinationDepends(impl)
            self.addName(name, True, impl)
        else:
            self.addName(name, False, [name])
        
        self.trackPop()
    
    def parseOneWordName(self, name, strong):
        self.trackAppend(name, strong, 'parseOneWordName', 'typedef?')
        
        whatis = self.whatis(name)
        assert(len(whatis) == 1)
        whatis = whatis[0]
        
        if whatis.startswith('enum '):
            self.parseEnum(name)
        elif '(' in whatis:
            cnt = whatis.count('(')
            assert(cnt in [1,2] and whatis.count(')') == cnt)
            self.parseFunctionType(name)
        elif '{...}' in whatis:
            impl = self.ptype(name)
            self.parseCombinationDepends(impl)
            impl[0] = 'typedef ' + impl[0]
            impl[-1] += ' ' + name
            self.addName(name, strong, impl)
        else:
            self.parseName(whatis, strong, initTrack=False)
            self.addName(name, strong, ['typedef %s %s' % (whatis, name)])
        
        self.trackPop()
    
    def parseFunctionType(self, name):
        self.trackAppend(name, False, 'parseFunctionType', 'func')
        
        impl = self.ptype(name + '*')
        self.parseFunctionTypeDepends(impl)
        
        arr = impl[0].replace('(','\t').replace(')','\t').split('\t')
        assert(len(arr) == 5)
        retType,stars,args = arr[0],arr[1].count('*')-1,arr[3]
        
        if len(name.split()) == 1:
            self.addName(name, True, ['typedef %s (%s%s)(%s)' % (
                retType, '*'*stars, name, args)])
        
        self.trackPop()
    
    def parseFunctionTypeDepends(self, impl):
        if len(impl) != 1:
            raise Exception('\n'.join(
                ['Error: *** len(%s) != 1' % str(impl)] +
                [self.track2str()]))
        impl = impl[0]
        
        while impl.strip().endswith(';'):
            impl = impl.strip()[:-1]
        impl = impl.strip()
        
        assert(impl.count('(') == 2)
        arr = impl.replace('(','\t').replace(')','\t').split('\t')
        retType,args = arr[0],arr[3]
        
        depends = []
        for dep in args.split(',') + [retType]:
            strong = True
            if dep.endswith('*'):
                strong = False
                while dep.endswith('*'):
                    dep = dep[:-1].strip()
            assert('(' not in dep)
            depends.append([dep,strong])
        
        for pair in depends:
            self.parseName(pair[0],pair[1], initTrack=False)
    
    def parseEnum(self, name):
        impl = self.ptype(name)
        
        details = self.parseEnumDetails(impl)
        if name not in details['members'] and len(name.split()) == 1:
            impl = ['typedef %s %s' % (impl[0], name)]
        
        names = [name] + details['members']
        if details['name']:
            names += ['enum ' + details['name']]
        
        self.addName(names, True, self.prettyEnum(impl))
    
    def prettyEnum(self, impl):
        if len(impl) != 1:
            print(impl)
        assert(len(impl) == 1)
        if len(impl[0]) > 60:
            impl = impl[0]
            impl = impl.replace('{',  '{\n\t')
            impl = impl.replace(', ', ',\n\t')
            impl = impl.replace('}',    '\n}')
            impl = [impl]
        return impl
    
    def parseEnumDetails(self, impl):
        assert(len(impl) == 1)
        assert(impl[0].count('{') == 1 and impl[0].count('}') == 1)
        
        arr = impl[0].replace('{','\t').replace('}','\t').split('\t')
        arr = [s.strip() for s in arr]
        assert(len(arr) == 3 and arr[2] == '')
        
        if arr[0] == 'enum':
            name = ''
        else:
            name = arr[0].split()[1]
        
        return {
            'name'    : name,
            'members' : [s.strip().split('=')[0] for s in arr[1].split(',')],
        }
    
class Main(object):
    def __init__(self, crash, argv):
        self.parser = CTypeParser(crash)
        self.argv = argv
    
    def run(self):
        self.parseArgs()
        if not self.updated():
            self.parseCTypes()
            self.dumpCTypes()
        else:
            print('Info: %s is updated' % self.outfile)
    
    def parseArgs(self):
        if len(self.argv) == 1:
            self.usage()
        elif len(self.argv) > 2:
            self.usage(rc=-1)
        
        infile = self.argv[1]
        if not infile.endswith('.in'):
            print('Error: *** file(%s) without suffix .in' % infile)
            self.usage(rc=-1)
        
        self.fname = os.path.basename(infile).split('.in')[0]
        self.path = os.path.dirname(infile)
        if not self.path:
            self.path = '.'
        
        self.macro = '_CRASH_%s_H' % self.fname.upper()
        self.infile = '%s/%s.in' % (self.path, self.fname)
        self.outfile = '%s/%s.h' % (self.path, self.fname)
        self.infilebak = self.infile + '.bak'
        
        if not os.path.exists(self.infile):
            print('Error: *** file(%s) does not exist' % self.infile)
            self.exit(-1)
    
    def parseCTypes(self):
        self.parser.parse(self.infile)
    
    def dumpCTypes(self):
        self.parser.dump(self.macro, self.outfile)
        if os.system('/bin/cp %s %s' % (self.infile, self.infilebak)) == 0:
            self.parser.crash.setFlag(self.fname)
        if not self.updated():
            raise Exception('Dump file but not updated')
    
    def readTypes(self, fname):
        fd = open(fname)
        contents = fd.read().split('\n')
        fd.close()
        
        types = set()
        for typ in contents:
            typ = typ.strip()
            if typ and not typ.startswith('#'):
                types.add(typ)
        return contents
    
    def includesAllTypes(self, infilebak, infile):
        typesBak = self.readTypes(infilebak)
        for typ in self.readTypes(infile):
            if typ not in typesBak:
                return False
        return True
    
    def updated(self):
        if not self.parser.crash.chkFlag(self.fname):
            return False
        
        if not os.path.isfile(self.infilebak):
            return False
        
        return self.includesAllTypes(self.infilebak, self.infile)
    
    def exit(self, rc=0):
        sys.exit(rc)
    
    def usage(self, rc=0):
        print('Usage: extpy %s <inc_header.in>' %
            os.path.basename(self.argv[0]))
        self.exit(rc)
    
if __name__ == '__main__':
    Main(Crash(extpy.crashInstance()), sys.argv).run()
