# Golden fingerprint of an exported world zip, CONTENT level: a byte hash of the whole zip would
# differ on every run by design (anvil mtime tables, level.dat LastPlayed), so we hash what the
# generator produced: every mca record (the second 4 KiB timestamp table skipped), NBT minus the
# volatile keys, and everything else as is. Run before and after a change and diff with
# golden_diff.py. Usage: golden_manifest.py <world.zip> [-o manifest.json] [--ignore GLOB ...]
import argparse, fnmatch, gzip, hashlib, json, struct, sys, zipfile

sys.path.insert(0, __import__("os").path.dirname(__file__))
import verify_mc_export as V

# informational file, it carries the wall clock and the run duration
DEFAULT_IGNORE = ["export_info.json"]
# volatile scalars inside Data (wall-clock writes of the format itself; LevelName just echoes
# the -o file name and says nothing about the generation)
VOLATILE_LEVEL_KEYS = ["Timestamp", "LastPlayed", "LevelName"]

def h_bytes(b, prefix=b""):
    h = hashlib.sha256()
    h.update(prefix)
    h.update(b)
    return h.hexdigest()

def mca_digest(raw):
    # region file: 5 x 4-byte entries per chunk at the head, mtime table at 4096 — we hash the
    # location table + every present slot's RAW record (length+format+payload), not the mtimes
    h = hashlib.sha256()
    h.update(b"mca")
    h.update(raw[:4096])
    for slot in range(1024):
        loc = struct.unpack(">I", raw[slot * 4:slot * 4 + 4])[0]
        if not loc:
            continue
        so = loc >> 8
        size = struct.unpack(">I", raw[so * 4096:so * 4096 + 4])[0]
        h.update(struct.pack(">HI", slot, so))
        h.update(raw[so * 4096:so * 4096 + 4 + size])
    return h.hexdigest()

def level_digest(raw):
    nbt = V.parse(gzip.decompress(raw))
    data = nbt.get("Data", nbt)
    for k in VOLATILE_LEVEL_KEYS:
        data.pop(k, None)
    return h_bytes(json.dumps(nbt, sort_keys=True, default=str).encode("utf-8"), b"nbt:")

def digest(path, ignore):
    zf = zipfile.ZipFile(path)
    members, skipped = {}, []
    for n in sorted(zf.namelist()):
        if any(fnmatch.fnmatch(n, g) for g in ignore):
            skipped.append(n)
            continue
        raw = zf.read(n)
        if n.endswith(".mca"):
            members[n] = mca_digest(raw)
        elif n in ("level.dat", "level.dat_old"):
            members[n] = level_digest(raw)
        else:
            members[n] = h_bytes(raw)
    return {"zip": path.replace("\\", "/"), "members": members,
            "ignored": skipped, "total": len(members)}

if __name__ == "__main__":
    ap = argparse.ArgumentParser()
    ap.add_argument("zip")
    ap.add_argument("-o", "--out", default=None)
    ap.add_argument("--ignore", action="append", default=None)
    a = ap.parse_args()
    m = digest(a.zip, a.ignore if a.ignore is not None else DEFAULT_IGNORE)
    text = json.dumps(m, indent=1, sort_keys=True)
    if a.out:
        open(a.out, "w", encoding="utf-8").write(text + "\n")
    else:
        print(text)
    print("members:", m["total"], "ignored:", len(m["ignored"]), file=sys.stderr)
