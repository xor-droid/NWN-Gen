# Requirements and decisions

## Confirmed by the user

1. Target game: Neverwinter Nights: Enhanced Edition, the first NWN game.
2. Replayable randomized dungeon adventures.
3. First content: solo, character levels 1–2.
4. Implementation: a POSIX C application suitable for a Linux host.
5. Configuration: party size and minimum/maximum character levels.
6. Explicit seeds that reproduce generated content.
7. Byte-for-byte identical generated files for identical generation inputs.
8. Primary output: a playable `.mod` file.
9. Continue implementation in Claude on the user's Linux host using this handoff.

The user selected the recommended template-based `.mod` approach. Specific flag
names, numeric limits, archive internals, room counts, and content details below
have not been separately approved and can be refined during implementation.

## Proposed command-line interface

```sh
make
./nwndgen \
  --base assets/shifting_cellars.mod \
  --templates assets/rooms.tsv \
  --seed 12345 \
  --party-size 1 \
  --min-level 1 \
  --max-level 2 \
  --output build/cellars-12345.mod
```

Proposed defaults: party size 1; minimum level 1; maximum level 2. Require an
explicit seed and output path. A decimal unsigned 64-bit seed is sufficient.
If adding random seed generation later, print and persist the chosen seed.

Provide `--help`, `--version`, precise error messages, and nonzero exit status
for invalid input or failed output. Reject duplicate/unknown flags and invalid
ranges. Avoid silently replacing an existing output; document overwrite behavior.

## Configuration semantics

Define level range as the **supported character-level band for this adventure**,
not a promise that every monster has a level within that band. For the first
version, select authored room variants whose declared support covers the whole
requested band and the configured party size. Reject unsupported combinations.

Initial content should support party size 1 and levels 1–2. Design the engine so
more party sizes and bands can be added without changing the file format. Do not
claim balanced support for levels 1–40 or larger parties without authored content.

Party size describes adventuring player characters; initial solo content assumes
no companion. Configuration does not need to modify the game's player limit or
force the player character's level.

## Reproducibility contract

Equal seed + normalized options + generator version/algorithm + exact template
and base-module bytes => identical output bytes on supported POSIX hosts.

- Output path, input-directory location, current time, timezone, locale, directory
  enumeration order, catalog row order, and compiler must not affect the result.
- Publish an explicit generator/algorithm version and stable ordering rules.
- Record seed, configuration, selected templates, and input hashes in a manifest.
- Store no build path, random UUID, process ID, or current timestamp in the artifact.
- Use portable integer arithmetic and explicit binary endianness.
- Guarantee generation reproducibility, not identical combat rolls or playthroughs.
- Different seeds may select the same content; finite content cannot make every
  seed unique. Metadata can still distinguish seeds.

## Output and error handling

Primary output must be an actual NWN module archive with the required resources,
not JSON renamed to `.mod`, a stub, or NWScript source without compiled scripts.
Preserve required resources from the base. Write output atomically and handle
partial writes, disk errors, corrupt templates, and resource collisions cleanly.
Do not leave a plausible-looking completed module after a failed generation.

Game assets must come from the user's installation or legally distributable
templates. Document required expansions, tilesets, HAKs, TLKs, and overrides.
Prefer stock NWN:EE content without third-party dependencies for the first pack.
