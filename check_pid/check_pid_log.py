#!/usr/bin/env python3
import re
import sys

def check(path):
    with open(path) as f:
        txt = f.read()

    if 'proc_test PASS' not in txt:
        return False, 'no proc_test PASS'

    alloc = set()
    last = {}          # pid -> 'a' / 'r'
    for ln in txt.splitlines():
        m = re.match(r'allocate pid:\s*(\d+)', ln)
        if m:
            pid = int(m.group(1))
            if pid in alloc:
                return False, f'pid{pid} 重复 allocate'
            if last.get(pid) == 'a':
                return False, f'pid{pid} 连续 allocate'
            alloc.add(pid)
            last[pid] = 'a'
            continue
        m = re.match(r'recycle pid:\s*(\d+)', ln)
        if m:
            pid = int(m.group(1))
            if pid not in alloc:
                return False, f'pid{pid} 未分配就回收'
            if last.get(pid) == 'r':
                return False, f'pid{pid} 连续回收'
            alloc.remove(pid)
            last[pid] = 'r'
    if alloc != {1}:
        return False, f'最终残留 pid {sorted(alloc)}'
    return True, ''

if __name__ == '__main__':
    ok, msg = check('./log/pid_log.txt')
    print('✅ PASS' if ok else f'❌ FAIL: {msg}')
    sys.exit(0 if ok else 1)