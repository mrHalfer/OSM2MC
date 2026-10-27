# -*- coding: utf-8 -*-
"""A test of the "folder with shape files" mode on real data (optional: a data directory is needed).

The directory is taken from the variable TMC_TEST_SHP . No directory — SKIP (code 77).
Checks: loading of layers (bldpol, csrpol, rdcpol, rdslin, vegpol, vgbpol), the default settings (default.t3d),
an export without errors, a non-empty result by categories.
"""
import os, re, sys
import tcfg
from tmc_run import run_tmc

SHP = tcfg._setting('TMC_TEST_SHP')


def main():
    if not os.path.isdir(SHP) or not any(f.lower().endswith('.shp') for f in os.listdir(SHP)):
        print('SKIP: there is no directory with shape files (%s); set TMC_TEST_SHP' % SHP)
        return 77
    out = os.path.join(tcfg.WORLDS, 'shp_folder.zip')
    code, log, sec = run_tmc(['.', '--out', out, '--seed', '777'], os.path.join(tcfg.WORLDS, 'shp_folder.log'), cwd=SHP)
    ok = code == 0 and os.path.exists(out)
    print('tMC exit %d, %.1f c' % (code, sec))
    for pat, title in ((r'Layer [^\n]*bldpol: \d+ objects', 'the bldpol layer is loaded'),
                       (r'3D settings: [^\n]*default\.t3d', 'the default settings (default.t3d)'),
                       (r'Objects: \d+ \(skipped: 0\)', 'all objects were exported'),
                       (r'Roads:', 'the result by categories')):
        hit = re.search(pat, log) is not None
        print('  %s %s' % ('ok ' if hit else 'BAD', title))
        ok = ok and hit
    print('TEST shp', 'PASS' if ok else 'FAIL')
    return 0 if ok else 1


if __name__ == '__main__':
    sys.exit(main())
