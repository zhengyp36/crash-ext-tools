#!/usr/bin/python
# -*- coding: utf-8 -*-

import re
import os
import sys
import time
import extpy

class MyExcept(Exception):
    def __init__(self, msg):
        super(type(self),self).__init__(msg)

class Log(object):
    DEBUG    = True
    flashing = False
    lastLen  = 0
    
    @classmethod
    def write(cls, msg, endl=True):
        if endl and not msg.endswith('\n'):
            msg += '\n'
        sys.stdout.write(msg)
        sys.stdout.flush()
    
    @classmethod
    def doLog(cls, msg, flashing=False, ts=True):
        if ts:
            msg = '[%s]%s' % (time.strftime('%Y-%m-%d %H:%M:%S'), msg)
        
        if cls.flashing and not flashing:
            cls.write('\n')
        
        if flashing:
            cls.write('\r' + ' ' * cls.lastLen + '\r' + msg, endl=False)
            cls.lastLen = len(msg)
        else:
            cls.write(msg)
        
        cls.flashing = flashing
    
    @classmethod
    def info(cls, msg, flashing=False, ts=True):
        cls.doLog(msg, flashing, ts)
    
    @classmethod
    def debug(cls, msg, flashing=False, ts=True):
        if cls.DEBUG:
            cls.doLog(msg, flashing, ts)

class OrderedDict(dict):
    def __init__(self):
        self._order = []
        super(type(self),self).__init__()
    
    def _addkey(self, key):
        if key not in self:
            self._order.append(key)
    
    def _delkey(self, key):
        if key in self:
            self._order.remove(key)
    
    def keys(self):
        return self._order[:]
    
    def __iter__(self):
        return iter(self._order)
    
    def __setitem__(self, key, value):
        self._addkey(key)
        super(type(self),self).__setitem__(key,value)
    
    def __delitem__(self, key):
        self._delkey(key)
        super(type(self),self).__delitem__(key)
    
    def pop(self, key):
        self._delkey(key)
        super(type(self),self).pop(key)

class Crash(object):
    def __init__(self, crash):
        self.crash = crash
        self.cache = {}
    
    def run(self, cmd, echo=False):
        ok,output = self.crash.run(cmd)
        output = self.strip(output)
        if not ok:
            raise MyExcept('\n\t'.join(
                ['\n\tRun crash-cmd {%s} error:' % cmd] + output
            ))
        return output
    
    @classmethod
    def strip(self, arr):
        while arr:
            if not arr[0]:
                arr = arr[1:]
            elif not arr[-1]:
                arr = arr[:-1]
            else:
                break
        return arr
    
    def parseTypeImpl(self, cmd):
        output = self.run(cmd)
        output[0] = output[0].split('type = ')[1].strip()
        return output
    
    def parseType(self, cmd, cache):
        if not cache:
            return self.parseTypeImpl(cmd)
        else:
            if cmd not in self.cache:
                self.cache[cmd] = self.parseTypeImpl(cmd)
                Log.debug('parsing ctypes: %d' % len(self.cache), flashing=True)
            return self.cache[cmd][:]
    
    def whatis(self, name, cache=True):
        return self.parseType('whatis ' + name, cache)
    
    def ptype(self, name, cache=True):
        return self.parseType('ptype ' + name, cache)
    
    def opMods(self, op, mods):
        if isinstance(mods,str):
            mods = [mods]
        
        opt = {
            'Load'   : '-s',
            'Remove' : '-d',
        }[op]
        
        for mod in mods:
            Log.info('%s %s ...' % (op, mod))
            self.run('mod %s %s' % (opt, mod))
    
    def loadMods(self, mods):
        self.opMods('Load', mods)
    
    def removeMods(self, mods):
        self.opMods('Remove', mods)

