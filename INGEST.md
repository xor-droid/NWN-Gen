# Start here: Claude ingestion context

You are continuing a software project for the user on their Linux host. Read every
Markdown file in this bundle before implementing. Work in the user's chosen project
directory. Follow any applicable local AGENTS.md or CLAUDE.md instructions.

## User-approved goal

Build a POSIX C command-line application that generates playable **Neverwinter
Nights: Enhanced Edition (NWN1:EE)** `.mod` adventures. The adventures should be
replayable randomized dungeons. Configuration must include party size, a level
range, and a seed. Identical seed, normalized configuration, generator version,
and input asset bytes must yield **byte-for-byte identical `.mod` output**.

The initial content targets **one solo level 1–2 character, without a companion**.
Party size must remain configurable; larger parties and other level ranges require
appropriate content and balance validation, rather than merely accepting flags.

The user chose `.mod` output over a manifest/NWScript-only deliverable and accepted
the recommendation to build from a small, tested base module and room templates.
A manifest is useful as an additional diagnostic, not a substitute for the module.

## Act on this task

Inspect the Linux host for an NWN:EE installation, assets, existing module tools,
and a C toolchain. Then implement the generator and validate it as far as the
environment allows. Use PLAN.md for milestones. Ask for a real asset path only
when inspection cannot find the required assets. Continue independent coding and
format tests while waiting for missing assets; do not fabricate successful game tests.

Keep the app itself POSIX C. Python may be used for independent test fixtures,
but must not become a runtime dependency. No cloud service, AI API, API key,
account, or paid service is needed.

## Important state at handoff

- This archive is a **specification and implementation handoff**, not an executable
  generator or a playable adventure.
- No starter NWN1:EE `.mod`, valid room-template pack, or compiled adventure scripts
  were supplied in the original session.
- No game/toolset playtest has occurred.
- An exploratory C assembler was started in the previous workspace. It is **not
  included in this archive**, has not been validated, and should not be treated as
  an existing implementation on this host.
- All interface examples and template conventions below are proposals unless
  explicitly listed as user-approved requirements.

## Suggested first reply and next action

Briefly confirm that you will build the deterministic `.mod` generator, inspect
the host, and report what asset/template work is necessary. Then perform that
inspection. Do not restart the product-choice conversation or ask whether the
user wants manifest-only output.

## Completion standard

Deliver buildable C source, build instructions, usage examples, deterministic
binary-format tests, documented input templates, and at least one generated
module validated with the available tools. Claim it is playable only after a
real NWN:EE load/playtest. If game access remains unavailable, name the missing
validation and assets explicitly instead of claiming the task is finished.
