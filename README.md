> **Implementation status:** the C generator lives in `src/` (see `USAGE.md`;
> build with `make`, test with `make test`). Content-pack status is in
> `assets/PACK.md`: only a format-test fixture exists, no playable adventure.
> The text below is the original handoff README.

# NWN:EE deterministic dungeon generator — handoff

This bundle carries the agreed requirements and proposed implementation approach
from a Codex conversation into Claude on a Linux host. It contains Markdown only;
it does not contain game assets, a working app, or a playable module.

## Feed this to Claude

Extract the archive in your project folder, then paste:

> Read `nwndgen-context/INGEST.md` and all Markdown files it references in this
> folder. Continue the project on this Linux host: build the POSIX C seeded NWN:EE
> `.mod` dungeon generator described there. Inspect the available game assets and
> tools first, then implement and test it. Preserve the byte-for-byte reproducibility
> requirement and the initial solo level 1–2 content target. Treat proposed details
> as implementation guidance, and report missing assets or unperformed game tests
> honestly. Do not replace playable `.mod` output with a manifest-only generator.

Adjust the relative folder path if you extract somewhere else. If your Claude
interface cannot read local files, attach all the Markdown files together and
instruct it to start with INGEST.md.

## Read order

1. INGEST.md — ready-to-use agent context and continuation instructions.
2. REQUIREMENTS.md — confirmed scope and proposed CLI/semantics.
3. DUNGEON_DESIGN.md — initial solo adventure content.
4. ARCHITECTURE.md — deterministic generation and NWN binary assembly.
5. TEMPLATES.md — real asset prerequisites and room contract.
6. PLAN.md — milestones and scope.
7. VALIDATION.md — reproducibility, format, and game acceptance tests.

The first action is host inspection and implementation, not another discussion of
whether to output `.mod`. The user's output choice is already settled.
