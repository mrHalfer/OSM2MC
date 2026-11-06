# A table of the [mcx] step timings (MCX_LOG_STEPS) over one or more export logs.
# Usage: python steps_report.py log1.txt [log2.txt ...]
# Each [mcx] line reads "[mcx] HH:MM:SS <name> <ms> ms", where <ms> is the time SINCE the previous
# [mcx] line (so a step's cost rides on the NEXT line). Repeated step names are summed.
# With several logs the columns are compared: the LAST log is flagged when a step costs
# >1000 ms and >1.5x more than in the FIRST one (a regression candidate, see .agents/build-test.md).
import re, sys


def load(path):
    steps = {}
    order = []
    for line in open(path, encoding='utf-8', errors='replace'):
        m = re.search(r'\[mcx\] \d\d:\d\d:\d\d (.+?) (\d+) ms', line)
        if m:
            name, ms = m.group(1).strip(), int(m.group(2))
            if name not in steps:
                steps[name] = 0
                order.append(name)
            steps[name] += ms
    return steps, order


if len(sys.argv) < 2:
    print('usage: steps_report.py log1.txt [log2.txt ...]')
    sys.exit(2)
logs = [load(p) for p in sys.argv[1:]]
shorts = [p.replace('\\', '/').split('/')[-1] for p in sys.argv[1:]]

for p, (steps, _), short in zip(sys.argv[1:], logs, shorts):
    total = sum(steps.values())
    print(f'=== {short}  total {total} ms ===')
    for name, ms in sorted(steps.items(), key=lambda kv: -kv[1])[:12]:
        if ms >= 50:
            print(f'  {ms:7}  {name}')
    print()

names = list(dict.fromkeys([n for _, order in logs for n in order]))
head = f'{"step":32}' + ''.join(f'{s:>13}' for s in shorts)
print(head)
tots = [0] * len(logs)
flagged = []
for n in names:
    vals = [st.get(n, 0) for st, _ in logs]
    for i, v in enumerate(vals):
        tots[i] += v
    note = ''
    if len(vals) > 1 and vals[-1] - vals[0] > 1000 and vals[-1] > max(1, vals[0]) * 1.5:
        note = f'<== {shorts[-1]} is +{vals[-1] - vals[0]} ms vs {shorts[0]}'
        flagged.append(n)
    print(f'{n:32}' + ''.join(f'{v:13}' for v in vals) + ('   ' + note if note else ''))
print(f'{"TOTAL":32}' + ''.join(f'{v:13}' for v in tots))
if len(logs) > 1:
    print('regression candidates:', flagged if flagged else 'none')
