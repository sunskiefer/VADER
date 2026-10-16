## What and why

## Checks
- [ ] Offline first: `tools/test_port.sh <vst.json>` (x86, ASan) and/or `python3 tools/test_catalog.py`; skin changes previewed offline
- [ ] Device-verified, or marked "offline only" here and in NOTES (device, MPC OS version, date)
- [ ] Catalog-conformant if it is a port or release (`catalog_check.py --catalog` OK)
- [ ] Docs synced (map in `.claude/skills/mpc-vst-plugin/SKILL.md`, "Docs sync"): NOTES / PORTING / RELEASING / README / ROADMAP / SKILL / docstrings, or "no docs needed: <why>"
- [ ] Repo stays device-generic (no IPs, serials, Akai stock skin files or Steinberg SDK)
