# Validation and acceptance checks

## Reproducibility

- Same base/pack/seed/config produces identical bytes in two fresh processes.
- Output path, current working directory, file mtimes, timezone, and locale do not
  change output. Test a non-default locale when available.
- Reordering catalog rows or filesystem enumeration does not change output.
- Known-answer PRNG vectors and a golden module hash pin algorithm behavior.
- GCC and Clang agree for the same fixed inputs; test multiple architectures when
  available. Never claim untested portability results.
- Exercise seed 0 and UINT64_MAX, invalid/overflow seeds, and boundary options.
- Different seeds exercise different variants over a sample; do not require every
  pair of seeds to select different rooms.

## Configuration and selection

- Defaults yield solo levels 1–2.
- Invalid ranges, unknown/duplicate options, missing paths and unsupported party/
  level combinations fail cleanly with no published artifact.
- Selected variants support the entire configured level band and party size.
- Slot IDs, variant IDs, ownership sets, and quest compatibility are validated.
- Catalog paths with spaces work; relative paths are catalog-relative.

## Binary format and failure paths

- Parse generated modules with an independently implemented NWN tool.
- Compare untouched resource payloads to their base bytes.
- Verify selected replacements and required module.ifo / ARE / GIT / GIC resources.
- Test shuffled key IDs, duplicate resource keys, truncated headers, malformed
  localized strings, out-of-range offsets, integer overflow attempts, oversized
  input, and unsupported archive/GFF versions.
- Reject conflicting slot ownership, missing triplets, unknown replacement
  targets, and unauthorized module/script overrides.
- Test disk/write failures as practical, existing output, and temp-file cleanup.
- Use strict compiler warnings and address/undefined-behavior sanitizers where
  available; resource cleanup should also allow leak checks.
- Bound aggregate memory usage, not just individual file sizes.

Fixtures may contain synthetic modules for parser tests. Clearly separate them
from playable assets and do not count fixture generation as a game test.

## In-game acceptance

Record NWN:EE version, platform, dependencies and content-pack version. For several
seeds and representative classes at levels 1 and 2, verify:

1. The module appears and loads as a new adventure.
2. The character starts on walkable ground and can move normally.
3. Doors/transitions go to the intended reachable destinations.
4. Encounters spawn and scripts execute without missing-resource errors.
5. The objective can be completed without class-specific mandatory abilities.
6. Required quest items and NPCs are reachable and match completion checks.
7. The safe/rest space behaves as designed.
8. The final challenge, rewards, shortcut, and exit work.
9. Another seed changes actual encounters/layout/content.
10. Regenerating the same seed restores identical module bytes.

Playtest party sizes above one only if the pack claims to support them. The level
range option does not automatically balance every class or party composition.

## Final report

Separate results into: source builds, deterministic/format tests, independent tool
validation, game load, and playtesting. Report exactly what ran and what remains.
List commands needed to reproduce a known artifact. Include missing assets or game
access as a concrete remaining dependency when applicable.
