# Generates the v2.0 rules catalog (rules/<layer>.json) from the v1.x data files and the
# behavior that is hardcoded in the exporter today. The generator is the ONLY writer of these
# files: rerun it and the output must be byte-identical (Uids are derived from layer+code, so a
# regeneration never changes them). Hand edits belong to the future user overrides, not here.
#
# Mechanical sources:
#   resources/osm_rules.json     — which codes each layer produces (the tag rules stay as they are)
#   resources/default.t3d        — the layer list + order, the REL data column, base line widths,
#                                  the material column, the legacy per-layer effect bits
#   resources/heights.csv        — the typical height (meters) and the display name per code
#   resources/point_models.csv   — the point figure number (ModelPoint) per code
# Seed tables below: they RECORD the current C++ behavior (building roles, road widths/markings,
# synthetic codes) so the catalog states what the code does today; phases P4/P5 make the C++ read
# it back. Golden world diffs guard that the record is faithful.
#
# Usage: py -3 devtools/make_rules.py [--out DIR]   (default out = rules/ in the repo root)
import argparse, io, json, os, re, uuid

REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
RES = os.path.join(REPO, "resources")

# the layer list is hardcoded on purpose (v2.0 reform): only codes inside these layers may be added
LAYERS = {
    "bldpol":  "Poly",   # buildings
    "vegpol":  "Poly",   # vegetation
    "vgbpol":  "Poly",   # yard vegetation (synthetic, between houses)
    "rdslin":  "Line",   # roads
    "rrslin":  "Line",   # railways
    "csrlin":  "Line",   # fences / power lines / boundaries
    "csrpol":  "Poly",   # plates (内容由 code 决定)
    "csrpnt":  "Point",  # point structures
    "hdtpnt":  "Point",  # water point structures
    "rdcpnt":  "Point",  # road point structures
    "rdcpol":  "Poly",   # parking lots and platforms
    "blkpol":  "Poly",   # block covers (squares, asphalt)
    "hdrpol":  "Poly",   # water areas
    "hdrlin":  "Line",   # water lines
    "mrspol":  "Poly",   # wetland effect overlay (may cover any other layer)
    "lcapol":  "Poly",   # ground surfaces (bare rock, sand)
    "terpol":  "Poly",   # territories: military base / prison (effect per code)
}
# legacy t3d source order — the loader must enumerate layers in exactly this order (P2);
# milpol+pripol are merged into terpol at pripol's place; vgbpol is synthetic, never enumerated
T3D_ORDER = ["hdrlin", "rdslin", "rrslin", "bldpol", "blkpol", "csrpol", "csrpnt", "csrlin",
             "hdtpnt", "rdcpnt", "rdcpol", "hdrpol", "vegpol", "mrspol", "lcapol", "terpol"]

# the legacy t3d effect bit of the source -> the v2.0 per-code effect (owner: the effect belongs
# to the object, not to the layer); mrspol/milpol/pripol bits 128/512/256 -> swamp/military/prison
LAYER_EFFECT = {"mrspol": "swamp", "milpol": "military", "pripol": "prison"}
# the v1.x source layer of a code when it moves (milpol/pripol -> terpol)
CODE_LAYER_MOVE = {"milpol": "terpol", "pripol": "terpol"}

# ModelPoly: the figure/role family of a polygon object (preview + P5 style choice).
MODEL_POLY_NAME = {
    0:  "Generic", 10: "Building", 11: "AdminBuilding", 12: "School", 13: "Kindergarten",
    14: "Hospital", 15: "ScienceBuilding", 16: "CultureBuilding", 17: "Museum", 18: "Mall",
    19: "Church", 20: "RailwayStation", 21: "IndustrialBuilding", 22: "ResidentialTower",
    23: "RuralHouse", 24: "Dacha", 25: "Cottage", 26: "Shed", 27: "Ruin",
    30: "Cemetery", 31: "SportsGround", 32: "Playground",
    35: "StationPlatform", 36: "ParkingSite",
    40: "Square", 41: "AsphaltSite", 45: "Water",
    50: "RockGround", 51: "SandGround", 55: "Wetland",
    60: "MilitaryTerritory", 61: "PrisonTerritory",
    70: "Forest", 71: "Orchard", 72: "Park", 73: "Shrubs", 74: "Vineyard",
    75: "Meadow", 76: "Arable", 77: "VegetableGarden", 78: "Lawn",
}
# ModelLine: the style family of a line object (records the roadScale/marking switches of
# scene/mcscene.cpp and export/exportrun.cpp and the rail/water/fence roles).
MODEL_LINE_NAME = {
    0: "GenericLine",
    10: "Highway", 11: "MajorStreet", 12: "NamedStreet", 13: "InterBlockRoad",
    14: "FootPath", 15: "BikePath", 16: "Crosswalk", 17: "IntraBlockPath",
    18: "Interchange", 19: "NetworkRoad", 20: "FieldRoad", 21: "Trail",
    30: "Railway", 31: "NarrowRailway", 32: "Monorail", 33: "MetroTunnel", 34: "Tram",
    40: "Fence", 45: "River", 46: "Stream", 47: "Canal",
}

