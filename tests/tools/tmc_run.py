# -*- coding: utf-8 -*-
"""Running tMC.exe from tests (a GUI application: we take the log from the --log file)."""
import os, subprocess, time
import tcfg


def run_tmc(args, log_path, env_extra=None, cwd=None, timeout=3600):
    """args — command-line arguments without --nogui/--log. Returns (exit code, log text, seconds)."""
    if not os.path.exists(tcfg.TMC_EXE):
        raise FileNotFoundError('no %s — build tMC (build.bat) or set TMC_EXE' % tcfg.TMC_EXE)
    env = tcfg.tool_env()
    env.update(env_extra or {})
    if os.path.exists(log_path):
        os.remove(log_path)
    t0 = time.time()
    p = subprocess.run([tcfg.TMC_EXE] + list(args) + ['--nogui', '--lang', 'en', '--log', log_path],
                       cwd=cwd or tcfg.WORLDS, env=env, timeout=timeout,
                       stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    text = ''
    if os.path.exists(log_path):
        text = open(log_path, encoding='utf-8', errors='replace').read()
    return p.returncode, text, time.time() - t0
