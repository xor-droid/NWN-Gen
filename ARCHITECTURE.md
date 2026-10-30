# Proposed architecture

## Approach

Use a small base `.mod` containing known-good module configuration, entry point,
compiled scripts, blueprints, and connected area slots. Use authored room variants
to replace resources for those slots. The C app deterministically selects variants
and writes a canonical module archive.

The simplest first version may keep the area graph fixed while changing the
internal room layouts, placements, encounters, loot, and objective bundles.
Describe that limitation accurately. Shuffling a catalog entry without changing
actual in-game content does not count as dungeon generation.

Full procedural tile placement is a later feature: it requires tileset adjacency,
tile orientations, walkmeshes, walkable entry positions, and navigation validation.
Do not invent tile IDs or promise traversable procedural geometry without testing.

## Separate concerns

1. CLI/config: parse and normalize options, validate support.
2. PRNG: fixed specified algorithm and unbiased bounded selection.
3. Catalog: parse, validate, canonicalize, and check template compatibility.
4. Plan: select compatible variants; validate graph/quest constraints.
5. Resources: read base and overlays; enforce resource ownership and collisions.
6. Writer: serialize canonical `.mod` data and metadata.
7. Manifest: record effective inputs and selections for inspection/reproduction.

Keep the generation plan independently inspectable and testable. Avoid a mandatory
third-party runtime. A Makefile and a C99 or C11 compiler should suffice for the
generator; template preparation may use documented external tools.

## Binary format

NWN1 `.mod` files use an ERF-family container. A module needs a `module.ifo` and
related resources. Areas commonly use `.are`, `.git`, and `.gic`. These are NWN
resource types; `.git` here means game-instance data, not a Git repository.
Most structured game data uses GFF V3.2. NWScript source `.nss` is not a replacement
for runnable compiled `.ncs` resources.

Initially support uncompressed `MOD V1.0` / `ERF V1.0`. Explicitly reject other
versions and NWN2 inputs. Verify exact structures and resource type IDs against
the reference implementations before writing code; a shared container signature
does not by itself prove an input is an NWN1 module.

ERF V1.0 implementation considerations:

- The header is 160 bytes; integers are explicitly little-endian.
- Key entries include a 16-byte resource name, resource ID, type, and padding.
- Resource-list entries carry data offset and size.
- Honor key resource IDs when locating payloads, not just key-array positions.
- Validate table sizes, offsets, string lengths, identifiers, duplicates, and
  arithmetic before access. Bound both per-file and aggregate memory use.
- Preserve needed localized strings and references; canonicalize ordering if parsed.
- Sort resource keys by ASCII-normalized name and numeric type and assign stable IDs.
- Use fixed archive build metadata and zero reserved/padding bytes.
- Never serialize native C structs directly: padding and host endianness differ.

Preserve unknown base resources byte-for-byte where possible. If an existing
resource type is unsupported for mutation, copy it or reject explicitly; never
drop it silently. Reject ambiguous duplicate resource keys.

## Template-based mutation

Author compatible variants of a slot with identical resource names and stable
arrival/door contracts. Replace an area's ARE/GIT/GIC triplet atomically. Preserve
the base `module.ifo` when the area set and graph do not change. Shared compiled
scripts and blueprints stay in the base; variants must reference available assets.

If later changing the area set or door graph, update IFO area lists, transition
targets, entry locations, tags, quest references, and related metadata coherently.
That is a separate milestone, not a filename substitution.

## Seed behavior

SplitMix64 with published constants is a reasonable small PRNG proposal. Use only
specified-width unsigned arithmetic and rejection sampling for bounded choices.
Sort slots and eligible variants before consuming random values. Add known-answer
tests and version the algorithm. Cryptographic randomness is unnecessary.

Consider separate derived streams for layout, encounters, and loot later so a
small loot change need not reshuffle layout. If added, specify the seed-derivation
function and treat changes as algorithm-version changes.

## Manifest

Produce stable UTF-8 or ASCII text with LF endings and no timestamp or absolute
paths. Record generator version, PRNG version, seed, normalized configuration,
content-pack ID/version, hashes of base/catalog/selected resources, and selected
room/objective IDs. Use a defined hash such as SHA-256, not an implementation-
dependent C hash. Catalog hashing must match the promised row-order invariance:
hash a canonical catalog representation, not raw unordered rows.

A sidecar is easiest to inspect. Optionally embed the same content as a named
text resource after verifying engine compatibility. If publishing multiple files,
document consistency/error handling; guarantee atomic publication of the `.mod`.

## Reference starting points

Consult current primary source code/documentation, and pin tool versions used for
asset preparation. These projects were located during the original session:

- [neverwinter.nim](https://github.com/niv/neverwinter.nim)
- [ERF reader/writer implementation](https://github.com/niv/neverwinter.nim/blob/master/neverwinter/erf.nim)
- [nwn-lib](https://github.com/niv/nwn-lib)
- [nwn-lib-d](https://github.com/CromFr/nwn-lib-d)
- [nasher](https://github.com/squattingmonk/nasher)

Their existence is useful evidence of tooling, not proof that the new generator
works. Review licenses before copying code. Independently validate generated
archives with one of these tools instead of relying only on a self-written reader.
