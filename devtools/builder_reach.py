# -*- coding: utf-8 -*-
"""builder_reach.py — reachability analysis of ModelBuilder methods (for cleaning unused code of the builder).
  py -3 builder_reach.py [--prune]   without the flag — only a report; --prune — removes unreachable methods from .cpp and their declarations from .h
Entry points: what is called by other project files (mcscene.cpp, tmcproject.cpp ...)."""
import re, sys, os

ROOT = os.path.join(os.path.dirname(os.path.abspath(__file__)), '..')
CPP = os.path.join(ROOT, 'scene', 'modelbuilder.cpp')
H = os.path.join(ROOT, 'scene', 'modelbuilder.h')
OTHERS = ['scene/mcscene.cpp', 'scene/mcscene.h', 'input/tmcproject.cpp', 'app/tmcrun.cpp', 'input/t3dsettings.cpp', 'app/mainwindow.cpp', 'app/main.cpp']


def strip_code(s):
    """Replaces comments and strings with spaces (the length and line breaks are kept) — for searching brackets/names."""
    out = []
    i, n = 0, len(s)
    while i < n:
        c = s[i]
        if s.startswith('//', i):
            j = s.find('\n', i)
            j = n if j < 0 else j
            out.append(' ' * (j - i)); i = j
        elif s.startswith('/*', i):
            j = s.find('*/', i + 2)
            j = n if j < 0 else j + 2
            out.append(''.join(ch if ch == '\n' else ' ' for ch in s[i:j])); i = j
        elif c == '"' or c == "'":
            j = i + 1
            while j < n and s[j] != c:
                j += 2 if s[j] == '\\' else 1
            j = min(n, j + 1)
            out.append(''.join(ch if ch == '\n' else ' ' for ch in s[i:j])); i = j
        else:
            out.append(c); i += 1
    return ''.join(out)


def functions(src, cls):
    """[(name, start, end)] of the definitions of cls:: methods at the top level (by offsets in src)."""
    code = strip_code(src)
    res = []
    for m in re.finditer(r'^[^\n;{}]*\b%s::(~?\w+)\s*\(' % cls, code, re.M):
        # the body: the first '{' after the closing ')' of the parameter list (taking constructor initializers into account)
        i = code.find('{', m.end())
        if i < 0:
            continue
        # we skip declarations without a body (just in case): there must be no ';' before '{'
        if ';' in code[m.end():i] and '(' not in code[m.end():i].split(';')[0]:
            continue
        depth, j = 0, i
        while j < len(code):
            if code[j] == '{': depth += 1
            elif code[j] == '}':
                depth -= 1
                if depth == 0: break
            j += 1
        start = code.rfind('\n\n', 0, m.start())
        res.append((m.group(1), m.start(), j + 1))
    return res


def main():
    src = open(CPP, encoding='utf-8').read()
    code = strip_code(src)
    funcs = functions(src, 'ModelBuilder')
    names = {f[0] for f in funcs}
    body = {f[0]: code[f[1]:f[2]] for f in funcs}
    # the header: inline methods and declarations
    hsrc = strip_code(open(H, encoding='utf-8').read())
    # entry points — the names of the methods found in other files
    ext = ''
    for fn in OTHERS:
        p = os.path.join(ROOT, fn)
        if os.path.exists(p):
            ext += strip_code(open(p, encoding='utf-8').read())
    entry = {n for n in names if re.search(r'ModelBuilder::%s\b' % n, ext)}
    entry |= {'BuildSingleObjectMesh', 'ExtractLineStrings'}      # ExtractLinePaths — inline in the header, calls ExtractLineStrings
    # inline definitions in .h (e.g. ExtractLinePaths) also call other methods — we take their text into account as the "body" of entry points
    reach, stack = set(), list(entry)
    hinline = hsrc
    while stack:
        n = stack.pop()
        if n in reach: continue
        reach.add(n)
        text = body.get(n, '')
        for other in names:
            if other not in reach and re.search(r'\b%s\b' % other, text):
                stack.append(other)
    dead = sorted(names - reach)
    print('methods in .cpp: %d; reachable: %d; unreachable: %d' % (len(names), len(reach & names), len(dead)))
    sizes = {f[0]: src.count('\n', f[1], f[2]) + 1 for f in funcs}
    print('entry points:', sorted(entry))
    print('unreachable (lines):', ', '.join('%s(%d)' % (n, sizes[n]) for n in dead))
    print('lines in unreachable: %d of %d' % (sum(sizes[n] for n in dead), src.count('\n') + 1))
    if '--prune' in sys.argv:
        # we delete from the end of the file so that the offsets do not shift; together with the preceding comment block
        spans = sorted([(f[1], f[2], f[0]) for f in funcs if f[0] in dead], reverse=True)
        for st, en, n in spans:
            # we include the doxygen/// comments right above the definition
            ls = src.rfind('\n', 0, st) + 1
            while True:
                prev = src.rfind('\n', 0, ls - 1) + 1
                line = src[prev:ls].strip()
                if line.startswith('//') or line.startswith('/*') or line.startswith('*') or line.endswith('*/'):
                    ls = prev
                else:
                    break
            src = src[:ls] + src[en:].lstrip('\n')
        open(CPP, 'w', encoding='utf-8').write(src)
        print('removed from .cpp:', len(spans))


if __name__ == '__main__':
    main()