# code -> Model (Poly layers); today's role switches: export/voxelize.cpp calcFloors() flags,
# the cemetery/sports fillers, the blkpol/lcapol code filters of scene/mcscene.cpp
MODEL_POLY_CODE = {
    44000000: 26, 44300000: 27, 44310000: 10, 44311000: 11, 44312000: 12, 44313000: 13,
    44314000: 14, 44315000: 15, 44316000: 16, 44317000: 17, 44318000: 18, 44319000: 19,
    44330000: 20, 44340000: 21, 44400000: 22, 44400002: 23, 44400100: 24, 44400110: 25,
    53510000: 30, 53700000: 31, 53710000: 32,
    62134000: 35, 62230000: 36,
    45300000: 40, 62231000: 41,
    31120000: 45, 31131000: 45, 31132000: 45, 31133000: 45, 31410000: 45, 31430000: 45,
    22410000: 50, 72251100: 51, 72310000: 55,
    53220000: 60, 53230000: 61,
    71111110: 70, 71111111: 70, 71126190: 70, 71123000: 71, 71126000: 72, 71211200: 73,
    71222000: 74, 71314000: 75, 71324000: 76, 71328000: 77, 71329000: 78,
}
# code -> Model (Line layers); rdslin: the roadScale switch of scene/mcscene.cpp + the marking
# sets of export/exportrun.cpp; rrslin: the rail roles of exportrun; hdrlin: the water lines
MODEL_LINE_CODE = {
    45210000: 10, 61210000: 10, 61220000: 10, 61230000: 10,
    45220000: 11, 45230000: 12, 45240000: 13,
    45260000: 14, 45261000: 15, 45262000: 16, 45270000: 17,
    61231000: 18, 61232000: 19, 61330000: 20, 61420000: 21,
    61111000: 30, 61112000: 31, 61113000: 32, 61123100: 33, 61124000: 34,
    82200000: 40,
    31420000: 45, 31421000: 46, 31440000: 47,
}
# the roadProps the C++ hardcodes today: mcscene.cpp roadScale (× the layer WidthBase) and the
# marking set of exportrun.cpp (main streets + highways); omitted when default
ROAD_PROPS = {
    45210000: (2.5, True), 61210000: (2.5, True), 61220000: (2.5, True), 61230000: (2.5, True),
    45220000: (2.0, True), 45230000: (2.0, True),
    45240000: (2.0, False), 61231000: (2.0, False), 61232000: (2.0, False),
}

# codes the exporter itself creates (tmcgreen.cpp): never appear in the input data
INTERNAL_CODES = {
    ("vgbpol", 71126190): ("Courtyard tree plantings", 15),
    ("vegpol", 71111111): ("Forced taiga plantings", 22),
}
# codes C++ recognizes but that arrive through the tmc_buildings.dat enhancer, not osm rules
SEED_CODES = {
    "bldpol": [44400100, 44400110],
}
# names heights.csv does not carry (the t3d/tqt-era codes)
NAME_FALLBACK = {
    22410000: "Bare rock", 72251100: "Sand", 72310000: "Wetlands", 82200000: "Fences",
    53220000: "Military bases", 53230000: "Prisons",
    31420000: "Rivers", 31421000: "Streams", 31440000: "Canals",
    31430000: "Canals and ditches (areas)",
    31650000: "Fountains", 51000101: "Waste bins", 53340900: "Communication towers",
    53340910: "Lighting masts", 53520000: "Monument steles", 53520900: "Memorial statues",
    53520930: "Sculptures", 53520950: "Benches", 62211000: "Bus stops",
}
# codes arriving from the tmc_buildings.dat enhancer (the {db:code} rule): the layer takes them
# with the generic behavior — the loader must not reject unknown db codes
DB_CODE_LAYERS = {"bldpol"}

UUID_NS = uuid.uuid5(uuid.NAMESPACE_URL, "https://github.com/mrHalfer/OSM2MC/rules")

def uid(layer, code):
    return str(uuid.uuid5(UUID_NS, "%s/%d" % (layer, code)))

def read_t3d():
    d = json.load(io.open(os.path.join(RES, "default.t3d"), encoding="utf-8"))
    return {s["layer"]: s for s in d["vectorSources"]}

def read_heights():
    d = {}
    for ln in io.open(os.path.join(RES, "heights.csv"), encoding="utf-8-sig").read().splitlines():
        ln = ln.strip()
        if not ln or ln.startswith("#") or ln.startswith("CODE;"):
            continue
        p = ln.split(";")
        try:
            code, high = int(p[0]), float(p[1])
        except ValueError:
            continue
        d[code] = (high, p[2] if len(p) > 2 else "")
    return d

