#!/usr/bin/env python3
"""Does a plugin's skin use only the file format MPC OS 2.x itself uses? (docs/OS2_SKINS.md)

Every JSON object in a skin (`Plugin Skins/TUI.json`, `Q-Links.json`) carries a `version`, and each kind of object has its own
version that changed between MPC OS generations. A skin whose objects all use a version, with fields, that the stock skins of
MPC OS 2.15.1 use is "2.x-shaped": 2.15.1 reads it (shown on a real unit), and 3.x reads it too. Anything else is "3.x" only.

The table of what 2.15.1 uses (`skin_roles_2x.json`) holds version numbers and field names only, never an Akai file. Rebuild it
from a folder of stock skins with:  python3 tools/skin_compat.py build <Synths folder> > tools/skin_roles_2x.json
Check a skin:                        python3 tools/skin_compat.py check <TUI.json> [Q-Links.json]

"Checked against the 2.15.1 table" is not "tested on a 2.x unit", and the table comes from one 2.x version."""
import collections
import glob
import json
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
TABLE_PATH = os.path.join(HERE, "skin_roles_2x.json")
MAX_GLIBC_2X = (2, 32)          # MPC OS 2.x has about glibc 2.32 (docs/NOTES.md)
# Component types MPC defines itself; any other type name is a widget the skin defines locally.
BASE_TYPES = ("Image", "Label", "Focus", "Button", "Knob", "Slider", "Meter", "Decorator", "Indicator")


def roles_of(doc, prefix):
    """{role: {version: {field, ...}}} for every versioned object. The role is the key an object sits under (with [] for a list
    item); `data` objects are named by their component's type, local widgets as one kind."""
    out = collections.defaultdict(lambda: collections.defaultdict(set))

    def walk(o, role, typ):
        if isinstance(o, dict):
            if "version" in o:
                key = role
                if role == "data":
                    key = "data:" + (typ if typ in BASE_TYPES else "(local widget)")
                out[prefix + key][o["version"]].update(k for k in o if k != "version")
            for k, x in o.items():
                # a component is {"type": ..., "data": {...}}: its data object takes the component's type
                walk(x, k + ("[]" if isinstance(x, list) else ""), o.get("type") if k == "data" else typ)
        elif isinstance(o, list):
            for x in o:
                walk(x, role, typ)
    walk(doc, "(root)", None)
    return out


def build_table(root):
    """The table from <root>/*/Plugin Skins/{TUI,Q-Links}.json (a stock Synths folder)."""
    roles = collections.defaultdict(lambda: collections.defaultdict(set))
    n = 0
    for name, prefix in (("TUI.json", "TUI:"), ("Q-Links.json", "QLinks:")):
        for f in sorted(glob.glob(os.path.join(root, "*", "Plugin Skins", name))):
            n += name == "TUI.json"
            for role, versions in roles_of(_load(f), prefix).items():
                for v, fields in versions.items():
                    roles[role][v].update(fields)
    return {"about": "Data versions and field names used by the stock skins of MPC OS 2.15.1 (an MPC Live), read from %d skins. "
                     "Version numbers and field names only; see tools/skin_compat.py." % n,
            "roles": {r: {str(v): sorted(f) for v, f in sorted(vs.items(), key=lambda x: str(x[0]))}
                      for r, vs in sorted(roles.items())}}


def _load(path):
    with open(path) as f:
        return json.load(f)


def load_table(path=TABLE_PATH):
    return _load(path)["roles"]


def skin_problems(tui, qlinks=None, table=None):
    """What in the skin MPC OS 2.15.1's own skins do not use, as short messages (empty: nothing). `tui` and `qlinks` are
    parsed JSON; a missing Q-Links.json is not a problem."""
    table = load_table() if table is None else table
    if not isinstance(tui, dict) or not isinstance(tui.get("pageData"), dict):
        return ["TUI.json has no pageData"]
    seen = roles_of(tui, "TUI:")
    if qlinks is not None:
        seen.update(roles_of(qlinks, "QLinks:"))
    problems = []
    for role in sorted(seen):
        known = table.get(role)
        for v in sorted(seen[role], key=str):
            if known is None:
                problems.append("%s: not a kind of object 2.15.1 skins have (version %s)" % (role, v))
            elif str(v) not in known:
                problems.append("%s version %s (2.15.1 uses %s)" % (role, v, ", ".join(sorted(known))))
            else:
                extra = sorted(seen[role][v] - set(known[str(v)]))
                if extra:
                    problems.append("%s version %s has field(s) 2.15.1 does not use: %s" % (role, v, ", ".join(extra)))
    return problems


def os_compat(glibc, tui, qlinks=None, table=None):
    """(list of MPC OS generations the plugin is compatible with, why not 2.x). ["2.x", "3.x"] needs glibc 2.32 or less and a
    skin that skin_problems() finds nothing in; otherwise ["3.x"]. `glibc` is "x.y[.z]" or None (needs no versioned symbol)."""
    why = []
    if glibc and tuple(int(p) for p in glibc.split(".")[:2]) > MAX_GLIBC_2X:
        why.append("needs glibc %s, MPC OS 2.x has about 2.32" % glibc)
    why += skin_problems(tui, qlinks, table)
    return (["3.x"] if why else ["2.x", "3.x"]), why


def _main(argv):
    if len(argv) >= 3 and argv[1] == "build":
        json.dump(build_table(argv[2]), sys.stdout, indent=1)
        print()
        return 0
    if len(argv) >= 3 and argv[1] == "check":
        tui = _load(argv[2])
        ql = _load(argv[3]) if len(argv) > 3 else None
        gens, why = os_compat(None, tui, ql)
        print("compatible with: " + " and ".join(gens))
        for w in why[:40]:
            print("  - " + w)
        return 0 if len(gens) == 2 else 1
    print(__doc__)
    return 2


if __name__ == "__main__":
    sys.exit(_main(sys.argv))
