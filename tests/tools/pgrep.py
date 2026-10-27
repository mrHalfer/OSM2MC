import re, sys
pat = re.compile(sys.argv[1])
path = sys.argv[2]
for i, line in enumerate(open(path, encoding='utf-8', errors='replace'), 1):
    if pat.search(line):
        print(i, line.rstrip()[:160])
