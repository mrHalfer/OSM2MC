# -*- coding: utf-8 -*-
"""Monte-Carlo of the city events' activation frame over an EXPORTED world (not a run stage).

    py -3 encounter_sim.py <export.zip> [--days=60]

Reads the rules OUT of the exported pack (the hours window of every event, its slot constants, the
search radii, the selector limits and the no-neighbor guard are parsed from the generated mcfunction
files) and the real marker positions out of the exported entities' regions, then replays the cycle
day after day with the same integer arithmetic the scoreboard of the server performs. Prints wakes
per night for every dread tier and every player placement, the clock hours the wake-ups land on, and
the share of each event against the theory (rolls x chance).

It is a simulator of the pack the exporter generates, not of the server: how the server really runs
selectors and scoreboard division is out of its scope (a live playtest or the servers stage checks
that). Nothing is written; the zip is any osmc export made with --encounters.
"""
import random, re, sys, os, zipfile
from collections import defaultdict

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import verify_mc_export as V            # ent_iter: the entities of the exported entities' regions

FN = 'datapacks/osmc_enc/data/osmc_enc/function/'    # the function root inside the export's zip
GUARD_FALLBACK = 20                     # only if the near file's distance= somehow fails to parse


def load_world(path):
    """The markers (x, y, z, kind, j) and the events of the near frame, read from the export."""
    z = zipfile.ZipFile(path)
    fn = lambda p: z.read(FN + p).decode('utf-8')
    markers = []
    for cell in V.ent_iter(z):
        for e in cell.get('Entities') or []:
            tags = [t for t in (e.get('Tags') or [])]
            if 'osmc.enc' not in tags:
                continue
            kind = next(t[6:] for t in tags if t.startswith('osmc.k'))
            j = next(int(t[6:]) for t in tags if t.startswith('osmc.p'))
            p = e['Pos']
            markers.append((int(p[0]), int(p[1]), int(p[2]), kind, j))
    assert markers, 'no osmc.enc markers in the world (was the export made with --encounters?)'
    by_kind = defaultdict(list)
    for m in markers:
        by_kind[m[3]].append(m)

    events = []
    for name in z.namelist():
        m = re.match(re.escape(FN) + r'event/([a-z0-9_]+)\.mcfunction$', name)
        if not m:
            continue
        body = z.read(name).decode('utf-8')
        ms = re.search(r'run function osmc_enc:ev/([a-z_]+)/scan', body)
        if not ms:
            continue                                 # a player-bound event or a stub, not ours
        win = re.search(r'matches (\d+)\.\.(\d+)', body)
        lo = re.search(r'scoreboard players remove #pos osmc_enc_t (\d+)\n', body)
        wid = re.search(r'scoreboard players operation #pos osmc_enc_t /= #(\w+?)_w2 ', body)
        assert win and lo and wid, name
        kind = re.search(r'tag=osmc\.k([a-z]+)', fn('ev/%s/scan.mcfunction' % ms.group(1))).group(1)
        events.append(dict(id=m.group(1), lo=int(win.group(1)), hi=int(win.group(2)),
                           lo_sub=int(lo.group(1)), kind=kind, places=by_kind.get(kind, [])))
    assert events, 'no place-bound events in the pack'
    near = re.search(r'run function osmc_enc:ev/([a-z_]+)/scan',
                     z.read(FN + 'event/%s.mcfunction' % events[0]['id']).decode()).group(1)
    guard = re.search(r'tag=osmc\.enc,scores=\{osmc_enc_b=1\.\.\},distance=\.\.(\d+)\]',
                      fn('ev/%s/near.mcfunction' % near))
    return markers, events, int(guard.group(1)) if guard else GUARD_FALLBACK


def tier(dread):
    """The dread ladder of cycle.mcfunction: the tier the city reads a dread value at."""
    return 4 if dread >= 95 else 3 if dread >= 75 else 2 if dread >= 50 else 1 if dread >= 25 else 0


def search(e, players, busy, markers, guard, radius):
    """scan then far: as @a at @s, the cap nearest places of the kind first, the first free one wakes."""
    cap = 10 if radius == 300 else 6
    for px, pz in players:
        near = [m for m in e['places'] if (m[0] - px) ** 2 + (m[2] - pz) ** 2 <= radius * radius]
        near.sort(key=lambda m: (m[0] - px) ** 2 + (m[2] - pz) ** 2)         # sort=nearest
        for m in near[:cap]:
            if busy[id(m)]:
                continue                                                     # taken until dawn
            if any(busy[id(o)] and (o[0] - m[0]) ** 2 + (o[2] - m[2]) ** 2 <= guard * guard
                   for o in markers):
                continue                                                     # no shared corner
            return m
    return None


