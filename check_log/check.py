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

    # 3. 从未被回收的僵尸进程
    # zombie_not_reaped = set(pid2ppid.keys()) - reaped
    # 3. 从未被回收的僵尸进程（1 号永不回收，排除）
    zombie_not_reaped = (set(pid2ppid.keys()) - reaped) - {1}
    # 4. 检查是否只剩 pid=1 且 ppid=1
    # final_valid = (len(pid2ppid) == 1 and
    #                pid2ppid.get(1) == 1)

    pass_test = ("proc_test PASS" in text)

    # --- 打印信息 ---
    if not pass_test:
        print("Warning: proc_test did not PASS, the log may be incomplete.")
    else:
        print("proc_test PASS found in log.")

    # if not final_valid:
    #     print("Fail: final state is NOT only root_proc (pid=1, parent=1).")
    # else:
    #     print("Great: final state has only root_proc (pid=1, parent=1).")

    if zombie_not_reaped:
        print("Zombie processes NOT reaped (pid -> parent pid):")
        for pid in sorted(zombie_not_reaped):
            print(f"  {pid}  ->  {pid2ppid[pid]}")
    else:
        print("Great: no zombie process left un-reaped.")

    # 5. 整体判定（用于 shell 脚本 grep）
    if pass_test and not zombie_not_reaped:
        print("RESULT: SUCCESS")
    else:
        print("RESULT: FAIL")

if __name__ == '__main__':
    if len(sys.argv) != 2:
        print("Usage: python check.py <logfile>")
        sys.exit(1)
    main(sys.argv[1])