def read_point_models():
    d = {}
    for ln in io.open(os.path.join(RES, "point_models.csv"), encoding="utf-8-sig").read().splitlines():
        ln = ln.strip()
        if not ln or ln.startswith("#"):
            continue
        p = ln.split(";")
        try:
            d[int(p[0])] = int(p[1])
        except ValueError:
            continue   # the "code;model" header line
    return d

def read_rule_codes():
    d = json.load(io.open(os.path.join(RES, "osm_rules.json"), encoding="utf-8"))
    per = {}
    for r in d["rules"]:
        layer = r.get("layer", "")
        code = r.get("fields", {}).get("CODE")
        try:
            code = int(str(code))
        except (TypeError, ValueError):
            continue   # "{db:code}" placeholders land here too
        if code > 0:
            per.setdefault(CODE_LAYER_MOVE.get(layer, layer), set()).add(code)
    return per

def num(v):
    return int(v) if float(v) == int(v) else float(v)

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--out", default=os.path.join(REPO, "rules"))
    a = ap.parse_args()

    t3d, heights, spot, rules = read_t3d(), read_heights(), read_point_models(), read_rule_codes()
    for (layer, code), (name, rel) in INTERNAL_CODES.items():
        rules.setdefault(layer, set()).add(code)
    for layer, codes in SEED_CODES.items():
        rules.setdefault(layer, set()).update(codes)

    os.makedirs(a.out, exist_ok=True)
    for fn in sorted(os.listdir(a.out)):
        if re.match(r"^[a-z]+\.json$", fn):
            os.remove(os.path.join(a.out, fn))

    warnings = []
    # the effect comes from the legacy source bit of the layer the code used to live on
    CODE_EFFECT = {53220000: "military", 53230000: "prison", 72310000: "swamp"}
    # vgbpol is synthetic (tmcgreen creates it at runtime): its file exists for the preview and
    # the internal codes, the loader never enumerates it (enumerated order = T3D_ORDER without it)
    for layer in T3D_ORDER + ["vgbpol"]:
        fam = LAYERS[layer]
        meta = {}
        if fam == "Line" and layer in t3d:
            meta["WidthBase"] = num(t3d[layer]["lineWidth"])
        if t3d.get(layer, {}).get("absorptionColumn") == "MAT":
            meta["MaterialColumn"] = "MAT"
        if layer in DB_CODE_LAYERS:
            meta["DbCodes"] = True
        col = "REL" if t3d.get(layer, {}).get("field") or layer == "vgbpol" else None

        objects = []
        for code in sorted(rules.get(layer, ())):
            high, name = heights.get(code, (None, ""))
            internal = (layer, code) in INTERNAL_CODES
            if internal and not high:
                high = INTERNAL_CODES[(layer, code)][1]
            if not name:
                name = INTERNAL_CODES[(layer, code)][0] if internal else NAME_FALLBACK.get(code, "")
            eff = CODE_EFFECT.get(code)
            if layer == "mrspol":
                eff = eff or "swamp"
            model = spot.get(code, 0) if fam == "Point" else \
                    (MODEL_POLY_CODE if fam == "Poly" else MODEL_LINE_CODE).get(code, 0)
            if model == 0:
                warnings.append("%s: code %d has no %s model (generic)" % (layer, code, fam))
            if not name:
                warnings.append("%s: code %d has no name" % (layer, code))
            o = {"Uid": uid(layer, code), "Code": code, "Name": name}
            o["Effect"] = eff
            o["Model"] = model
            rel = {}
            if col:
                rel = {"Auto": True, "Column": col}
                if high:
                    rel["Default"] = num(high)
            elif high:
                rel = {"Auto": True, "Default": num(high)}
            if rel:
                o["Rel"] = rel
            internal = (layer, code) in INTERNAL_CODES
            if internal:
                o["Internal"] = True
            props = {}
            if layer == "rdslin" and code in ROAD_PROPS:
                ws, mark = ROAD_PROPS[code]
                if ws != 1.0:
                    props["WidthScale"] = ws
                if mark:
                    props["Marking"] = True
            if props:
                o["Props"] = props
            objects.append(o)

        doc = {"Layer": layer, "Family": fam}
        doc.update(meta)
        doc["Objects"] = objects
        text = json.dumps(doc, indent=1, ensure_ascii=False) + "\n"
        with io.open(os.path.join(a.out, layer + ".json"), "w", encoding="utf-8", newline="\n") as f:
            f.write(text)
        print("%-8s %s %3d objects" % (layer, fam, len(objects)))

    for w in warnings:
        print("warning:", w)

if __name__ == "__main__":
    main()