def replay(events, markers, guard, dread, players, days, rng, times=None):
    """The cycle of cycle.mcfunction; returns wakes per night, per event, and the wake distances."""
    d = {e['id']: -1 for e in events}                 # the day stamp #<id>_d
    k = {e['id']: -1 for e in events}                 # the slots spent #<id>_k
    busy = {id(m): False for m in markers}            # the busy score   osmc_enc_b
    latch, per_night, per_ev, dists = 0, defaultdict(int), defaultdict(int), defaultdict(list)
    lv = tier(dread)
    ch, rolls = (lv + 1) * 10, lv + 1                 # #ch and #rolls of the frame
    for day_i in range(days):
        woke = 0
        for t in range(0, 24000, 200):                # the cycle runs every 200 ticks
            day = t
            if 0 <= day < 500 and latch != 1:         # the morning hands the city back
                for m in busy:
                    busy[m] = False
            latch = 1 if 0 <= day < 500 else 0
            if not players:                           # unless entity @a: the night waits
                continue
            for e in events:
                if not (e['lo'] <= day <= e['hi']):
                    continue
                if d[e['id']] < day_i:                # a new day hands the counter back
                    k[e['id']] = -1
                    d[e['id']] = day_i
                w = e['hi'] - e['lo'] + 1             # the baked #<id>_w of this event
                slot = (2 * rolls * (day - e['lo_sub']) + w) // (2 * w) - 1   # as the server truncs
                if slot <= k[e['id']]:
                    continue
                k[e['id']] = slot
                if rng.randint(1, 100) > ch:
                    continue                          # the roll of 1..100 against #ch
                hit = (search(e, players, busy, markers, guard, 300)
                       or search(e, players, busy, markers, guard, 1000))
                if hit:
                    busy[id(hit)] = True
                    woke += 1
                    per_ev[e['id']] += 1
                    if times is not None:
                        times.append(day)
                    dists[e['id']].append(min(((hit[0] - px) ** 2 + (hit[2] - pz) ** 2) ** 0.5
                                              for px, pz in players))
        per_night[day_i] = woke
    return per_night, per_ev, dists


def report(path, days):
    markers, events, guard = load_world(path)
    xs = [m[0] for m in markers]
    zs = [m[2] for m in markers]
    cx, cz = sum(xs) // len(xs), sum(zs) // len(zs)
    print('markers %d, kinds %s'
          % (len(markers), {k: sum(1 for m in markers if m[3] == k)
                            for k in sorted({m[3] for m in markers})}))
    print('map x %d..%d, z %d..%d; %d events on the near frame, guard %d blocks'
          % (min(xs), max(xs), min(zs), max(zs), len(events), guard))
    places = {
        'city center': [(cx, cz)],
        'map corner': [(min(xs), min(zs))],
        'two players, split': [(cx, cz), (max(xs), max(zs))],
        'far from town (nothing within 1000)': [(max(xs) + 4000, max(zs) + 4000)],
        'nobody online': [],
    }
    for dread in (0, 25, 50, 75, 95):
        for label, ps in places.items():
            per, per_ev, dists = replay(events, markers, guard, dread, ps, days,
                                        random.Random(1234 + dread))
            avg = sum(per.values()) / days
            empty = sum(1 for v in per.values() if v == 0)
            d_all = sorted(x for v in dists.values() for x in v)
            print('dread %3d lv%d | %-38s | wakes/night avg %5.2f max %2d | empty nights %2d/%d'
                  ' | median dist %s'
                  % (dread, tier(dread), label, avg, max(per.values()), empty, days,
                     d_all[len(d_all) // 2] if d_all else '-'))
    # the evenness: which clock hours the wake-ups land on (a mid tier, a busy placement)
    ts = []
    replay(events, markers, guard, 50, places['two players, split'], days, random.Random(7), times=ts)
    hist = defaultdict(int)
    for t in ts:
        hist[t // 2000] += 1
    print('\nwake-ups by clock hour (dread 50, two players, %d days; daytime 0 = 06:00):' % days)
    for b in range(12):
        h = (b * 2 + 6) % 24
        print('  %02d:00-%02d:00 %s %d' % (h, (h + 2) % 24, '#' * hist.get(b, 0), hist.get(b, 0)))
    # every event alone: its own share against the theory of its tier
    lv = tier(50)
    for scen in ('two players, split', 'map corner'):
        _, per_ev, _ = replay(events, markers, guard, 50, places[scen], days, random.Random(99))
        print('\nper event (dread 50, %s, %d days):' % (scen, days))
        for e in events:
            print('  %-18s places %3d  woke %4d  (%.2f/night, theory %d x %d%% = %.2f)'
                  % (e['id'], len(e['places']), per_ev[e['id']], per_ev[e['id']] / days,
                     lv + 1, (lv + 1) * 10, (lv + 1) * (lv + 1) * 0.10))


if __name__ == '__main__':
    args = [a for a in sys.argv[1:] if not a.startswith('--')]
    if not args:
        sys.exit(__doc__)
    days = next((int(a.split('=')[1]) for a in sys.argv[1:] if a.startswith('--days=')), 60)
    report(args[0], days)