class utils(object):
    @classmethod
    def cut(cls, str, sep, off=0, reverse=0):
        #
        # Examples:
        #   cut('int x; int y;', ';') => ['int x', '; int y;']
        #   cut('int **x', '*', reverse=1, off=1) => ['int **', 'x']
        #
        i = { 0:str.find, 1:str.rfind }[reverse](sep)
        return [str[:i+off].strip(), str[i+off:].strip()]
    
    @classmethod
    def rcut(cls, str, sep, off=1):
        return cls.cut(str, sep, off=1, reverse=1)
    
    class fun(object):
        @classmethod
        def split(cls, s):
            #
            # Formats of string s and result of splitting:
            #   returnType (argList)
            #       => [returnType, [argList]]
            #   returnType (*)(argList)
            #       => [returnType, [*], [argList]]
            #   returnType (*name)(argList)
            #       => [returnType, [*name], [argList]]
            #   returnType (*name[cnt])(argList)
            #       => [returnType, [*name[cnt]], [argList]]
            #
            # That is:
            #   Every '(xxx)' makes a new group i.e. List ['xxx']
            #
            i,j,r = 0,0,[]
            curr,stack = r,[r]
            
            while j < len(s):
                if s[j] == '(':
                    # Push string s[i:j] into curr group
                    if i < j:
                        curr.append(s[i:j])
                    i = j + 1
                    
                    # Generate a new group and push it into curr group
                    newGrp = []
                    curr.append(newGrp)
                    stack.append(newGrp)
                    
                    # Switch to newGrp
                    curr = newGrp
                
                elif s[j] == ')':
                    # The ')' does not match any '('
                    if len(stack) < 2:
                        return []
                    
                    # Push string s[i:j] into curr group
                    if i < j:
                        curr.append(s[i:j])
                    i = j + 1
                    
                    # Current group done, pop curr group and switch to the prev
                    stack.pop()
                    curr = stack[-1]
                
                j += 1
            
            # Push the last string s[i:j] into curr group
            if i < j:
                curr.append(s[i:j])
            
            # Some '(' have no ')' to match
            if curr != r:
                return []
            
            return r
        
        @classmethod
        def join(cls, r):
            # r is a splitting-result from utils.fun.split()
            s = ''
            for i in r:
                if isinstance(i,str):
                    s += i
                else:
                    s += '(%s)' % cls.join(i)
            return s
        
        @classmethod
        def unpack(cls, arr, strip=True):
            # r is a splitting-result from utils.fun.split()
            result = []
            
            skip = {
                False : (lambda s : False),
                True  : (lambda s : s[0] in ['.','*','[']),
            }[strip]
            
            assert(isinstance(arr,list))
            for s in arr:
                if isinstance(s,str):
                    for s in s.split(','):
                        s = s.strip()
                        if s and not skip(s):
                            result.append(s)
                else:
                    result += cls.unpack(s)
            
            return result

