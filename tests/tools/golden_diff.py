# Diff two manifests made by golden_manifest.py. Exit code 0 = the worlds are identical
# (over the non-ignored members), 1 = any member differs / appeared / disappeared.
# Usage: golden_diff.py <before.json> <after.json>
import json, sys

def load(p):
    return json.load(open(p, encoding="utf-8"))

if __name__ == "__main__":
    a, b = load(sys.argv[1]), load(sys.argv[2])
    ma, mb = a["members"], b["members"]
    gone = sorted(set(ma) - set(mb))
    new = sorted(set(mb) - set(ma))
    changed = sorted(k for k in set(ma) & set(mb) if ma[k] != mb[k])
    for k in gone:
        print("GONE    ", k)
    for k in new:
        print("NEW     ", k)
    for k in changed:
        print("CHANGED ", k)
    print(f"identical: {not (gone or new or changed)}  (members {len(ma)}/{len(mb)}, changed {len(changed)})")
    sys.exit(0 if not (gone or new or changed) else 1)
