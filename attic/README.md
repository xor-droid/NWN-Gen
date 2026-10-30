# attic

`pack-python-retired.tar.gz` holds the former `tools/pack/*.py` authoring scripts (install scanning,
tile pool, Python reference model of the dungeon generator, slice builders). They were replaced by C:

| was | now |
|---|---|
| scan_*, common, tiles, pool, wok, check_conventions, export_tilepack, export_templates | `tools/nwnpack.c` (+ `src/key.c`, `src/tileset.c`) |
| compare_c, area (validators) | `tools/dgcheck.c` |
| chain_ref, layout, rng (reference model) | `src/dungeon.c`; `tests/data/expected_*.txt` are its frozen output |
| build_slice, build_slice2 | superseded by `nwndgen --recipe` |

Kept only because this directory is not under version control. Nothing builds, tests or documents it;
delete it whenever you like.
