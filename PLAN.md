# Implementation plan

## 1. Inspect and settle the input contract

- Read local project instructions and inspect the C toolchain.
- Locate NWN1:EE data, a starter module, module tools, and script compiler.
- Distinguish NWN1 from NWN2 installations.
- Check template licensing and required game content.
- Confirm which validation can run automatically and which requires the game.

Deliver: a small documented asset contract and an honest list of available/missing
inputs. Do not block CLI/format work just because playtesting is unavailable.

## 2. Deterministic generator core

- Implement strict configuration parsing and normalized configuration output.
- Implement a specified seeded PRNG with known-answer vectors.
- Parse and sort the catalog; verify all slot resource ownership and eligibility.
- Produce a stable plan with selected rooms and objective bundle.
- Fail unsupported level/party combinations before creating output.

Deliver: useful source and tests showing seed/config reproducibility.

## 3. Module reader and canonical writer

- Parse required NWN1 ERF V1.0 structures with bounds/overflow checks.
- Preserve base resources and apply only contract-approved room overlays.
- Validate required module/area resources and cross-resource names as feasible.
- Serialize fixed metadata, deterministic resource order, and little-endian fields.
- Write a reproducibility manifest with input hashes.
- Publish completed module atomically with a documented overwrite policy.

Deliver: real `.mod` assembly verified by an independent parser. A syntactically
valid archive is a milestone, not proof of playability.

## 4. Playable first content pack

- Build the base and room variants if no suitable pack exists.
- Implement and compile one coherent objective and exit path first.
- Add encounter variants, useful loot, a safe halfway space, and final challenge.
- Add the other objective bundles after the first works end to end.
- Keep initial support solo levels 1–2 and document class limitations discovered.

Deliver: a runnable adventure that varies actual game content across seeds.

## 5. Verification and delivery

Follow VALIDATION.md. Include source, Makefile, instructions, legal content-pack
inputs or precise asset preparation steps, examples, test results, and remaining
limitations. Record a reproducible sample invocation and expected SHA-256 for a
fixed test pack. Do not claim game validation if only archive tests ran.

## Scope guardrails

- No website, online service, LLM dependency, or API integration.
- Do not substitute a manifest-only deliverable for the requested `.mod`.
- Do not assume arbitrary imported modules support the template contract.
- Do not claim arbitrary party/level balance because the CLI accepts a number.
- Keep graph rewriting and procedural tile synthesis out of the first milestone
  unless the necessary asset and navigation validation already exists.
- If assets or game execution remain unavailable, complete independent work and
  explain precisely what prevents a playable-module verification.
