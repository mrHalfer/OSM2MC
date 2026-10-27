# -*- coding: utf-8 -*-
"""Common paths of the tMC tests. Everything lies inside tests (except the tMC build, Qt, GDAL and Java).

The machine paths are taken from the environment variables QT_BIN, GDAL_BIN, TMC_EXE, JAVA, TMC_TEST_SHP
or from the optional file tests/local_config.json (not included in the repository) with the same keys."""
import json, os, shutil

TOOLS = os.path.dirname(os.path.abspath(__file__))
TESTS = os.path.dirname(TOOLS)
ROOT = os.path.dirname(TESTS)                       # the project root
RESOURCES = os.path.join(ROOT, 'resources')         # OSM rules, the height table, the enhancer, templates, default.t3d
WORLDS = os.path.join(TESTS, 'worlds')            # generated worlds (zip) and unpacked copies for the servers
DATA = os.path.join(TESTS, 'data')
BUILD = os.path.join(TESTS, 'build')                # harness artifacts (exe, obj)
SERVERS = os.path.join(TESTS, 'servers')
SERVER_DIR = {'26': os.path.join(SERVERS, 'mc26'), '121': os.path.join(SERVERS, 'mc121')}

def _local():
    try:
        with open(os.path.join(TESTS, 'local_config.json'), encoding='utf-8') as f:
            return json.load(f)
    except (OSError, ValueError):
        return {}


_LOCAL = _local()


def _setting(key, default=''):
    return os.environ.get(key) or _LOCAL.get(key) or default


QT_BIN = _setting('QT_BIN')
_TP_GDAL_BIN = os.path.join(ROOT, 'third_party', 'gdal', 'bin')
GDAL_BIN = _setting('GDAL_BIN') or (_TP_GDAL_BIN if os.path.isdir(_TP_GDAL_BIN) else '')        # the GDAL runtime from the project, if it was put there
TMC_EXE = _setting('TMC_EXE') or shutil.which('tMC') or 'tMC.exe'


def find_java():
    cands = [_setting('JAVA'), shutil.which('java') or '']
    for c in cands:
        if c and os.path.exists(c):
            return c
    return 'java'


JAVA = find_java()
os.makedirs(WORLDS, exist_ok=True)


def tool_env():
    """The environment for running tMC.exe / the harness: Qt and GDAL in PATH."""
    env = dict(os.environ)
    env['PATH'] = os.pathsep.join([p for p in (QT_BIN, GDAL_BIN) if p] + [env.get('PATH', '')])
    return env
