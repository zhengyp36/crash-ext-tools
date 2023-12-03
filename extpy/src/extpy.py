# -*- coding: utf-8 -*-
import os
import sys
import time
import socket

#
# [WORKSPACE]
#   CRASH_EXTEND_PYTHON_WORKSPACE = /tmp/crash_extpy_<pid>
#   ${CRASH_EXTEND_PYTHON_WORKSPACE}/extpy.socket
#   ${CRASH_EXTEND_PYTHON_WORKSPACE}/crash.stdout
#
# [COMMANDS]
#   CMD  : crash-command
#   ACK  : OK or ERR
#   STOP : STOP
#

class ExtPy(object):
    DEBUG = False
    
    def debug(self, msg):
        if type(self).DEBUG:
            print('Debug: ' + msg)
    
    def error(self, msg):
        print('Error: *** ' + msg)
        sys.exit(1)
    
    def dump(self, output, limit=-1):
        lineNo = 0
        for line in output:
            lineNo += 1
            if limit == -1 or lineNo <= limit:
                print(line)
            else:
                break
    
    def __init__(self):
        wsKey = 'CRASH_EXTEND_PYTHON_WORKSPACE'
        if wsKey not in os.environ:
            error(wsKey + ' is not set')
        
        self.workspace = os.environ[wsKey]
        self.sockf     = self.workspace + '/extpy.socket'
        self.stdout    = self.workspace + '/crash.stdout'
        self.conn      = None
    
    def __del__(self):
        self.stopServer()
        self.debug('ExtPy Server is destroying.')
    
    def cache(self, relative_path=''):
        if relative_path:
            return self.workspace + '/' + relative_path
        else:
            return self.workspace
    
    def startServer(self):
        self.debug('ExtPy Server is starting.')
        os.system('rm -rf ' + self.sockf)
        
        self.server = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
        self.server.bind(self.sockf)
        self.server.listen(1)
        
        self.debug('Wait crash-process to connect...')
        self.conn, self.addr = self.server.accept()
        self.debug('Crash-process is connected.')
    
    def execCmd(self, cmd):
        cmd = cmd.strip()
        if not cmd:
            print('Error: *** command is null')
            return [False,[]]
        
        if cmd == 'extpy' or cmd.startswith('extpy '):
            print('Error: *** extpy is not support in py-script')
            return [False,[]]
        
        self.debug('Execute command: ' + cmd + ' ...')
        if not self.conn:
            error('No crash-process is connected.')
        
        os.system('echo -n > %s' % self.stdout)
        self.conn.send(cmd)
        
        ack = self.conn.recv(1024)
        if ack not in [ 'OK', 'ERR' ]:
            error('Receive invalid ack(' + ack + ')')
        ok = {'OK':True,'ERR':False}[ack]
        
        fd = open(self.stdout)
        contents = fd.read().split('\n')
        fd.close()
        
        return [ok,contents]
    run = execCmd
    
    def stopServer(self):
        if self.conn:
            self.execCmd('STOP')
            self.conn.close()
            self.conn = None
            self.debug('ExtPy Server is stopped.')

def crashInstance():
    instance = ExtPy()
    instance.startServer()
    time.sleep(0.2)
    return instance
