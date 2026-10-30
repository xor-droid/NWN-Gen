# Porting the remaining Python to POSIX C

Rule from the user (2026-10-29): anything built for this project is POSIX C (or POSIX sh), not Python.
Runtime (`nwndgen`) has been C-only from the start; layout/tile generation moved into C with
`--recipe`. This plan retires the rest of the Python (≈1500 lines) in phases. Each phase ends with
`make test` + `make sanitize` green and the old Python deleted, so Python never runs alongside its
replacement for long.

| Phase | Replaces | New C | Done when |
|---|---|---|---|
| 1. Test tooling | `tests/erfpy.py`, `make_fixture.py`, `verify_mod.py`, 6 inline python blocks in `e2e.sh`, `indep_nwn_tools.sh` | `tests/fixtool.c` (standalone: own ERF/GFF writer+parser, shares **no** code with `src/`, so it stays an independent check) | e2e golden hashes unchanged (proves byte-identical fixtures); no `python3` in `tests/` |
| 2. Install reader | `tools/pack/{common,scan_stock_areas,scan_tdc_full,check_conventions,tiles,pool,wok,export_tilepack,export_templates}.py` | `src/key.c` (KEY/BIF reader), `.set`/2DA/ASCII-`.wok` parsers, `tools/nwnpack.c` (`nwnpack tilepack`, `nwnpack templates`, `nwnpack check`) | `nwnpack` output is byte-identical to the Python exports for tdc01; `neverwinter.nim` no longer needed to author a pack |
| 3. Independent verifier | `tools/pack/{compare_c,chain_ref,layout,rng,area}.py` | `tools/dgcheck.c`: re-derives tile adjacency from the `.set`, checks a built `.mod` (tile rules, objects on floor, waypoints outside triggers, links resolve) | `dgcheck` passes on the seed modules; `tests/data/expected_*.txt` kept as frozen goldens (written by the retired reference) |
| 4. Cleanup | `build_slice*.py` (superseded by `--recipe`), docs | — | `find . -name '*.py'` is empty; README/USAGE updated |

Risks / decisions
* Phase 2 reads the install directly (KEY/BIF + `data/mod`, `data/nwm` ERFs). Only base-game data
  is needed (tdc01 + two donor modules); override/hak stack is not emulated.
* Phase 3 keeps independence by deriving rules from the `.set` file again instead of reusing the tilepack.
* `tests/indep_nwn_tools.sh` stays: it is POSIX sh calling external neverwinter.nim binaries, optional.

## Status (2026-10-29): all four phases done

* Phase 1: `tests/fixtool.c` replaced `erfpy.py`, `make_fixture.py`, `verify_mod.py` and 6 inline python blocks.
  Its fixtures are byte-identical to the Python ones (4 modes compared) and the e2e golden hashes did not change.
* Phase 2: `tools/nwnpack.c` + `src/key.c` + `src/tileset.c`. `nwnpack tilepack` output is byte-identical to the Python
  export; `nwnpack check` reproduces the 49 + 183 areas / 0 violations gate; modules built from the C-made and
  Python-made templates are semantically identical in all 11 resources. No neverwinter.nim needed for authoring.
* Phase 3: `tools/dgcheck.c` validates built modules from the tileset's own rules; 300/300 seeds of a 3-area chain pass
  against the real tdc01 data (`tools/sweep_seeds.sh`), and a deliberately wrong tileset is rejected.
* Phase 4: `tools/pack/*.py` archived to `attic/pack-python-retired.tar.gz`; `find . -name '*.py'` is empty.
  The only external binaries left are optional: neverwinter.nim in `tests/indep_nwn_tools.sh` (POSIX sh) and `jq`/`sha256sum`
  in shell checks.