class CType(object):
    TYPES = {
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
        'struct'    : 'combi',
        'union'     : 'combi',
        'enum'      : 'combi',
        
        # Alias types: defined by typedef
        #<one-word> : 'alias'
        #<*(*)(*)>  : 'function'
    }
    
    SUB_TYPES = ['struct', 'union', 'enum']
    SPECIALS = ['const', 'volatile', 'register']
    EXCEPTION_LINES = ['<no data fields>']
    SEPARATOR = '/+/+/+/+/+/+/+/'
    
    parser = None
    
    @classmethod
    def setParser(cls, parser):
        assert(isinstance(parser, CTypeParser))
        cls.parser = parser
    
    @classmethod
    def make(cls, line='',
        hasVarName=False, strong=True, desc=None, resolve=True, skip=False):
        
        assert(bool(line) == (not desc))
        if line:
            if isinstance(line,list):
                assert(len(line) == 1)
                s = line[0]
            else:
                s = line
            desc = cls.parse(s, hasVarName, strong)
        assert(cls.isNormalDesc(desc))
        
        if skip:
            desc['skip'] = True
        
        inst = cls(desc)
        if resolve:
            inst.resolve()
        
        return inst
    
    @classmethod
    def genCodes(cls):
        cls.parser.clrCodes()
        cls.codeStart()
        
        for ctype in cls.parser.ctypes:
            cls.parser.ctypes[ctype].doCode()
        
        cls.codeEnd()
        return cls.parser.getCodes()
    
    @classmethod
    def newDesc(cls):
        class DescDepend(dict):
            def add(self, desc):
                assert(cls.isNormalDesc(desc))
                self[desc['name']] = desc
        
        return {
            'type'    : '',
            'subType' : '',
            'name'    : '',
            'strong'  : True,
            'skip'    : False,
            'impl'    : [],           # implement itself
            'decl'    : [],           # declare new varibles
            'depend'  : DescDepend(), # save desc
        }
    
    @classmethod
    def parse(cls, lines, hasVarName, strong, skip=False):
        #
        # Case 1 Parse Single-Line (normal: not-function)
        # ----------------------------------------------------------------------
        #   Format : const CTypeName **varName[num1][num2];
        #   Output : mytype=(CTypeName,strong),decl,depend=[]
        # 
        # Case 2 Parse Single-Line (function)
        # ----------------------------------------------------------------------
        #   Format : returnType (*funcName[num1][num2])(argList);
        #   Output : mytype=(),decl,depend=[(CTypeName,strong)]
        #
        # Case 3 Parse Single-Line (enum)
        # ----------------------------------------------------------------------
        #   Format : const enum enumName {E_VAL1, E_VAL2} *varName[num1][num2];
        #   Output : mytype=(),decl,depend=[]
        #
        # Case 4 Parse Multiple-Lines
        # ----------------------------------------------------------------------
        #   Format : const struct stName {
        #                // ...
        #            } *varName[num1][num2];
        #   Output : mytype=(),decl,depend=[(CTypeName,strong)]
        #
        if isinstance(lines,str):
            lines = [lines]
        
        if len(lines) != 1:
            # parse: alias(typedef) or struct/union-implement
            return cls.parseMultiLine(lines, hasVarName=False, strong=True)
        elif '{' in lines[0]:
            # parse: alias(typedef) or enum-implement
            return cls.parseEnum(lines[0], hasVarName=False, strong=True)
        elif '(' in lines[0]:
            # parse: alias(typedef) or dependancies of struct/union-implement
            return cls.parseFunction(lines[0], hasVarName, strong=True)
        else:
            # parse: inFile(builtin/struct/union/enum/alias) or alias(typedef)
            #        or dependancies of struct/union-implement
            return cls.parseNormal(lines[0], hasVarName, strong)
    
    @classmethod
    def parseNormal(cls, line, hasVarName, strong, decl=True):
        if ';' in line:
            line = line.split(';')[0]
        
        desc = cls.newDesc()
        desc['strong'] = strong and '*' not in line
        
        if '[' in line:
            line,cArray = utils.cut(line,'[')
        else:
            cArray = ''
        
        array = line.split()
        if hasVarName:
            if '*' in array[-1]:
                array[-1] = utils.rcut(array[-1],'*')[0]
            else:
                array = array[:-1]
        
        if decl:
            if array[-1].endswith('*'):
                sep = ''
            else:
                sep = ' '
            desc['decl'] = [' '.join(array) + sep + cls.SEPARATOR + cArray]
            assert(desc['decl'])
        
        array = ' '.join(array).replace('*',' ').split()
        desc['name'] = ' '.join([i for i in array if i not in cls.SPECIALS])
        assert(desc['name'])
        
        cls.setTypeByName(desc)
        return desc
    
    @classmethod
    def parseFunction(cls, line, hasVarName, strong=True, decl=True):
        assert(strong)
        
        desc = cls.newDesc()
        desc['type'] = 'function'
        
        if ';' in line:
            line = line.split(';')[0]
        
        r = utils.fun.split(line)
        if len(r) == 2:
            r = [r[0],[],r[1]]
        
        assert(len(r) == 3)
        assert(isinstance(r[0],str) and r[0].strip())
        assert(isinstance(r[1],list) and (len(r[1]) in [0,1]))
        assert(isinstance(r[2],list))
        
        returnType,argList = r[0].strip(),r[2]
        
        if decl:
            if r[1]:
                midstr = r[1][0].strip()
                
                assert(midstr.startswith('*'))
                stars,midstr = utils.rcut(midstr, '*')
                
                if '[' in midstr:
                    funcName,cArray = utils.cut(midstr,'[')
                else:
                    funcName,cArray = midstr,''
                
                assert(bool(hasVarName) == bool(funcName))
                
                desc['decl'] = ['%s (%s%s%s)(%s)' % (
                    returnType,
                    stars, cls.SEPARATOR, cArray,
                    utils.fun.join(argList))]
            else:
                desc['decl'] = ['%s %s(%s)' % (
                    returnType,
                    cls.SEPARATOR,
                    utils.fun.join(argList))]
        
        for s in utils.fun.unpack(argList) + [returnType]:
            desc['depend'].add(cls.parseNormal(s,
                hasVarName=False, strong=True, decl=False))
        
        return desc
    
    @classmethod
    def parseEnum(cls, line, hasVarName=False, strong=True):
        assert(strong and not hasVarName)
        assert('enum ' in line or 'enum{' in line)
        assert(line.count('{') == line.count('}') == 1)
        
        # Just for its declare-format
        desc = cls.newDesc()
        desc['decl'] = [cls.parseCombiDecl(line, hasVarName)]
        return desc
    
    @classmethod
    def parseMultiLine(cls, lines, hasVarName, strong):
        assert(len(lines) >= 2 and
            lines[0].count('{') == lines[-1].count('}') == 1)
        tmpDecl = lines[:-1] + [cls.parseCombiDecl(lines[-1], hasVarName)]
        
        desc = cls.newDesc()
        for line in tmpDecl:
            if line.strip() not in cls.EXCEPTION_LINES:
                desc['decl'].append(line)
                
                line = line.strip()
                if '(' in line:
                    handler = cls.parseFunction
                elif line and '{' not in line and '}' not in line:
                    handler = cls.parseNormal
                else:
                    handler = None
                
                if handler:
                    tmp = handler(line,hasVarName=True,strong=True,decl=False)
                    if tmp['type'] == 'function':
                        desc['depend'].update(tmp['depend'])
                    else:
                        desc['depend'].add(tmp)
            else:
                for s in cls.EXCEPTION_LINES:
                    if s == line.strip():
                        desc['decl'].append(line.replace(s,'char dummy[1];'))
                        break
        
        return desc
    
    @classmethod
    def parseCombiDecl(cls, line, hasVarName):
        # Format of line: 'xxx } *varName[num1][num2];'
        #   1. Call by cls.parseEnum and 'xxx' should be 'const enum xxx {'
        #   2. Call by cls.parseMultiLine and 'xxx' should be empty
        if ';' in line:
            line = line.split(';')
        
        if '*' in line:
            head,tail = utils.rcut(line, '*')
        else:
            head,tail = utils.rcut(line, '}')
        
        if '[' in tail:
            varName,tail = utils.cut(tail, '[')
        else:
            varName = ''
        
        assert(bool(varName) == bool(hasVarName))
        
        if head.endswith('*'):
            decl = '%s%s%s' % (head, cls.SEPARATOR, tail)
        else:
            assert(head.endswith('}'))
            decl = '%s %s%s' % (head, cls.SEPARATOR, tail)
        
        return decl
    
    @classmethod
    def setTypeByName(cls, desc):
        array = desc['name'].split()
        assert(array)
        
        if array[0] in cls.TYPES:
            desc['type'] = cls.TYPES[array[0]]
            if desc['type'] == 'combi':
                assert(len(array) == 2 and array[0] in cls.SUB_TYPES)
                desc['subType'] = array[0]
        else:
            assert(len(array) == 1)
            desc['type'] = 'alias'
    
    @classmethod
    def isNormalDesc(cls, desc):
        if desc['type'] not in ['base','builtin','combi','alias']:
            return False
        
        if desc['type'] == 'combi':
            if desc['subType'] not in cls.SUB_TYPES:
                return False
        elif desc['subType']:
                return False
        
        if not desc['name']:
            return False
        
        return True
    
    def __init__(self, desc):
        self.desc = desc
    
    def resolve(self):
        assert(isinstance(self.parser, CTypeParser))
        if not self.added():
            if not self.desc['impl']:
                self.doResolve()
            self.add()
        return self
    
    @classmethod
    def strongStr(self, strong):
        return {True:'strong',False:'weak'}[(not not strong)]
    
    @classmethod
    def tagSpecial(cls, type):
        return '<%s>' % type
    
    def tag(self, strong=None):
        if strong is None:
            strong = self.desc['strong']
        
        if self.desc['subType'] in self.SUB_TYPES:
            return ':'.join([self.desc['name'].replace(' ',':'),
                self.strongStr(strong)])
        elif self.desc['type'] == 'alias':
            return ':'.join(['alias',self.desc['name'],self.strongStr(strong)])
        else:
            assert(self.desc['type'] in ['base','builtin'])
            return self.tagSpecial(self.desc['type'])
    
    def added(self):
        if self.tag(strong=True) in self.parser.ctypes:
            return True
        elif (not self.desc['strong'] and
            self.tag(strong=False) in self.parser.ctypes):
            return True
        else:
            return False
    
    def add(self):
        if not self.added():
            self.parser.ctypes[self.tag()] = self
    
    def doResolve(self):
        if self.desc['skip']:
            self.desc['impl'] = ['<skip>']
        elif self.desc['type'] in ['base','builtin']:
            self.desc['impl'] = ['<%s>' % self.desc['type']]
        elif self.desc['type'] == 'alias':
            self.resolveAlias()
        else:
            assert(self.desc['type'] == 'combi')
            self.resolveCombi()
        self.resolveDepends()
    
    def resolveAlias(self):
        assert(self.desc['name'])
        whatis = self.parser.crash.whatis(self.desc['name'])
        assert(len(whatis) == 1)
        whatis = whatis[0]
        
        if '{...}' in whatis:
            subType = utils.cut(whatis,'{')[0].split()[-1]
            assert(subType in self.SUB_TYPES)
            
            lines = self.parser.crash.ptype(self.desc['name'])
            desc = self.parse(lines, hasVarName=False, strong=True)
            self.desc['depend'].update(desc['depend'])
        else:
            desc = self.parse(whatis,
                hasVarName=False, strong=self.desc['strong'])
            if '(' not in whatis:
                assert(not self.desc['depend'])
                assert(self.isNormalDesc(desc))
                assert(not desc['impl'])
                self.desc['depend'].add(desc)
            else:
                self.desc['depend'].update(desc['depend'])
        
        self.desc['impl'] = self.doDeclare(desc,
            self.desc['name'], typedef=True)
    
    def resolveCombi(self):
        if self.desc['strong']:
            lines = self.parser.crash.ptype(self.desc['name'])
            if self.desc['subType'] == 'enum':
                assert(len(lines) == 1)
            else:
                assert(len(lines) >= 2)
            
            desc = self.parse(lines, hasVarName=False, strong=True)
            self.desc['depend'].update(desc['depend'])
            self.desc['impl'] = self.doDeclare(desc)
        else:
            self.desc['impl'] = [self.desc['name'] + ';']
    
    def resolveDepends(self):
        #
        # typedef ignores weak struct/union/enum such as
        # -----------------------------------------------
        #   /* struct spa; */ /* This line is omitted */
        #   typedef struct spa spa_t;
        # -----------------------------------------------
        #
        if self.desc['type'] == 'alias' and len(self.desc['depend']) == 1:
            desc = self.desc['depend'].values()[0]
            if desc['type'] == 'combi' and not desc['strong']:
                return
        
        for desc in self.desc['depend'].values():
            if desc['name'] != self.desc['name']:
                self.make(desc=desc)
            else:
                assert(not desc['strong'])
                assert(desc['subType'] in ['struct','union'])
    
    @classmethod
    def doDeclare(cls, desc, name='', typedef=False):
        assert(desc['decl'])
        decl = [i.replace(cls.SEPARATOR,name) for i in desc['decl']]
        
        if typedef:
            decl[0] = 'typedef ' + decl[0]
        
        if 'enum' in decl[0].split() and '{' in decl[0]:
            decl = cls.prettyEnum(decl)
        
        decl[-1] = decl[-1].strip()
        if not decl[-1].strip().endswith(';'):
            decl[-1] += ';'
        
        return decl
    
    @classmethod
    def prettyEnum(cls, decl, maxLen=60):
        # Format of line: 'enum  xxx { xxx,  xxx,  xxx }  xxx'
        if isinstance(decl,list):
            line = ''.join(decl)
        else:
            line = decl
        
        assert(cls.SEPARATOR not in line)
        array = line.replace('{',cls.SEPARATOR).replace('}',
            cls.SEPARATOR).split(cls.SEPARATOR)
        assert(len(array) == 3)
        
        head = ' '.join(array[0].split() + ['{'])
        mid  = ', '.join([i.strip() for i in array[1].split(',')])
        tail = ' '.join(['}'] + array[2].split())
        line = ''.join([head,mid,tail])
        
        if len(line) > maxLen:
            return line.replace('{','{\n\t').replace('}','\n}').replace(', ',
                ',\n\t').split('\n')
        else:
            return [line]
    
    def doCode(self):
        tag = self.tag()
        if tag == self.tagSpecial('builtin'):
            self.parser.addCodes(self.tag(), self.codeBuiltin())
        elif 0 <= tag.find('<') < tag.find('>'):
            pass
        else:
            self.parser.addCodes(self.tag(), self.codeByImpl())
    
    @classmethod
    def codeStart(cls, outFileName=''):
        if not outFileName:
            outFileName = 'crash_auto_head.h'
        macro = '_CRASH_AUTO_HEAD_%s_' % outFileName.replace('.','_').upper()
        
        cls.parser.addCodes(cls.tagSpecial('file.head'), [
            '#ifndef ' + macro,
            '#define ' + macro
        ])
        
        if cls.tagSpecial('builtin') in cls.parser.ctypes:
            cls.parser.ctypes[cls.tagSpecial('builtin')].doCode()
        
        cls.parser.addCodes(cls.tagSpecial('c++.head'), [
            '#ifdef __cplusplus',
            'extern "C" {',
            '#endif',
        ])
    
    @classmethod
    def codeEnd(cls, outFileName=''):
        if not outFileName:
            outFileName = 'crash_auto_head.h'
        macro = '_CRASH_AUTO_HEAD_%s_' % outFileName.replace('.','_').upper()
        
        cls.parser.addCodes(cls.tagSpecial('c++.tail'), [
            '#ifdef __cplusplus',
            '} // extern "C" ',
            '#endif',
        ])
        
        cls.parser.addCodes(cls.tagSpecial('file.tail'), [
            '#endif // ' + macro,
        ])
    
    def codeBuiltin(self):
        codes = []
        codes.append('#include <stddef.h>')
        codes.append('#include <stdint.h>')
        return self.codesWithMacro('CRASH_HEAD_BUILTIN_INCLUDED', codes)
    
    def codeByImpl(self):
        if self.desc['skip'] or self.desc['type'] in ['base','builtin']:
            return []
        else:
            assert(self.desc['impl'])
            macro = 'CRASH_HEAD_' + self.tag().replace(' ',
                '_').replace(':','_').upper()
            return self.codesWithMacro(macro, self.desc['impl'])
    
    @classmethod
    def codesWithMacro(cls, macro, codes):
        result = []
        result.append('#ifndef ' + macro)
        result.append('#define ' + macro)
        result += codes
        result.append('#endif // ' + macro)
        return result

