#!/usr/bin/env python3
import re
import sys
from pathlib import Path

def main(log_path: str):
    text = Path(log_path).read_text(encoding='utf-8', errors='ignore')

    # 1. 收集所有创建的进程及其父进程
    pid2ppid = {}
    for m in re.finditer(r'start pid:\s+(\d+).*?parent.*?(\d+)', text):
        pid = int(m.group(1))
        ppid = int(m.group(2))
        pid2ppid[pid] = ppid

    # 2. 收集所有被回收的僵尸子进程
    reaped = {int(m.group(1)) for m in
              re.finditer(r'Found zombie child pid:\s+(\d+)', text)}

    # 3. 找出从未被回收的僵尸进程
    #    所有创建的进程 - 被回收的进程
    zombie_not_reaped = set(pid2ppid.keys()) - reaped

    if not zombie_not_reaped:
        print("Great: no zombie process left un-reaped.")
        return

    print("Zombie processes NOT reaped (pid -> parent pid):")
    for pid in sorted(zombie_not_reaped):
        print(f"  {pid}  ->  {pid2ppid[pid]}")

if __name__ == '__main__':
    if len(sys.argv) != 2:
        print("Usage: python zombie_check.py <logfile>")
        sys.exit(1)
    main(sys.argv[1])