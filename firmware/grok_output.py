# 15:11:20.710 [MainThread] DEBUG   thonny.running: RUNNER GOT: ProgramOutput, BackendEvent(cwd='/', data='\r\naddr ffff\r\ndata <26> R - - - - -\r\n\r\naddr ffff\r\ndata <26> R avma lic - - -\r\n\r\nvalid: start: addr f075\r\ndata <26> R avma - - - -\r\nread 26\r\n\r\n', event_type='ProgramOutput', lib_dirs=['/lib'], sequence='BackendEvent', stream_name='stdout', sys_path=['', '.frozen', '/lib']) in state: running
# 15:11:20.934 [MainThread] DEBUG   thonny.running: RUNNER GOT: ProgramOutput, BackendEvent(cwd=None, data='addr 004e\r\n', event_type='ProgramOutput', lib_dirs=None, sequence='BackendEvent', stream_name='stdout', sys_path=None) in state: running
# 15:11:20.936 [MainThread] DEBUG   thonny.running: RUNNER GOT: ProgramOutput, BackendEvent(cwd=None, data='Traceback (most recent call last):\r\n  File "<stdin>", line 131, in <module>\r\n  File "<stdin>", line 91, in GetData\r\nKeyboardInterrupt: \r\n', event_type='ProgramOutput', lib_dirs=None, sequence='BackendEvent', stream_name='stderr', sys_path=None) in state: running
# 15:11:20.943 [MainThread] DEBUG   thonny.running: RUNNER GOT: ProgramOutput, BackendEvent(cwd=None, data='\r\nMPY: soft reboot\r\n', event_type='ProgramOutput', lib_dirs=None, sequence='BackendEvent', stream_name='stdout', sys_path=None) in state: running

import ast
import sys
import re

PROGRAM_OUTPUT = re.compile(r"ProgramOutput, BackendEvent.*data='(.*?)',")

def Process(line):
    m = PROGRAM_OUTPUT.search(line)
    if m:
        data = m.group(1)
        for item in data.split('\\r\\n'):
            if item:
                print(item)

for line in sys.stdin:
    Process(line)
