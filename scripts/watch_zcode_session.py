#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
监督指定 zcode (ZCode CLI) 会话是否"还活着并在干活"。

判定口径（三重证据，任一活跃即认为在干活）：
  1. ZCode 进程存在（session_runtime_presence.process_id 对应进程存活）
  2. session.time_updated 距现在 < STALE_MS（默认 20 分钟）
  3. rollout jsonl 的 mtime 距现在 < STALE_MS

用法：
  python scripts/watch_zcode_session.py                 # 检查默认会话，人类可读输出
  python scripts/watch_zcode_session.py --json          # JSON 输出（供 automation 解析）
  python scripts/watch_zcode_session.py --stale-min 20  # 自定义停滞阈值

退出码：0=活跃；2=停滞/疑似死亡；1=脚本错误（如 db 打不开）。
仅供监督/报告，不直接写会话状态文件——干预由调用方（automation）决定。
"""
import argparse
import datetime
import json
import os
import sys
import time

SID = "sess_4b95a939-edea-43db-a9cc-41aadad9529d"
DB = r"C:\Users\Matrixtime\.zcode\cli\db\db.sqlite"
ROLLOUT = os.path.join(
    os.environ.get("USERPROFILE", r"C:\Users\Matrixtime"),
    ".zcode", "cli", "rollout",
    f"model-io-{SID}.jsonl",
)


def process_alive(pid: int) -> bool:
    """Windows: 用 tasklist 查 PID 是否存在（不依赖 psutil）。"""
    if not pid:
        return False
    try:
        import subprocess
        out = subprocess.run(
            ["tasklist", "/FI", f"PID eq {pid}", "/FO", "CSV", "/NH"],
            capture_output=True, text=True, timeout=15,
        ).stdout
        return str(pid) in out and "INFO:" not in out.upper()
    except Exception:
        return False


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--json", action="store_true")
    ap.add_argument("--stale-min", type=int, default=20)
    args = ap.parse_args()

    now = time.time()
    stale_s = args.stale_min * 60
    res = {
        "session_id": SID,
        "checked_at": datetime.datetime.now().strftime("%Y-%m-%d %H:%M:%S"),
        "stale_threshold_min": args.stale_min,
    }

    # 1) 数据库：会话时间 + 进程
    try:
        import sqlite3
        con = sqlite3.connect(f"file:{DB}?mode=ro", uri=True)
        con.row_factory = sqlite3.Row
        row = con.execute(
            "SELECT time_updated, last_outcome, title FROM session WHERE id=?", (SID,)
        ).fetchone()
        pres = con.execute(
            "SELECT process_id FROM session_runtime_presence WHERE session_id=?", (SID,)
        ).fetchone()
        con.close()
    except Exception as e:
        res["error"] = f"db: {e}"
        print(json.dumps(res, ensure_ascii=False) if args.json else f"[ERROR] {res['error']}")
        return 1

    if not row:
        res["error"] = "session row not found"
        print(json.dumps(res, ensure_ascii=False) if args.json else "[ERROR] 会话不存在")
        return 1

    pid = int(pres["process_id"]) if pres and pres["process_id"] else 0
    updated_ms = int(row["time_updated"])
    age_s = max(0.0, now - updated_ms / 1000.0)
    db_alive = process_alive(pid)

    res["title"] = row["title"]
    res["last_outcome"] = row["last_outcome"]
    res["process_id"] = pid
    res["process_alive"] = db_alive
    res["db_age_sec"] = round(age_s, 1)

    # 2) rollout 文件 mtime
    if os.path.exists(ROLLOUT):
        roll_age = now - os.path.getmtime(ROLLOUT)
        res["rollout_age_sec"] = round(roll_age, 1)
    else:
        res["rollout_age_sec"] = None

    # 3) 综合判定
    fresh = age_s < stale_s
    rollout_fresh = (res["rollout_age_sec"] is not None
                     and res["rollout_age_sec"] < stale_s)
    active = db_alive and (fresh or rollout_fresh)
    res["active"] = active
    res["verdict"] = (
        "ACTIVE" if active else
        "STALLED" if db_alive else
        "DEAD"
    )

    if args.json:
        print(json.dumps(res, ensure_ascii=False, indent=2))
    else:
        mark = "OK" if active else "!!"
        print(f"[{mark}] {res['verdict']}  {res['title']}")
        print(f"     进程 {pid}: {'存活' if db_alive else '不存在'}")
        print(f"     DB 更新于 {age_s/60:.1f} 分钟前 (last_outcome={res['last_outcome']})")
        if res["rollout_age_sec"] is not None:
            print(f"     rollout 更新于 {res['rollout_age_sec']/60:.1f} 分钟前")
        print(f"     检查时间 {res['checked_at']}")

    return 0 if active else 2


if __name__ == "__main__":
    sys.exit(main())