class CTypeParser(object):
    def __init__(self, crash):
        assert(isinstance(crash, Crash))
        self.crash = crash
        self.ctypes = OrderedDict()
        self.codes = OrderedDict()
    
    def clrCodes(self):
        self.codes = OrderedDict()
    
    def addCodes(self, tag, codes):
        assert(tag == '<builtin>' or tag not in self.codes)
        self.codes[tag] = codes
    
    def getCodes(self):
        codes = []
        for tag in self.codes:
            codes += self.codes[tag]
            codes.append('')
        return codes
    
    def parse(self, inFile):
        if not inFile.endswith('.in'):
            Log.info("Error: *** inFile(%s) does not endwith '*.in'" % inFile)
            sys.exit(-1)
        
        outFile = inFile[:-3] + '.h'
        
        CType.setParser(self)
        Log.info('Parsing %s ...' % inFile)
        fd = open(inFile)
        lines = fd.read().split('\n')
        fd.close()
        
        for line in lines:
            line = line.strip()
            if line.startswith('#'):
                line = line.split('#')[1].strip()
                if line.startswith('module'):
                    self.parseModules(line)
            elif line:
                self.parseCTypes(line)
        
        Log.info('Generate %s ...' % outFile)
        codes = CType.genCodes()
        
        fd = open(outFile, 'w')
        fd.write('\n'.join(codes))
        fd.close()
        
        Log.info('Write into %s done.' % outFile)
    
    def parseModules(self, line):
        grp = re.search('^([+-]{0,1})\s*module\s*:\s*(.*)', line)
        if grp:
            op,mods = grp.groups()
            {
                ''  : self.crash.loadMods,
                '+' : self.crash.loadMods,
                '-' : self.crash.removeMods,
            }[op](mods.replace(',',' ').split())
    
    def parseCTypes(self, line):
        op,cTypeLine = re.search('^([+-]{0,1})\s*(.*)', line).groups()
        skip = {
            ''  : False,
            '+' : False,
            '-' : True,
        }[op]
        Log.info('%s %s ...' % ({False:'Parsing',True:'Skip'}[skip], cTypeLine))
        CType.make(cTypeLine, skip=skip)

class Main(object):
    @classmethod
    def run(cls):
        crash = Crash(extpy.crashInstance())
        
        usage = 'Usage: extpy %s <*.in>' % os.path.basename(sys.argv[0])
        if len(sys.argv) == 1:
            print(usage)
            sys.exit(0)
        elif len(sys.argv) > 2:
            print('Error: *** arguments too many')
            print(usage)
        else:
            CTypeParser(crash).parse(sys.argv[1])

if __name__ == '__main__':
    Main.run()
