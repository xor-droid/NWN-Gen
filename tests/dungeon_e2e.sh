#!/bin/sh
# End-to-end checks for `nwndgen --recipe` (procedural dungeons) against SYNTHETIC fixtures.
# usage: sh tests/dungeon_e2e.sh path/to/build-dir   (needs nwndgen, dgdump, mkdgfix, erfcheck)
# POSIX sh + the C tools only.
set -u
BUILD=$(cd "$1" && pwd)
HERE=$(cd "$(dirname "$0")" && pwd)
T=$(mktemp -d "${TMPDIR:-/tmp}/nwndgen-dg.XXXXXX") || exit 1
trap 'rm -rf "$T"' EXIT INT TERM
pass=0; fail=0
ok()  { pass=$((pass+1)); }
bad() { fail=$((fail+1)); echo "FAIL: $*"; }
check() { desc=$1; shift; if "$@" >/dev/null 2>&1; then ok; else bad "$desc"; fi; }
checkfail() { desc=$1; shift; if "$@" >/dev/null 2>&1; then bad "$desc (expected failure)"; else ok; fi; }

FX="$T/fixture dir"
mkdir "$FX" && "$BUILD/mkdgfix" "$FX" || { echo "fixture build failed"; exit 1; }
cp "$HERE/data/recipe_test.txt" "$FX/"
check "committed tilepack_test.txt matches the generator" cmp "$FX/tilepack.txt" "$HERE/data/tilepack_test.txt"
cp "$HERE/data/tilepack_test.txt" "$FX/tilepack_test.txt"
gen() { "$BUILD/nwndgen" --game nwn1 --base "$FX/base.mod" --recipe "$FX/recipe_test.txt" "$@"; }

# --- basic generation + determinism ---------------------------------------
mkdir "$T/o1" "$T/o2"
gen --seed 12345 --output "$T/o1/a.mod" --manifest "$T/o1/a.txt" >/dev/null || bad "recipe generation"
(cd "$T/o2" && TZ=Pacific/Kiritimati LC_ALL=C.UTF-8 \
  "$BUILD/nwndgen" --game nwn1 --base "$FX/base.mod" --recipe "$FX/recipe_test.txt" --seed 12345 \
  --output other.mod --manifest m.txt >/dev/null) || bad "generation from another cwd"
check "same bytes across cwd/name/tz/locale" cmp "$T/o1/a.mod" "$T/o2/other.mod"
check "manifest identical across runs" cmp "$T/o1/a.txt" "$T/o2/m.txt"
# Golden: the fixture build of seed 12345 without roster/scripts. Recorded after the creature work and
# anchored to the previous generator (v0.3.0) by building the real seed-1 and seed-2 modules with both
# (byte-identical). Any change to the plain path must bump DUNGEON_ALGORITHM_VERSION and this value.
grep -q '^output_sha256=967cc0db8ddda9b78ddb70f2d800845e48c726d573eb6d8fcfef685e1e590855' "$T/o1/a.txt" && ok || bad "plain build golden sha"
for k in dungeon_algorithm= recipe_sha256= tilepack_sha256= template_are_sha256= template_git_sha256= \
         area.ca=12x9 area.cb=9x12 area.cc=16x16 output_sha256=; do
  grep -q "^$k" "$T/o1/a.txt" && ok || bad "manifest lacks $k"
done
grep -q "$T" "$T/o1/a.txt" && bad "manifest leaks a path" || ok
check "erf parses (nwn1)" "$BUILD/erfcheck" nwn1 "$T/o1/a.mod"
gen --seed 12346 --output "$T/o1/b.mod" >/dev/null && ok || bad "second seed"
checkfail "different seed gives different module" cmp -s "$T/o1/a.mod" "$T/o1/b.mod"

# 2 base resources (module.ifo, repute.fac) + 3 parts for each of the 3 areas
"$BUILD/erfcheck" nwn1 "$T/o1/a.mod" | grep -q "11 resources" && ok || bad "resource count"

# --- gate and argument checks ----------------------------------------------
checkfail "refuses to overwrite" gen --seed 12345 --output "$T/o1/a.mod"
check "--force overwrites" gen --seed 12345 --output "$T/o1/a.mod" --force
gen --seed 1 --output "$T/o1/e1.mod" --party-size 5 2>"$T/err.txt" && bad "party 5 accepted" || ok
grep -q "recipe supports" "$T/err.txt" && ok || bad "party error message"
gen --seed 1 --output "$T/o1/e2.mod" --max-level 5 2>/dev/null && bad "level 5 accepted" || ok
gen --seed 1 --output "$T/o1/e3.mod" --min-level 2 --max-level 3 --party-size 4 >/dev/null && ok || bad "in-range band rejected"
"$BUILD/nwndgen" --game nwn2 --base "$FX/base.mod" --recipe "$FX/recipe_test.txt" --seed 1 --output "$T/x.mod" 2>/dev/null
[ $? -eq 2 ] && ok || bad "--recipe with --game nwn2 must be a usage error"
"$BUILD/nwndgen" --game nwn1 --base "$FX/base.mod" --recipe "$FX/recipe_test.txt" --templates x --seed 1 --output "$T/x.mod" 2>/dev/null
[ $? -eq 2 ] && ok || bad "--recipe with --templates must be a usage error"
"$BUILD/nwndgen" --game nwn1 --base "$FX/base.mod" --seed 1 --output "$T/x.mod" 2>/dev/null
[ $? -eq 2 ] && ok || bad "missing --recipe/--templates must be a usage error"
[ -e "$T/x.mod" ] && bad "failed run left an output" || ok

# --- bad inputs fail cleanly, with no output file ---------------------------
badrun() { desc=$1; shift; rm -f "$T/bad.mod"
  if "$BUILD/nwndgen" --game nwn1 --base "$FX/base.mod" --seed 1 --output "$T/bad.mod" "$@" >/dev/null 2>"$T/err.txt"; then
    bad "$desc (expected failure)"; elif [ -e "$T/bad.mod" ]; then bad "$desc left an output"; else ok; fi; }
badrun "missing recipe file" --recipe "$FX/nope.txt"
cp "$FX/recipe_test.txt" "$FX/r_badkey.txt"; echo "bogus 1" >> "$FX/r_badkey.txt"
badrun "unknown recipe key" --recipe "$FX/r_badkey.txt"
sed 's/^tilepack .*/tilepack missing.txt/' "$FX/recipe_test.txt" > "$FX/r_notp.txt"
badrun "missing tilepack" --recipe "$FX/r_notp.txt"
sed 's/^template_git .*/template_git tpl_are.are/' "$FX/recipe_test.txt" > "$FX/r_wrongtype.txt"
badrun "wrong template type" --recipe "$FX/r_wrongtype.txt"
sed 's/^area ca .*/area ca 6 6 8 Cramped/' "$FX/recipe_test.txt" > "$FX/r_cramped.txt"
badrun "area too small for its rooms" --recipe "$FX/r_cramped.txt"
grep -q "area 'ca'" "$T/err.txt" && ok || bad "error names the area"
head -c 100 "$FX/tilepack_test.txt" > "$FX/tp_short.txt"
sed 's/^tilepack .*/tilepack tp_short.txt/' "$FX/recipe_test.txt" > "$FX/r_shorttp.txt"
badrun "incomplete tilepack" --recipe "$FX/r_shorttp.txt"

# --- independent validator: the generated modules obey the tileset's own rules -------------------
for seed in 1 2 3 12345 99999; do
  gen --seed $seed --output "$T/o1/v$seed.mod" >/dev/null || { bad "generate seed $seed for dgcheck"; continue; }
  "$BUILD/dgcheck" --set "$FX/tileset.set" --tileset tsttest "$T/o1/v$seed.mod" >"$T/dg.txt" 2>&1 && ok || bad "dgcheck seed $seed: $(grep BAD "$T/dg.txt" | head -3)"
done
grep -q "adjacency violations 0" "$T/dg.txt" && ok || bad "dgcheck reports adjacency counts"
checkfail "dgcheck rejects a module checked against the wrong tileset rules" "$BUILD/dgcheck" --set "$FX/tileset_bad.set" --tileset tsttest "$T/o1/v1.mod"
"$BUILD/dgcheck" --set "$FX/tileset_bad.set" --tileset tsttest "$T/o1/v1.mod" 2>&1 | grep -q "adjacency violations" && ok || bad "dgcheck names the violations"
checkfail "dgcheck rejects a module of the wrong tileset" "$BUILD/dgcheck" --set "$FX/tileset.set" --tileset tdc01 "$T/o1/v1.mod"
checkfail "dgcheck rejects a non-module" "$BUILD/dgcheck" --set "$FX/tileset.set" --tileset tsttest "$FX/recipe_test.txt"
[ "$("$BUILD/dgcheck" 2>/dev/null; echo $?)" = 2 ] && ok || bad "dgcheck without arguments should exit 2"

# --- layouts/tiles/portals equal the stored reference dumps -------------------
# (tests/data/expected_*.txt were produced by the independent reference model
# the retired Python reference model; the unit tests compare the same files in-process)
for s in 0 12345 18446744073709551615; do
  "$BUILD/dgdump" "$FX/recipe_test.txt" $s "$FX/base.ifo" > "$T/c.txt" 2>/dev/null
  cmp -s "$T/c.txt" "$HERE/data/expected_$s.txt" && ok || bad "dump for seed $s differs from tests/data/expected_$s.txt"
done

# --- creatures + objective (roster, scripts) -----------------------------------------------------
mkdir "$FX/scripts" && cp "$HERE/../assets/scripts/"* "$FX/scripts/" && cp "$HERE/data/roster_test.txt" "$FX/"
{ cat "$FX/recipe_test.txt"; echo "roster roster_test.txt"; echo "scripts scripts"; } > "$FX/recipe_spawn.txt"
gens() { "$BUILD/nwndgen" --game nwn1 --base "$FX/base.mod" --recipe "$FX/recipe_spawn.txt" "$@"; }
mkdir "$T/s1" "$T/s2"
gens --seed 12345 --output "$T/s1/a.mod" --manifest "$T/s1/a.txt" >/dev/null && ok || bad "spawn generation"
(cd "$T/s2" && TZ=Pacific/Kiritimati LC_ALL=C.UTF-8 \
  "$BUILD/nwndgen" --game nwn1 --base "$FX/base.mod" --recipe "$FX/recipe_spawn.txt" --seed 12345 \
  --output other.mod --manifest m.txt >/dev/null) || bad "spawn generation from another cwd"
check "spawn: same bytes across cwd/name/tz/locale" cmp "$T/s1/a.mod" "$T/s2/other.mod"
check "spawn: manifest identical across runs" cmp "$T/s1/a.txt" "$T/s2/m.txt"
for k in spawn_algorithm= roster_sha256= theme= script.scel_enter.ncs_sha256= script.scel_enter.nss_sha256= \
         script.scel_acquire.ncs_sha256= script.scel_acquire.nss_sha256= spawns.ca= spawns.cb= spawns.cc=; do
  grep -q "^$k" "$T/s1/a.txt" && ok || bad "spawn manifest lacks $k"
done
grep -q "$T" "$T/s1/a.txt" && bad "spawn manifest leaks a path" || ok
check "spawn: erf parses" "$BUILD/erfcheck" nwn1 "$T/s1/a.mod"
# plain 11 resources + module.jrl + scel_enter.{nss,ncs} + scel_acquire.{nss,ncs}
"$BUILD/erfcheck" nwn1 "$T/s1/a.mod" | grep -q "16 resources" && ok || bad "spawn resource count"
"$BUILD/erfcheck" nwn1 "$T/o1/a.mod" | grep -q "11 resources" && ok || bad "plain build gained resources"
# a recipe without roster/scripts is untouched by the feature: the same module as before
gen --seed 12345 --output "$T/s1/plain.mod" >/dev/null && cmp -s "$T/s1/plain.mod" "$T/o1/a.mod" && ok || bad "plain build changed"
for seed in 1 2 3 12345 99999; do
  gens --seed $seed --output "$T/s1/v$seed.mod" --party-size $((seed % 3 + 1)) --min-level 1 --max-level $((seed % 3 + 2)) >/dev/null || { bad "spawn generate seed $seed"; continue; }
  "$BUILD/dgcheck" --set "$FX/tileset.set" --tileset tsttest --roster "$FX/roster_test.txt" "$T/s1/v$seed.mod" >"$T/dg.txt" 2>&1 && ok || bad "dgcheck spawn seed $seed: $(grep BAD "$T/dg.txt" | head -3)"
done
grep -q "^spawn: " "$T/dg.txt" && ok || bad "dgcheck reports the spawn summary"
sed 's/^key .*/key t_other/' "$FX/roster_test.txt" > "$FX/roster_otherkey.txt"
checkfail "dgcheck rejects markers that disagree with the roster" "$BUILD/dgcheck" --set "$FX/tileset.set" --tileset tsttest --roster "$FX/roster_otherkey.txt" "$T/s1/v12345.mod"
# a module with the objective stripped is rejected: take a plain build and give it only the area hook
checkfail "dgcheck needs a roster file that parses" "$BUILD/dgcheck" --set "$FX/tileset.set" --tileset tsttest --roster "$FX/recipe_test.txt" "$T/s1/v1.mod"

badruns() { desc=$1; shift; rm -f "$T/bad.mod"
  if "$BUILD/nwndgen" --game nwn1 --base "$FX/base.mod" --seed 1 --output "$T/bad.mod" "$@" >/dev/null 2>"$T/err.txt"; then
    bad "$desc (expected failure)"; elif [ -e "$T/bad.mod" ]; then bad "$desc left an output"; else ok; fi; }
{ cat "$FX/recipe_test.txt"; echo "roster roster_test.txt"; } > "$FX/r_norost.txt"
badruns "roster without scripts" --recipe "$FX/r_norost.txt"
grep -q "together" "$T/err.txt" && ok || bad "roster/scripts error message"
{ cat "$FX/recipe_test.txt"; echo "spawn_mode boss_only"; } > "$FX/r_bossnoroster.txt"
badruns "boss_only without roster" --recipe "$FX/r_bossnoroster.txt"
{ cat "$FX/recipe_spawn.txt"; echo "spawn_mode sideways"; } > "$FX/r_badmode.txt"
badruns "unknown spawn_mode" --recipe "$FX/r_badmode.txt"
# boss_only: just the boss (1 hit point), still a valid module
{ cat "$FX/recipe_spawn.txt"; echo "spawn_mode boss_only"; } > "$FX/recipe_boss.txt"
"$BUILD/nwndgen" --game nwn1 --base "$FX/base.mod" --recipe "$FX/recipe_boss.txt" --seed 12345 --output "$T/s1/boss.mod" --manifest "$T/s1/boss.txt" >/dev/null && ok || bad "boss_only generation"
grep -q "^spawn_mode=boss_only" "$T/s1/boss.txt" && ok || bad "boss_only manifest"
grep -q "^spawn_mode=full" "$T/s1/a.txt" && ok || bad "full manifest"
"$BUILD/dgcheck" --set "$FX/tileset.set" --tileset tsttest --roster "$FX/roster_test.txt" "$T/s1/boss.mod" >"$T/dg.txt" 2>&1 && ok || bad "dgcheck boss_only: $(grep BAD "$T/dg.txt" | head -3)"
grep -q "boss has 1 hit point" "$T/dg.txt" && grep -q "^spawn: 0 creature" "$T/dg.txt" && ok || bad "dgcheck sees a boss-only module"
# scenery: props, lights, containers and the theme's area light come from the same roster
cp "$HERE/data/roster_deco_test.txt" "$FX/"
{ cat "$FX/recipe_test.txt"; echo "roster roster_deco_test.txt"; echo "scripts scripts"; } > "$FX/recipe_deco.txt"
for seed in 1 2 3 12345; do
  "$BUILD/nwndgen" --game nwn1 --base "$FX/base.mod" --recipe "$FX/recipe_deco.txt" --seed $seed --output "$T/s1/d$seed.mod" --manifest "$T/s1/d$seed.txt" >/dev/null || { bad "scenery generate seed $seed"; continue; }
  "$BUILD/dgcheck" --set "$FX/tileset.set" --tileset tsttest --roster "$FX/roster_deco_test.txt" "$T/s1/d$seed.mod" >"$T/dg.txt" 2>&1 && ok || bad "dgcheck scenery seed $seed: $(grep BAD "$T/dg.txt" | head -3)"
  grep -Eq "^spawn: [0-9]+ creature marker\(s\), [1-9][0-9]* decor, [1-9][0-9]* light, [1-9][0-9]* container" "$T/dg.txt" && ok || bad "scenery seed $seed placed nothing: $(grep '^spawn:' "$T/dg.txt")"
  for k in ambience= scenery.ca= scenery.cb= scenery.cc=; do grep -q "^$k" "$T/s1/d$seed.txt" && ok || bad "scenery manifest lacks $k"; done
done
grep -q "^scenery.ca=decor 0 light 0 container 0" "$T/s1/a.txt" && grep -q "^ambience=none" "$T/s1/a.txt" && ok || bad "a roster without scenery should list none"
"$BUILD/nwndgen" --game nwn1 --base "$FX/base.mod" --recipe "$FX/recipe_deco.txt" --seed 12345 --output "$T/s1/d_again.mod" --force >/dev/null && cmp -s "$T/s1/d_again.mod" "$T/s1/d12345.mod" && ok || bad "scenery build is not reproducible"
{ cat "$FX/recipe_deco.txt"; echo "spawn_mode boss_only"; } > "$FX/recipe_deco_boss.txt"
"$BUILD/nwndgen" --game nwn1 --base "$FX/base.mod" --recipe "$FX/recipe_deco_boss.txt" --seed 12345 --output "$T/s1/db.mod" --manifest "$T/s1/db.txt" >/dev/null && ok || bad "scenery boss_only generation"
grep -q "^scenery.ca=decor 0 light 0 container 0" "$T/s1/db.txt" && ok || bad "boss_only should place no scenery"
printf 'nwndgen-roster 1\nvault v\nkey k\nrelic r\ntheme a A\nminion m 1\nboss b 2\nprop p 0\n' > "$FX/roster_badprop.txt"
{ cat "$FX/recipe_test.txt"; echo "roster roster_badprop.txt"; echo "scripts scripts"; } > "$FX/r_badprop.txt"
badruns "a scenery weight of 0" --recipe "$FX/r_badprop.txt"
grep -q "weight" "$T/err.txt" && ok || bad "scenery weight error message"
printf 'nwndgen-roster 1\nvault v\nkey k\nrelic r\ntheme a A\nminion m 1\nboss b 2\nambience 1 2 3 4 5\n' > "$FX/roster_badamb.txt"
{ cat "$FX/recipe_test.txt"; echo "roster roster_badamb.txt"; echo "scripts scripts"; } > "$FX/r_badamb.txt"
badruns "a malformed ambience line" --recipe "$FX/r_badamb.txt"
grep -q "six hex digits" "$T/err.txt" && ok || bad "ambience error message"
{ cat "$FX/recipe_test.txt"; echo "roster nope.txt"; echo "scripts scripts"; } > "$FX/r_noroster.txt"
badruns "missing roster file" --recipe "$FX/r_noroster.txt"
{ cat "$FX/recipe_test.txt"; echo "roster roster_test.txt"; echo "scripts nowhere"; } > "$FX/r_noscripts.txt"
badruns "missing scripts directory" --recipe "$FX/r_noscripts.txt"
mkdir "$FX/scripts_bad" && cp "$FX/scripts/"* "$FX/scripts_bad/" && printf 'not a script' > "$FX/scripts_bad/scel_enter.ncs"
{ cat "$FX/recipe_test.txt"; echo "roster roster_test.txt"; echo "scripts scripts_bad"; } > "$FX/r_badncs.txt"
badruns "scripts that are not NCS" --recipe "$FX/r_badncs.txt"
grep -q "NCS" "$T/err.txt" && ok || bad "NCS error message"
printf 'nwndgen-roster 1\nvault v\nkey k\nrelic r\ntheme a A\nminion m 1\nboss b 2\n' > "$FX/roster_hard.txt"
{ cat "$FX/recipe_test.txt"; echo "roster roster_hard.txt"; echo "scripts scripts"; } > "$FX/r_hard.txt"
badruns "no theme can serve a level-1 solo with a CR 2 boss" --recipe "$FX/r_hard.txt" --max-level 1
printf 'nwndgen-roster 1\nvault v\nkey k\nrelic r\ntheme a A\nminion m 1\nboss b 7\n' > "$FX/roster_hard.txt"
badruns "boss above the band cap" --recipe "$FX/r_hard.txt" --max-level 2
grep -q "no theme with a boss" "$T/err.txt" && ok || bad "cap error message"


# --- corridor kit + plain doors (opt-in) ------------------------------------------------------------
{ cat <<'REC'
nwndgen-recipe 1
tilepack tilepack_kit.txt
template_are tpl_are.are
template_git tpl_git.git
template_door tpl_door.git
module_name Kit Test
module_tag KIT_TEST
module_description Synthetic kit fixture.
support 1 4 1 4
corridors kit
doors plain
area ka 14 14 3 Kit A
area kb 14 14 3 Kit B
area kc 16 16 4 Kit C
REC
} > "$FX/recipe_kit.txt"
genk() { "$BUILD/nwndgen" --game nwn1 --base "$FX/base.mod" --recipe "$FX/recipe_kit.txt" "$@"; }
dgk() { "$BUILD/dgcheck" --set "$FX/${KSET:-tileset_kit.set}" --tileset tsttest "$@"; }
mkdir "$T/k"
ndoors=0
for seed in 1 2 3 4 5 12345; do
  genk --seed $seed --output "$T/k/k$seed.mod" --manifest "$T/k/k$seed.txt" >/dev/null || { bad "kit generate seed $seed"; continue; }
  dgk "$T/k/k$seed.mod" >"$T/dg.txt" 2>&1 && ok || bad "dgcheck kit seed $seed: $(grep BAD "$T/dg.txt" | head -3)"
  grep -q "adjacency violations 0" "$T/dg.txt" && ok || bad "kit seed $seed adjacency"
  n=$(grep -c '^doors\.' "$T/k/k$seed.txt"); [ "$n" -eq 3 ] && ok || bad "kit seed $seed doors keys ($n)"
  ndoors=$((ndoors + $(sed -n 's/^doors\.[a-z]*=//p' "$T/k/k$seed.txt" | awk '{s += $1} END {print s + 0}')))
done
[ "$ndoors" -gt 0 ] && ok || bad "no kit seed produced a door"
for k in dungeon_algorithm=dungeon-chain/2 corridors=kit doors=plain template_door_sha256= doors.ka= doors.kb= doors.kc=; do
  grep -q "^$k" "$T/k/k1.txt" && ok || bad "kit manifest lacks $k"
done
grep -q "$T" "$T/k/k1.txt" && bad "kit manifest leaks a path" || ok
check "kit: erf parses" "$BUILD/erfcheck" nwn1 "$T/k/k1.mod"
genk --seed 12345 --output "$T/k/again.mod" --force >/dev/null && cmp -s "$T/k/again.mod" "$T/k/k12345.mod" && ok || bad "kit build is not reproducible"
checkfail "kit differs from the plain build of the same seed" cmp -s "$T/k/k12345.mod" "$T/o1/a.mod"
gen --seed 12345 --output "$T/k/plain.mod" >/dev/null && cmp -s "$T/k/plain.mod" "$T/o1/a.mod" && ok || bad "plain build changed after the kit work"
# --- doors mixed: every door look the pack has (the fixture has type 22 and type 23 door tiles) -------
sed 's/^doors plain/doors mixed/' "$FX/recipe_kit.txt" > "$FX/recipe_kit_mixed.txt"
looks22=0; looks23=0
for seed in 1 2 3 4 5 6 7 8; do
  "$BUILD/nwndgen" --game nwn1 --base "$FX/base.mod" --recipe "$FX/recipe_kit_mixed.txt" --seed $seed --output "$T/k/x$seed.mod" --manifest "$T/k/x$seed.txt" >/dev/null || { bad "doors mixed generate seed $seed"; continue; }
  dgk "$T/k/x$seed.mod" >"$T/dg.txt" 2>&1 && ok || bad "dgcheck doors mixed seed $seed: $(grep BAD "$T/dg.txt" | head -3)"
  grep -q '^doors=mixed$' "$T/k/x$seed.txt" && grep -q '^door_looks=' "$T/k/x$seed.txt" && ok || bad "doors mixed manifest seed $seed"
  looks22=$((looks22 + $(sed -n 's/^door_looks=//p' "$T/k/x$seed.txt" | tr ',' '\n' | sed -n 's/^22://p' | awk '{s += $1} END {print s + 0}')))
  looks23=$((looks23 + $(sed -n 's/^door_looks=//p' "$T/k/x$seed.txt" | tr ',' '\n' | sed -n 's/^23://p' | awk '{s += $1} END {print s + 0}')))
done
[ "$looks22" -gt 0 ] && [ "$looks23" -gt 0 ] && ok || bad "doors mixed should use both looks (22: $looks22, 23: $looks23)"
# doors plain ignores the second look: the same bytes whether or not the pack lists it
grep -v ' 23$' "$FX/tilepack_kit.txt" > "$FX/tilepack_kit_no23.txt"
sed 's/^tilepack tilepack_kit.txt/tilepack tilepack_kit_no23.txt/' "$FX/recipe_kit.txt" > "$FX/recipe_kit_no23.txt"
"$BUILD/nwndgen" --game nwn1 --base "$FX/base.mod" --recipe "$FX/recipe_kit_no23.txt" --seed 12345 --output "$T/k/no23.mod" >/dev/null && cmp -s "$T/k/no23.mod" "$T/k/k12345.mod" && ok || bad "doors plain changed because the pack lists another look"
"$BUILD/nwndgen" --game nwn1 --base "$FX/base.mod" --recipe "$FX/recipe_kit.txt" --seed 12345 --output "$T/k/pl.mod" --manifest "$T/k/pl.txt" --force >/dev/null && ! grep -q '^door_looks' "$T/k/pl.txt" && ok || bad "doors plain manifest lists looks"
# the validator reads the door type from the tileset: a lie about the look must show
KSET=tileset_kit_look.set; sed 's/^Type=23/Type=22/' "$FX/tileset_kit.set" > "$FX/$KSET"
nbadlook=0
for seed in 1 2 3 4 5 6 7 8; do dgk "$T/k/x$seed.mod" >/dev/null 2>&1 || nbadlook=$((nbadlook + 1)); done
[ "$nbadlook" -gt 0 ] && ok || bad "dgcheck accepted every type-23 door with the tileset saying 22"
{ cat "$FX/recipe_kit.txt"; } | sed 's/^doors plain/doors sideways/' > "$FX/r_badlook.txt"
badruns "doors sideways" --recipe "$FX/r_badlook.txt"

# --- doors doorway: a generic-look door in every corridor entrance (Doorway edge tiles) -----------------
sed 's/^doors plain/doors doorway/' "$FX/recipe_kit.txt" > "$FX/recipe_kit_doorway.txt"
dwdoors=0; dwlooks=""
for seed in 1 2 3 4 5 6 7 8 9 10; do
  "$BUILD/nwndgen" --game nwn1 --base "$FX/base.mod" --recipe "$FX/recipe_kit_doorway.txt" --seed $seed --output "$T/k/w$seed.mod" --manifest "$T/k/w$seed.txt" >"$T/k/w$seed.out" || { bad "doors doorway generate seed $seed"; continue; }
  dgk "$T/k/w$seed.mod" >"$T/dg.txt" 2>&1 && ok || bad "dgcheck doors doorway seed $seed: $(grep BAD "$T/dg.txt" | head -3)"
  grep -q "adjacency violations 0" "$T/dg.txt" && ok || bad "doors doorway seed $seed adjacency"
  grep -q '^doors=doorway$' "$T/k/w$seed.txt" && ok || bad "doors doorway manifest seed $seed"
  dwdoors=$((dwdoors + $(sed -n 's/^doors\.[a-z]*=//p' "$T/k/w$seed.txt" | awk '{s += $1} END {print s + 0}')))
  dwlooks="$dwlooks,$(sed -n 's/^door_looks=//p' "$T/k/w$seed.txt")"
done
[ "$dwdoors" -gt 0 ] && ok || bad "doors doorway produced no door"
for g in generic0 generic2 generic4; do
  case "$dwlooks" in *$g:*) ok ;; *) bad "doors doorway never used look $g ($dwlooks)" ;; esac
done
grep -q '^  Door looks: ' "$T/k/w1.out" && ok || bad "doors doorway summary lacks Door looks"
"$BUILD/nwndgen" --game nwn1 --base "$FX/base.mod" --recipe "$FX/recipe_kit_doorway.txt" --seed 3 --output "$T/k/again_w.mod" >/dev/null && cmp -s "$T/k/again_w.mod" "$T/k/w3.mod" && ok || bad "doors doorway is not reproducible"
# the Doorway records do not change plain, mixed or wide builds: same bytes with and without them in the pack
awk '$1 == "kit" && $3 ~ /D/ {next} {print}' "$FX/tilepack_kit.txt" > "$FX/tilepack_kit_nodw.txt"
for mode in recipe_kit recipe_kit_mixed; do
  sed 's/^tilepack tilepack_kit.txt/tilepack tilepack_kit_nodw.txt/' "$FX/$mode.txt" > "$FX/${mode}_nodw.txt"
  for seed in 5 12345; do
    "$BUILD/nwndgen" --game nwn1 --base "$FX/base.mod" --recipe "$FX/$mode.txt" --seed $seed --output "$T/k/a.mod" --force >/dev/null && "$BUILD/nwndgen" --game nwn1 --base "$FX/base.mod" --recipe "$FX/${mode}_nodw.txt" --seed $seed --output "$T/k/b.mod" --force >/dev/null && cmp -s "$T/k/a.mod" "$T/k/b.mod" && ok || bad "$mode seed $seed changed because the pack lists Doorway tiles"
  done
done
# an old template without generic donors is refused, and so is a pack without Doorway tiles
sed 's/^template_door tpl_door.git/template_door tpl_door1.git/' "$FX/recipe_kit_doorway.txt" > "$FX/r_dw_old.txt"
badruns "doors doorway with an old template_door" --recipe "$FX/r_dw_old.txt"
grep -q "nwnpack templates" "$T/err.txt" && ok || bad "doors doorway old template message"
sed 's/^tilepack tilepack_kit.txt/tilepack tilepack_kit_nodw.txt/' "$FX/recipe_kit_doorway.txt" > "$FX/r_dw_nopack.txt"
"$BUILD/nwndgen" --game nwn1 --base "$FX/base.mod" --recipe "$FX/r_dw_nopack.txt" --seed 3 --output "$T/k/nopack.mod" --manifest "$T/k/nopack.txt" --force >/dev/null && ! grep -q '^door_looks=.' "$T/k/nopack.txt" && ok || bad "doors doorway without Doorway tiles should build with no doorway doors"
# negative proofs: the validator reads the tileset, so lies about edges or door slots must show
KSET=tileset_kit_dwedge.set; sed 's/^Left=Doorway/Left=Corridor/' "$FX/tileset_kit.set" > "$FX/$KSET"
nbad=0; for seed in 1 2 3 4 5; do dgk "$T/k/w$seed.mod" >/dev/null 2>&1 || nbad=$((nbad + 1)); done
[ "$nbad" -gt 0 ] && ok || bad "dgcheck accepted Doorway tiles whose left edges the tileset calls Corridor"
KSET=tileset_kit_dwtype.set; sed 's/^Type=0$/Type=22/' "$FX/tileset_kit.set" > "$FX/$KSET"
nbad=0; for seed in 1 2 3 4 5; do dgk "$T/k/w$seed.mod" >/dev/null 2>&1 || nbad=$((nbad + 1)); done
[ "$nbad" -gt 0 ] && ok || bad "dgcheck accepted generic doors on a slot the tileset types as 22"
sed 's/^doors plain/doors sideways/' "$FX/recipe_kit.txt" > "$FX/r_badlook2.txt"
badruns "doors sideways (message lists doorway)" --recipe "$FX/r_badlook2.txt"
grep -q "doorway" "$T/err.txt" && ok || bad "doors error should list doorway"

# kit corridors without doors
sed '/^doors plain/d; /^template_door/d' "$FX/recipe_kit.txt" > "$FX/recipe_kit_nodoor.txt"
"$BUILD/nwndgen" --game nwn1 --base "$FX/base.mod" --recipe "$FX/recipe_kit_nodoor.txt" --seed 3 --output "$T/k/nd.mod" --manifest "$T/k/nd.txt" >/dev/null && ok || bad "kit without doors generation"
grep -q "^doors=none" "$T/k/nd.txt" && ok || bad "kit without doors manifest"
dgk "$T/k/nd.mod" >"$T/dg.txt" 2>&1 && ok || bad "dgcheck kit without doors: $(grep BAD "$T/dg.txt" | head -3)"
grep -q " 0 doors" "$T/dg.txt" && ok || bad "kit without doors should have none"
# kit + creatures + scenery
{ cat "$FX/recipe_kit.txt"; echo "roster roster_deco_test.txt"; echo "scripts scripts"; } > "$FX/recipe_kit_deco.txt"
for seed in 1 2 3 12345; do
  "$BUILD/nwndgen" --game nwn1 --base "$FX/base.mod" --recipe "$FX/recipe_kit_deco.txt" --seed $seed --output "$T/k/d$seed.mod" >/dev/null || { bad "kit scenery generate seed $seed"; continue; }
  dgk --roster "$FX/roster_deco_test.txt" "$T/k/d$seed.mod" >"$T/dg.txt" 2>&1 && ok || bad "dgcheck kit scenery seed $seed: $(grep BAD "$T/dg.txt" | head -3)"
done
# corridor_dressing off|low|mid|high: props along the straight kit corridors
cp "$HERE/data/roster_corr_test.txt" "$HERE/data/roster_dress_test.txt" "$FX/"
{ cat "$FX/recipe_kit.txt"; echo "roster roster_corr_test.txt"; echo "scripts scripts"; } > "$FX/recipe_kit_corr.txt"
mkdir -p "$T/cd"
"$BUILD/nwndgen" --game nwn1 --base "$FX/base.mod" --recipe "$FX/recipe_kit_corr.txt" --seed 12345 --output "$T/cd/off.mod" --manifest "$T/cd/off.txt" >/dev/null || bad "corridor_dressing baseline generate"
! grep -q 'corridor_' "$T/cd/off.txt" && ok || bad "corridor_dressing appears in the manifest without the recipe key"
cprops=0; cmod=
for lvl in low mid high; do
  { cat "$FX/recipe_kit_corr.txt"; echo "corridor_dressing $lvl"; } > "$FX/recipe_kit_corr_$lvl.txt"
  for seed in 1 2 3 4 12345; do
    "$BUILD/nwndgen" --game nwn1 --base "$FX/base.mod" --recipe "$FX/recipe_kit_corr_$lvl.txt" --seed $seed --output "$T/cd/${lvl}_$seed.mod" --manifest "$T/cd/${lvl}_$seed.txt" >"$T/cd/${lvl}_$seed.out" || { bad "corridor_dressing $lvl generate seed $seed"; continue; }
    grep -q "^corridor_dressing=$lvl\$" "$T/cd/${lvl}_$seed.txt" && grep -q '^corridor_algorithm=corridor/1$' "$T/cd/${lvl}_$seed.txt" && grep -q '^corridor_props\.[a-z_0-9]*=[0-9]*$' "$T/cd/${lvl}_$seed.txt" && ok || bad "corridor_dressing $lvl manifest seed $seed"
    grep -q 'corridor props (corridor_dressing '"$lvl"')' "$T/cd/${lvl}_$seed.out" && ok || bad "corridor_dressing $lvl summary seed $seed"
    dgk --roster "$FX/roster_corr_test.txt" "$T/cd/${lvl}_$seed.mod" >"$T/dg.txt" 2>&1 && ok || bad "dgcheck corridor_dressing $lvl seed $seed: $(grep BAD "$T/dg.txt" | head -3)"
    n=$(sed -n 's/^corridor_props\.[a-z_0-9]*=//p' "$T/cd/${lvl}_$seed.txt" | paste -sd+ | bc 2>/dev/null || echo 0); cprops=$((cprops + ${n:-0})); [ "${n:-0}" -gt 0 ] && cmod="$T/cd/${lvl}_$seed.mod"
  done
done
[ "$cprops" -gt 0 ] && ok || bad "corridor_dressing placed no props over 15 modules"
"$BUILD/nwndgen" --game nwn1 --base "$FX/base.mod" --recipe "$FX/recipe_kit_corr_high.txt" --seed 12345 --output "$T/cd/again.mod" >/dev/null && cmp -s "$T/cd/again.mod" "$T/cd/high_12345.mod" && ok || bad "corridor_dressing build is not reproducible"
{ cat "$FX/recipe_kit_corr.txt"; echo "corridor_dressing off"; } > "$FX/recipe_kit_corr_off.txt"
"$BUILD/nwndgen" --game nwn1 --base "$FX/base.mod" --recipe "$FX/recipe_kit_corr_off.txt" --seed 12345 --output "$T/cd/offkey.mod" >/dev/null && cmp -s "$T/cd/offkey.mod" "$T/cd/off.mod" && ok || bad "corridor_dressing off differs from leaving the key out"
# a pack without prop spots (made before this feature) still builds, with no corridor props
sed 's/ props [0-9]*$//' "$FX/tilepack_kit.txt" > "$FX/tilepack_kit_noprops.txt"
sed 's/^tilepack tilepack_kit.txt/tilepack tilepack_kit_noprops.txt/' "$FX/recipe_kit_corr_high.txt" > "$FX/recipe_kit_corr_nopack.txt"
"$BUILD/nwndgen" --game nwn1 --base "$FX/base.mod" --recipe "$FX/recipe_kit_corr_nopack.txt" --seed 12345 --output "$T/cd/nopack.mod" --manifest "$T/cd/nopack.txt" >/dev/null && grep -q '^corridor_props\.[a-z_0-9]*=0$' "$T/cd/nopack.txt" && ! grep -q '^corridor_props\.[a-z_0-9]*=[1-9]' "$T/cd/nopack.txt" && ok || bad "old pack: corridor_dressing should place nothing"
# negative proof: against a roster without those corridor props dgcheck must object
[ -n "$cmod" ] && { dgk --roster "$FX/roster_dress_test.txt" "$cmod" >"$T/dg.txt" 2>&1 && bad "dgcheck accepted corridor props outside the roster" || { grep -q "is not in the prop list" "$T/dg.txt" && ok || bad "dgcheck corridor prop message: $(head -3 "$T/dg.txt")"; }; }
{ cat "$FX/recipe_test.txt"; echo "roster roster_corr_test.txt"; echo "scripts scripts"; echo "corridor_dressing high"; } > "$FX/r_cnokit.txt"
badruns "corridor_dressing without corridors kit" --recipe "$FX/r_cnokit.txt"
{ cat "$FX/recipe_kit.txt"; echo "corridor_dressing high"; } > "$FX/r_cnoroster.txt"
badruns "corridor_dressing without roster" --recipe "$FX/r_cnoroster.txt"
{ cat "$FX/recipe_kit_corr_high.txt"; echo "corridor_dressing low"; } > "$FX/r_cdup.txt"
badruns "corridor_dressing given twice" --recipe "$FX/r_cdup.txt"
{ cat "$FX/recipe_kit_corr.txt"; echo "corridor_dressing lots"; } > "$FX/r_cbad.txt"
badruns "corridor_dressing lots" --recipe "$FX/r_cbad.txt"
# locks off|low|mid|high: locked corridor doors (pickable, or keyed with the key on a creature in an earlier room) and locked containers
mkdir -p "$T/lk"
"$BUILD/nwndgen" --game nwn1 --base "$FX/base.mod" --recipe "$FX/recipe_kit_corr.txt" --seed 12345 --output "$T/lk/off.mod" --manifest "$T/lk/off.txt" >/dev/null || bad "locks baseline generate"
! grep -q 'locks' "$T/lk/off.txt" && ok || bad "locks appears in the manifest without the recipe key"
ldoors=0; lkeyed=0; lchests=0; lkmod=
for lvl in low mid high; do
  { cat "$FX/recipe_kit_corr.txt"; echo "locks $lvl"; } > "$FX/recipe_kit_lock_$lvl.txt"
  for seed in 1 2 3 4 5 6 7 8 12345; do
    "$BUILD/nwndgen" --game nwn1 --base "$FX/base.mod" --recipe "$FX/recipe_kit_lock_$lvl.txt" --seed $seed --output "$T/lk/${lvl}_$seed.mod" --manifest "$T/lk/${lvl}_$seed.txt" >"$T/lk/${lvl}_$seed.out" || { bad "locks $lvl generate seed $seed"; continue; }
    grep -q "^locks=$lvl\$" "$T/lk/${lvl}_$seed.txt" && grep -q '^locks_algorithm=locks/1$' "$T/lk/${lvl}_$seed.txt" && grep -q '^locks\.[a-z_0-9]*=doors [0-9]*, keyed [0-9]*, chests [0-9]*$' "$T/lk/${lvl}_$seed.txt" && ok || bad "locks $lvl manifest seed $seed"
    grep -q 'locks (locks '"$lvl"')' "$T/lk/${lvl}_$seed.out" && ok || bad "locks $lvl summary seed $seed"
    dgk --roster "$FX/roster_corr_test.txt" "$T/lk/${lvl}_$seed.mod" >"$T/dg.txt" 2>&1 && ok || bad "dgcheck locks $lvl seed $seed: $(grep BAD "$T/dg.txt" | head -3)"
    eval "$(sed -n 's/^locks\.[a-z_0-9]*=doors \([0-9]*\), keyed \([0-9]*\), chests \([0-9]*\)$/ldoors=$((ldoors + \1)); lkeyed=$((lkeyed + \2)); lchests=$((lchests + \3));/p' "$T/lk/${lvl}_$seed.txt" | paste -sd' ')"
    grep -q 'keyed [1-9]' "$T/lk/${lvl}_$seed.txt" && lkmod="$T/lk/${lvl}_$seed.mod"
  done
done
[ "$ldoors" -gt 0 ] && ok || bad "locks locked no door over 27 modules"
[ "$lkeyed" -gt 0 ] && ok || bad "locks made no keyed door over 27 modules"
[ "$lchests" -ge 0 ] && ok
"$BUILD/nwndgen" --game nwn1 --base "$FX/base.mod" --recipe "$FX/recipe_kit_lock_high.txt" --seed 12345 --output "$T/lk/again.mod" >/dev/null && cmp -s "$T/lk/again.mod" "$T/lk/high_12345.mod" && ok || bad "locks build is not reproducible"
{ cat "$FX/recipe_kit_corr.txt"; echo "locks off"; } > "$FX/recipe_kit_lock_off.txt"
"$BUILD/nwndgen" --game nwn1 --base "$FX/base.mod" --recipe "$FX/recipe_kit_lock_off.txt" --seed 12345 --output "$T/lk/offkey.mod" >/dev/null && cmp -s "$T/lk/offkey.mod" "$T/lk/off.mod" && ok || bad "locks off differs from leaving the key out"
# negative proof: a key the door names but no creature carries
if [ -n "$lkmod" ]; then
  kt=$(LC_ALL=C grep -a -o 'KEY_[a-z_0-9]*_1' "$lkmod" | head -1)
  off=$(LC_ALL=C grep -a -b -o "$kt" "$lkmod" | head -1 | cut -d: -f1) # change the last character of the first copy of the tag only
  cp "$lkmod" "$T/lk/badkey.mod" && printf 9 | dd of="$T/lk/badkey.mod" bs=1 seek=$((off + ${#kt} - 1)) conv=notrunc 2>/dev/null
  dgk --roster "$FX/roster_corr_test.txt" "$T/lk/badkey.mod" >"$T/dg.txt" 2>&1 && bad "dgcheck accepted a door key nobody carries" || { grep -q "creatures in the area carry it\|opens 0 doors" "$T/dg.txt" && ok || bad "dgcheck key message: $(head -3 "$T/dg.txt")"; }
else bad "locks: no keyed module to run the negative proof on"; fi
{ cat "$FX/recipe_kit.txt"; echo "locks high"; } > "$FX/r_lnoroster.txt"
badruns "locks without roster" --recipe "$FX/r_lnoroster.txt"
{ cat "$FX/recipe_test.txt"; echo "roster roster_corr_test.txt"; echo "scripts scripts"; echo "locks high"; } > "$FX/r_lnokit.txt"
badruns "locks without corridors kit" --recipe "$FX/r_lnokit.txt"
{ cat "$FX/recipe_kit_lock_high.txt"; echo "locks low"; } > "$FX/r_ldup.txt"
badruns "locks given twice" --recipe "$FX/r_ldup.txt"
{ cat "$FX/recipe_kit_corr.txt"; echo "locks lots"; } > "$FX/r_lbad.txt"
badruns "locks lots" --recipe "$FX/r_lbad.txt"
# ambush off|low|mid|high: some room creatures stay hidden until a chest is opened or a player comes near; the creatures are the ones already planned
mkdir -p "$T/am"
"$BUILD/nwndgen" --game nwn1 --base "$FX/base.mod" --recipe "$FX/recipe_kit_corr.txt" --seed 12345 --output "$T/am/off.mod" --manifest "$T/am/off.txt" >"$T/am/off.out" || bad "ambush baseline generate"
! grep -q 'ambush' "$T/am/off.txt" && ok || bad "ambush appears in the manifest without the recipe key"
agroups=0; ahidden=0; amod=
for lvl in low mid high; do
  { cat "$FX/recipe_kit_corr.txt"; echo "ambush $lvl"; } > "$FX/recipe_kit_amb_$lvl.txt"
  for seed in 1 2 3 4 5 6 7 8 12345; do
    "$BUILD/nwndgen" --game nwn1 --base "$FX/base.mod" --recipe "$FX/recipe_kit_amb_$lvl.txt" --seed $seed --output "$T/am/${lvl}_$seed.mod" --manifest "$T/am/${lvl}_$seed.txt" >"$T/am/${lvl}_$seed.out" || { bad "ambush $lvl generate seed $seed"; continue; }
    grep -q "^ambush=$lvl\$" "$T/am/${lvl}_$seed.txt" && grep -q '^ambush_algorithm=ambush/1$' "$T/am/${lvl}_$seed.txt" && grep -q '^ambush\.[a-z_0-9]*=groups [0-9]*, hidden [0-9]*, chests [0-9]*$' "$T/am/${lvl}_$seed.txt" && grep -q '^script.scel_beat.ncs_sha256=' "$T/am/${lvl}_$seed.txt" && ok || bad "ambush $lvl manifest seed $seed"
    grep -q 'ambush (ambush '"$lvl"')' "$T/am/${lvl}_$seed.out" && ok || bad "ambush $lvl summary seed $seed"
    dgk --roster "$FX/roster_corr_test.txt" "$T/am/${lvl}_$seed.mod" >"$T/dg.txt" 2>&1 && ok || bad "dgcheck ambush $lvl seed $seed: $(grep BAD "$T/dg.txt" | head -3)"
    eval "$(sed -n 's/^ambush\.[a-z_0-9]*=groups \([0-9]*\), hidden \([0-9]*\), chests [0-9]*$/agroups=$((agroups + \1)); ahidden=$((ahidden + \2));/p' "$T/am/${lvl}_$seed.txt" | paste -sd' ')"
    grep -q 'groups [1-9]' "$T/am/${lvl}_$seed.txt" && amod="$T/am/${lvl}_$seed.mod"
    # hidden creatures are planned ones: the creature and CR totals equal the same seed without the key
    "$BUILD/nwndgen" --game nwn1 --base "$FX/base.mod" --recipe "$FX/recipe_kit_corr.txt" --seed $seed --output "$T/am/plain_$seed.mod" --force >"$T/am/plain_$seed.out" 2>&1
    [ "$(grep -E 'creatures [0-9]+|total CR' "$T/am/${lvl}_$seed.out")" = "$(grep -E 'creatures [0-9]+|total CR' "$T/am/plain_$seed.out")" ] && ok || bad "ambush $lvl changed the creature totals, seed $seed"
  done
done
[ "$agroups" -gt 0 ] && [ "$ahidden" -ge $((agroups * 2)) ] && ok || bad "ambush planned no groups over 27 modules ($agroups groups, $ahidden hidden)"
"$BUILD/nwndgen" --game nwn1 --base "$FX/base.mod" --recipe "$FX/recipe_kit_amb_high.txt" --seed 12345 --output "$T/am/again.mod" >/dev/null && cmp -s "$T/am/again.mod" "$T/am/high_12345.mod" && ok || bad "ambush build is not reproducible"
{ cat "$FX/recipe_kit_corr.txt"; echo "ambush off"; } > "$FX/recipe_kit_amb_off.txt"
"$BUILD/nwndgen" --game nwn1 --base "$FX/base.mod" --recipe "$FX/recipe_kit_amb_off.txt" --seed 12345 --output "$T/am/offkey.mod" >/dev/null && cmp -s "$T/am/offkey.mod" "$T/am/off.mod" && ok || bad "ambush off differs from leaving the key out"
# negative proof: markers that lose their group id while the area still runs the heartbeat script
if [ -n "$amod" ]; then
  LC_ALL=C sed 's/scel_amb/scel_amc/g' "$amod" > "$T/am/nogroup.mod"
  dgk --roster "$FX/roster_corr_test.txt" "$T/am/nogroup.mod" >"$T/dg.txt" 2>&1 && bad "dgcheck accepted a heartbeat script without ambush groups" || { grep -q "OnHeartbeat is" "$T/dg.txt" && ok || bad "dgcheck heartbeat message: $(head -3 "$T/dg.txt")"; }
else bad "ambush: no module with a group to run the negative proof on"; fi
{ cat "$FX/recipe_kit.txt"; echo "ambush high"; } > "$FX/r_anoroster.txt"
badruns "ambush without roster" --recipe "$FX/r_anoroster.txt"
{ cat "$FX/recipe_kit_amb_high.txt"; echo "ambush low"; } > "$FX/r_adup.txt"
badruns "ambush given twice" --recipe "$FX/r_adup.txt"
{ cat "$FX/recipe_kit_corr.txt"; echo "ambush lots"; } > "$FX/r_abad.txt"
badruns "ambush lots" --recipe "$FX/r_abad.txt"
# traps off|low|mid|high: hidden floor traps (rooms and straight corridor cells) and trapped containers, strength by level
mkdir -p "$T/tr"
"$BUILD/nwndgen" --game nwn1 --base "$FX/base.mod" --recipe "$FX/recipe_kit_corr.txt" --seed 12345 --output "$T/tr/off.mod" --manifest "$T/tr/off.txt" >/dev/null || bad "traps baseline generate"
! grep -q 'traps' "$T/tr/off.txt" && ok || bad "traps appears in the manifest without the recipe key"
tfloor=0; tchest=0; tmod=
for lvl in low mid high; do
  { cat "$FX/recipe_kit_corr.txt"; echo "traps $lvl"; } > "$FX/recipe_kit_trap_$lvl.txt"
  for seed in 1 2 3 4 5 6 7 8 12345; do
    "$BUILD/nwndgen" --game nwn1 --base "$FX/base.mod" --recipe "$FX/recipe_kit_trap_$lvl.txt" --seed $seed --output "$T/tr/${lvl}_$seed.mod" --manifest "$T/tr/${lvl}_$seed.txt" >"$T/tr/${lvl}_$seed.out" || { bad "traps $lvl generate seed $seed"; continue; }
    grep -q "^traps=$lvl\$" "$T/tr/${lvl}_$seed.txt" && grep -q '^traps_algorithm=traps/1$' "$T/tr/${lvl}_$seed.txt" && grep -q '^traps\.[a-z_0-9]*=floor [0-9]*, chests [0-9]*$' "$T/tr/${lvl}_$seed.txt" && ok || bad "traps $lvl manifest seed $seed"
    grep -q 'traps (traps '"$lvl"')' "$T/tr/${lvl}_$seed.out" && ok || bad "traps $lvl summary seed $seed"
    dgk --roster "$FX/roster_corr_test.txt" "$T/tr/${lvl}_$seed.mod" >"$T/dg.txt" 2>&1 && ok || bad "dgcheck traps $lvl seed $seed: $(grep BAD "$T/dg.txt" | head -3)"
    eval "$(sed -n 's/^traps\.[a-z_0-9]*=floor \([0-9]*\), chests \([0-9]*\)$/tfloor=$((tfloor + \1)); tchest=$((tchest + \2));/p' "$T/tr/${lvl}_$seed.txt" | paste -sd' ')"
    grep -q 'floor [1-9]' "$T/tr/${lvl}_$seed.txt" && tmod="$T/tr/${lvl}_$seed.mod"
  done
done
[ "$tfloor" -gt 0 ] && ok || bad "traps placed no floor trap over 27 modules"
"$BUILD/nwndgen" --game nwn1 --base "$FX/base.mod" --recipe "$FX/recipe_kit_trap_high.txt" --seed 12345 --output "$T/tr/again.mod" >/dev/null && cmp -s "$T/tr/again.mod" "$T/tr/high_12345.mod" && ok || bad "traps build is not reproducible"
{ cat "$FX/recipe_kit_corr.txt"; echo "traps off"; } > "$FX/recipe_kit_trap_off.txt"
"$BUILD/nwndgen" --game nwn1 --base "$FX/base.mod" --recipe "$FX/recipe_kit_trap_off.txt" --seed 12345 --output "$T/tr/offkey.mod" >/dev/null && cmp -s "$T/tr/offkey.mod" "$T/tr/off.mod" && ok || bad "traps off differs from leaving the key out"
# negative proofs: an unknown trap type, and a trap marker without its DC
if [ -n "$tmod" ]; then
  LC_ALL=C sed 's/scel_trapdc/scel_trapdd/g' "$tmod" > "$T/tr/nodc.mod"
  dgk --roster "$FX/roster_corr_test.txt" "$T/tr/nodc.mod" >"$T/dg.txt" 2>&1 && bad "dgcheck accepted a trap marker without a DC" || { grep -q "lacks a valid type, DC" "$T/dg.txt" && ok || bad "dgcheck trap message: $(head -3 "$T/dg.txt")"; }
else bad "traps: no module with a floor trap to run the negative proof on"; fi
{ cat "$FX/recipe_kit.txt"; echo "traps high"; } > "$FX/r_tnoroster.txt"
badruns "traps without roster" --recipe "$FX/r_tnoroster.txt"
{ cat "$FX/recipe_kit_trap_high.txt"; echo "traps low"; } > "$FX/r_tdup.txt"
badruns "traps given twice" --recipe "$FX/r_tdup.txt"
{ cat "$FX/recipe_kit_corr.txt"; echo "traps lots"; } > "$FX/r_tbad.txt"
badruns "traps lots" --recipe "$FX/r_tbad.txt"
# levers on|off: one door per area becomes a gate that a lever in an earlier room opens (needs locks)
mkdir -p "$T/lv"
"$BUILD/nwndgen" --game nwn1 --base "$FX/base.mod" --recipe "$FX/recipe_kit_lock_high.txt" --seed 12345 --output "$T/lv/off.mod" --manifest "$T/lv/off.txt" >/dev/null || bad "levers baseline generate"
! grep -q 'levers' "$T/lv/off.txt" && ok || bad "levers appears in the manifest without the recipe key"
lgates=0; lvmod=
for lvl in low mid high; do
  { cat "$FX/recipe_kit_corr.txt"; echo "locks $lvl"; echo "levers on"; } > "$FX/recipe_kit_lever_$lvl.txt"
  for seed in 1 2 3 4 5 6 7 8 12345; do
    "$BUILD/nwndgen" --game nwn1 --base "$FX/base.mod" --recipe "$FX/recipe_kit_lever_$lvl.txt" --seed $seed --output "$T/lv/${lvl}_$seed.mod" --manifest "$T/lv/${lvl}_$seed.txt" >"$T/lv/${lvl}_$seed.out" || { bad "levers $lvl generate seed $seed"; continue; }
    grep -q '^levers=on$' "$T/lv/${lvl}_$seed.txt" && grep -q '^levers_algorithm=levers/1$' "$T/lv/${lvl}_$seed.txt" && grep -q '^levers\.[a-z_0-9]*=gates [0-9]*$' "$T/lv/${lvl}_$seed.txt" && grep -q '^script.scel_use.ncs_sha256=' "$T/lv/${lvl}_$seed.txt" && ok || bad "levers $lvl manifest seed $seed"
    grep -q 'levers (levers on)' "$T/lv/${lvl}_$seed.out" && ok || bad "levers $lvl summary seed $seed"
    dgk --roster "$FX/roster_corr_test.txt" "$T/lv/${lvl}_$seed.mod" >"$T/dg.txt" 2>&1 && ok || bad "dgcheck levers $lvl seed $seed: $(grep BAD "$T/dg.txt" | head -3)"
    eval "$(sed -n 's/^levers\.[a-z_0-9]*=gates \([0-9]*\)$/lgates=$((lgates + \1));/p' "$T/lv/${lvl}_$seed.txt" | paste -sd' ')"
    grep -q 'gates [1-9]' "$T/lv/${lvl}_$seed.txt" && lvmod="$T/lv/${lvl}_$seed.mod"
    # the lever only changes a door and adds one marker: the creature totals equal the same seed without the key
    "$BUILD/nwndgen" --game nwn1 --base "$FX/base.mod" --recipe "$FX/recipe_kit_lock_$lvl.txt" --seed $seed --output "$T/lv/plain_$seed.mod" --force >"$T/lv/plain_$seed.out" 2>&1
    [ "$(grep -E 'creatures [0-9]+|total CR' "$T/lv/${lvl}_$seed.out")" = "$(grep -E 'creatures [0-9]+|total CR' "$T/lv/plain_$seed.out")" ] && ok || bad "levers $lvl changed the creature totals, seed $seed"
  done
done
[ "$lgates" -gt 0 ] && ok || bad "levers made no gate over 27 modules"
"$BUILD/nwndgen" --game nwn1 --base "$FX/base.mod" --recipe "$FX/recipe_kit_lever_high.txt" --seed 12345 --output "$T/lv/again.mod" >/dev/null && cmp -s "$T/lv/again.mod" "$T/lv/high_12345.mod" && ok || bad "levers build is not reproducible"
{ cat "$FX/recipe_kit_corr.txt"; echo "locks high"; echo "levers off"; } > "$FX/recipe_kit_lever_off.txt"
"$BUILD/nwndgen" --game nwn1 --base "$FX/base.mod" --recipe "$FX/recipe_kit_lever_off.txt" --seed 12345 --output "$T/lv/offkey.mod" >/dev/null && cmp -s "$T/lv/offkey.mod" "$T/lv/off.mod" && ok || bad "levers off differs from leaving the key out"
# negative proof: a lever that loses the name of its gate, and so a gate without a lever
if [ -n "$lvmod" ]; then
  LC_ALL=C sed 's/scel_gate/scel_gatx/g' "$lvmod" > "$T/lv/nogate.mod"
  dgk --roster "$FX/roster_corr_test.txt" "$T/lv/nogate.mod" >"$T/dg.txt" 2>&1 && bad "dgcheck accepted a lever without a gate" || { grep -q "names no gate door" "$T/dg.txt" && grep -q "opened by 0 levers" "$T/dg.txt" && ok || bad "dgcheck lever message: $(head -3 "$T/dg.txt")"; }
else bad "levers: no module with a gate to run the negative proof on"; fi
{ cat "$FX/recipe_kit_corr.txt"; echo "levers on"; } > "$FX/r_lvnolocks.txt"
badruns "levers without locks" --recipe "$FX/r_lvnolocks.txt"
{ cat "$FX/recipe_kit_lever_high.txt"; echo "levers off"; } > "$FX/r_lvdup.txt"
badruns "levers given twice" --recipe "$FX/r_lvdup.txt"
{ cat "$FX/recipe_kit_lock_high.txt"; echo "levers high"; } > "$FX/r_lvbad.txt"
badruns "levers high" --recipe "$FX/r_lvbad.txt"
# patrols off|low|mid|high: a few room creatures walk a route inside their room (scel_beat nudges them); nothing is added, so the totals do not change
mkdir -p "$T/pt"
"$BUILD/nwndgen" --game nwn1 --base "$FX/base.mod" --recipe "$FX/recipe_kit_corr.txt" --seed 12345 --output "$T/pt/off.mod" --manifest "$T/pt/off.txt" >/dev/null || bad "patrols baseline generate"
! grep -q 'patrols' "$T/pt/off.txt" && ok || bad "patrols appears in the manifest without the recipe key"
npat=0; ptmod=
for lvl in low mid high; do
  { cat "$FX/recipe_kit_corr.txt"; echo "patrols $lvl"; } > "$FX/recipe_kit_patrol_$lvl.txt"
  for seed in 1 2 3 4 5 6 7 8 12345; do
    "$BUILD/nwndgen" --game nwn1 --base "$FX/base.mod" --recipe "$FX/recipe_kit_patrol_$lvl.txt" --seed $seed --output "$T/pt/${lvl}_$seed.mod" --manifest "$T/pt/${lvl}_$seed.txt" >"$T/pt/${lvl}_$seed.out" || { bad "patrols $lvl generate seed $seed"; continue; }
    grep -q "^patrols=$lvl\$" "$T/pt/${lvl}_$seed.txt" && grep -q '^patrols_algorithm=patrols/1$' "$T/pt/${lvl}_$seed.txt" && grep -q '^patrols\.[a-z_0-9]*=patrols [0-9]*$' "$T/pt/${lvl}_$seed.txt" && grep -q '^script.scel_beat.ncs_sha256=' "$T/pt/${lvl}_$seed.txt" && ok || bad "patrols $lvl manifest seed $seed"
    grep -q 'patrols (patrols '"$lvl"')' "$T/pt/${lvl}_$seed.out" && ok || bad "patrols $lvl summary seed $seed"
    dgk --roster "$FX/roster_corr_test.txt" "$T/pt/${lvl}_$seed.mod" >"$T/dg.txt" 2>&1 && ok || bad "dgcheck patrols $lvl seed $seed: $(grep BAD "$T/dg.txt" | head -3)"
    eval "$(sed -n 's/^patrols\.[a-z_0-9]*=patrols \([0-9]*\)$/npat=$((npat + \1));/p' "$T/pt/${lvl}_$seed.txt" | paste -sd' ')"
    grep -q 'patrols [1-9]' "$T/pt/${lvl}_$seed.txt" && ptmod="$T/pt/${lvl}_$seed.mod"
    "$BUILD/nwndgen" --game nwn1 --base "$FX/base.mod" --recipe "$FX/recipe_kit_corr.txt" --seed $seed --output "$T/pt/plain_$seed.mod" --force >"$T/pt/plain_$seed.out" 2>&1
    [ "$(grep -E 'creatures [0-9]+|total CR' "$T/pt/${lvl}_$seed.out")" = "$(grep -E 'creatures [0-9]+|total CR' "$T/pt/plain_$seed.out")" ] && ok || bad "patrols $lvl changed the creature totals, seed $seed"
  done
done
[ "$npat" -gt 0 ] && ok || bad "patrols made no patrol over 27 modules"
"$BUILD/nwndgen" --game nwn1 --base "$FX/base.mod" --recipe "$FX/recipe_kit_patrol_high.txt" --seed 12345 --output "$T/pt/again.mod" >/dev/null && cmp -s "$T/pt/again.mod" "$T/pt/high_12345.mod" && ok || bad "patrols build is not reproducible"
{ cat "$FX/recipe_kit_corr.txt"; echo "patrols off"; } > "$FX/recipe_kit_patrol_off.txt"
"$BUILD/nwndgen" --game nwn1 --base "$FX/base.mod" --recipe "$FX/recipe_kit_patrol_off.txt" --seed 12345 --output "$T/pt/offkey.mod" >/dev/null && cmp -s "$T/pt/offkey.mod" "$T/pt/off.mod" && ok || bad "patrols off differs from leaving the key out"
# negative proof: markers that lose their route while the area still runs the heartbeat script
if [ -n "$ptmod" ]; then
  LC_ALL=C sed 's/scel_rt/scel_rx/g' "$ptmod" > "$T/pt/noroute.mod"
  dgk --roster "$FX/roster_corr_test.txt" "$T/pt/noroute.mod" >"$T/dg.txt" 2>&1 && bad "dgcheck accepted a heartbeat script without patrols" || { grep -q "OnHeartbeat is" "$T/dg.txt" && ok || bad "dgcheck patrol message: $(head -3 "$T/dg.txt")"; }
else bad "patrols: no module with a patrol to run the negative proof on"; fi
{ cat "$FX/recipe_kit.txt"; echo "patrols high"; } > "$FX/r_pnoroster.txt"
badruns "patrols without roster" --recipe "$FX/r_pnoroster.txt"
{ cat "$FX/recipe_kit_patrol_high.txt"; echo "patrols low"; } > "$FX/r_pdup.txt"
badruns "patrols given twice" --recipe "$FX/r_pdup.txt"
{ cat "$FX/recipe_kit_corr.txt"; echo "patrols lots"; } > "$FX/r_pbad.txt"
badruns "patrols lots" --recipe "$FX/r_pbad.txt"
# encounters off|low|mid|high: random encounters while playing (rest hook and quiet-spell timer); the generator only writes the pool and the numbers
mkdir -p "$T/en"
"$BUILD/nwndgen" --game nwn1 --base "$FX/base.mod" --recipe "$FX/recipe_kit_corr.txt" --seed 12345 --output "$T/en/off.mod" --manifest "$T/en/off.txt" >/dev/null || bad "encounters baseline generate"
! grep -q 'encounters' "$T/en/off.txt" && ok || bad "encounters appears in the manifest without the recipe key"
enmod=
for lvl in low mid high; do
  { cat "$FX/recipe_kit_corr.txt"; echo "encounters $lvl"; } > "$FX/recipe_kit_enc_$lvl.txt"
  for seed in 1 2 3 4 5 6 7 8 12345; do
    "$BUILD/nwndgen" --game nwn1 --base "$FX/base.mod" --recipe "$FX/recipe_kit_enc_$lvl.txt" --seed $seed --output "$T/en/${lvl}_$seed.mod" --manifest "$T/en/${lvl}_$seed.txt" >"$T/en/${lvl}_$seed.out" || { bad "encounters $lvl generate seed $seed"; continue; }
    grep -q "^encounters=$lvl\$" "$T/en/${lvl}_$seed.txt" && grep -q '^encounters_algorithm=encounters/1$' "$T/en/${lvl}_$seed.txt" && grep -q '^encounters_config=pool [1-9][0-9]*, group [234], cap [1-9], rest [1-9][0-9]*, quiet [1-9][0-9]*$' "$T/en/${lvl}_$seed.txt" && grep -q '^script.scel_rest.ncs_sha256=' "$T/en/${lvl}_$seed.txt" && ok || bad "encounters $lvl manifest seed $seed"
    grep -q 'encounters (encounters '"$lvl"')' "$T/en/${lvl}_$seed.out" && ok || bad "encounters $lvl summary seed $seed"
    dgk --roster "$FX/roster_corr_test.txt" "$T/en/${lvl}_$seed.mod" >"$T/dg.txt" 2>&1 && ok || bad "dgcheck encounters $lvl seed $seed: $(grep BAD "$T/dg.txt" | head -3)"
    enmod="$T/en/${lvl}_$seed.mod"
    "$BUILD/nwndgen" --game nwn1 --base "$FX/base.mod" --recipe "$FX/recipe_kit_corr.txt" --seed $seed --output "$T/en/plain_$seed.mod" --force >"$T/en/plain_$seed.out" 2>&1
    [ "$(grep -E 'creatures [0-9]+|total CR' "$T/en/${lvl}_$seed.out")" = "$(grep -E 'creatures [0-9]+|total CR' "$T/en/plain_$seed.out")" ] && ok || bad "encounters $lvl changed the creature totals, seed $seed"
  done
done
grep -q 'cap 2, rest 15, quiet 200' "$T/en/low_12345.txt" && grep -q 'cap 4, rest 30, quiet 120' "$T/en/mid_12345.txt" && grep -q 'cap 6, rest 50, quiet 70' "$T/en/high_12345.txt" && ok || bad "encounters numbers per level"
for ps in 2 3; do
  "$BUILD/nwndgen" --game nwn1 --base "$FX/base.mod" --recipe "$FX/recipe_kit_enc_high.txt" --seed 12345 --party-size $ps --output "$T/en/p$ps.mod" --manifest "$T/en/p$ps.txt" >/dev/null && grep -q "group $ps," "$T/en/p$ps.txt" && ok || bad "encounters group size for a party of $ps"
done
"$BUILD/nwndgen" --game nwn1 --base "$FX/base.mod" --recipe "$FX/recipe_kit_enc_high.txt" --seed 12345 --output "$T/en/again.mod" >/dev/null && cmp -s "$T/en/again.mod" "$T/en/high_12345.mod" && ok || bad "encounters build is not reproducible"
{ cat "$FX/recipe_kit_corr.txt"; echo "encounters off"; } > "$FX/recipe_kit_enc_off.txt"
"$BUILD/nwndgen" --game nwn1 --base "$FX/base.mod" --recipe "$FX/recipe_kit_enc_off.txt" --seed 12345 --output "$T/en/offkey.mod" >/dev/null && cmp -s "$T/en/offkey.mod" "$T/en/off.mod" && ok || bad "encounters off differs from leaving the key out"
# negative proofs: a missing number, and a rest hook without the pool
LC_ALL=C sed 's/scel_etick/scel_etic9/g' "$enmod" > "$T/en/notick.mod"
dgk --roster "$FX/roster_corr_test.txt" "$T/en/notick.mod" >"$T/dg.txt" 2>&1 && bad "dgcheck accepted encounters without scel_etick" || { grep -q "scel_etick must be an int" "$T/dg.txt" && ok || bad "dgcheck encounter message: $(head -3 "$T/dg.txt")"; }
LC_ALL=C sed 's/scel_ec\([^a-z]\)/scel_ed\1/' "$enmod" > "$T/en/nopool.mod"
dgk --roster "$FX/roster_corr_test.txt" "$T/en/nopool.mod" >"$T/dg.txt" 2>&1 && bad "dgcheck accepted a rest hook without the pool" || { grep -q "Mod_OnPlayerRest is" "$T/dg.txt" && ok || bad "dgcheck rest hook message: $(head -3 "$T/dg.txt")"; }
{ cat "$FX/recipe_kit.txt"; echo "encounters high"; } > "$FX/r_enoroster.txt"
badruns "encounters without roster" --recipe "$FX/r_enoroster.txt"
{ cat "$FX/recipe_kit_enc_high.txt"; echo "encounters low"; } > "$FX/r_edup.txt"
badruns "encounters given twice" --recipe "$FX/r_edup.txt"
{ cat "$FX/recipe_kit_corr.txt"; echo "encounters lots"; } > "$FX/r_ebad.txt"
badruns "encounters lots" --recipe "$FX/r_ebad.txt"
# negative proofs: the validator re-reads the tileset, so a lie about edges, door slots or doors must show
KSET=tileset_kit_edge.set; sed 's/^Top=Corridor/Top=Bridge/' "$FX/tileset_kit.set" > "$FX/$KSET"
checkfail "dgcheck rejects wrong corridor edges" dgk "$T/k/k1.mod"
dgk "$T/k/k1.mod" 2>&1 | grep -q "adjacency violations" && ok || bad "dgcheck names the edge violations"
KSET=tileset_kit_nodoor.set; sed 's/^Doors=1/Doors=0/' "$FX/tileset_kit.set" > "$FX/$KSET"
checkfail "dgcheck rejects doors that are not on a door tile" dgk "$T/k/k12345.mod"
KSET=tileset_kit_type.set; sed 's/^Type=22/Type=23/' "$FX/tileset_kit.set" > "$FX/$KSET"
checkfail "dgcheck rejects a door of the wrong type" dgk "$T/k/k12345.mod"
KSET=tileset_kit_facing.set; sed 's/^Orientation=0.0/Orientation=90.0/' "$FX/tileset_kit.set" > "$FX/$KSET"
checkfail "dgcheck rejects a door facing the wrong way" dgk "$T/k/k12345.mod"
KSET=tileset_kit_extra.set
awk '/^\[TILE200\]/{f=1} f&&/^Doors=0/{print "Doors=1"; print "[TILE200DOOR0]\nType=22\nX=0\nY=0\nZ=0\nOrientation=0"; f=0; next} {print}' "$FX/tileset_kit.set" > "$FX/$KSET"
miss=0
for seed in 1 2 3 4 5; do dgk "$T/k/k$seed.mod" 2>&1 | grep -q "has no door at its slot" && miss=$((miss+1)); done
[ "$miss" -gt 0 ] && ok || bad "dgcheck must notice a door tile without a door"
unset KSET
# kit tiles never leak into a plain pack: the plain pack cannot satisfy a kit recipe
sed 's/^tilepack .*/tilepack tilepack_test.txt/' "$FX/recipe_kit.txt" > "$FX/r_kit_nokit.txt"
badrun "kit corridors with a pack that has no kit records" --recipe "$FX/r_kit_nokit.txt"
grep -q "kit records" "$T/err.txt" && ok || bad "no-kit-records error message"
sed 's/^corridors kit/corridors round/' "$FX/recipe_kit.txt" > "$FX/r_badcor.txt"
badrun "unknown corridors value" --recipe "$FX/r_badcor.txt"
sed 's/^doors plain/doors locked/' "$FX/recipe_kit.txt" > "$FX/r_baddoor.txt"
badrun "unknown doors value" --recipe "$FX/r_baddoor.txt"
sed '/^corridors kit/d' "$FX/recipe_kit.txt" > "$FX/r_doorwide.txt"
badrun "doors plain without kit corridors" --recipe "$FX/r_doorwide.txt"
grep -q "corridors kit" "$T/err.txt" && ok || bad "doors-without-kit error message"
sed '/^template_door/d' "$FX/recipe_kit.txt" > "$FX/r_nodonor.txt"
badrun "doors plain without template_door" --recipe "$FX/r_nodonor.txt"
grep -q "template_door" "$T/err.txt" && ok || bad "missing template_door error message"
sed 's/^template_door .*/template_door tpl_are.are/' "$FX/recipe_kit.txt" > "$FX/r_wrongdonor.txt"
badrun "template_door of the wrong type" --recipe "$FX/r_wrongdonor.txt"
sed 's/^area ka 14 14 3 Kit A/area ka 7 7 5 Kit A/' "$FX/recipe_kit.txt" > "$FX/r_kitcramped.txt"
badrun "kit area too small for its rooms" --recipe "$FX/r_kitcramped.txt"
grep -q "larger area" "$T/err.txt" && ok || bad "kit cramped error message"


# --- end-of-run summary ----------------------------------------------------------------------------
"$BUILD/nwndgen" --game nwn1 --base "$FX/base.mod" --recipe "$FX/recipe_kit_deco.txt" --seed 2 --party-size 3 --min-level 2 --max-level 3 --output "$T/k/sum.mod" --force > "$T/sum.txt" 2>&1 || bad "summary run"
grep -q "Built for:  character levels 2-3, party of 3" "$T/sum.txt" && ok || bad "summary: level/party line"
grep -q "Levels:     3 areas" "$T/sum.txt" && grep -q "Level 3: " "$T/sum.txt" && ok || bad "summary: levels"
grep -q "per room (1..n):" "$T/sum.txt" && grep -q "^  Creatures (" "$T/sum.txt" && grep -q "^  Objective:" "$T/sum.txt" && ok || bad "summary: creatures/objective"
grep -q "^  Reproduce:  nwndgen --game nwn1 --base .* --seed 2 --party-size 3 --min-level 2 --max-level 3 --output " "$T/sum.txt" && ok || bad "summary: reproduce command"
r=$(sed -n 's/^  Output SHA-256: //p' "$T/sum.txt"); [ "${#r}" -eq 64 ] && ok || bad "summary: output hash"
"$BUILD/nwndgen" --game nwn1 --base "$FX/base.mod" --recipe "$FX/recipe_kit_deco.txt" --seed 2 --party-size 3 --min-level 2 --max-level 3 --output "$T/k/sum_again.mod" --manifest "$T/k/sum_again.txt" >/dev/null && grep -q "^output_sha256=$r" "$T/k/sum_again.txt" && ok || bad "reproduce command does not reproduce the printed hash"
"$BUILD/nwndgen" --game nwn1 --base "$FX/base.mod" --recipe "$FX/recipe_test.txt" --seed 2 --output "$T/k/sum2.mod" --force > "$T/sum2.txt" 2>&1 || bad "summary run (no roster)"
grep -q "Theme:      none" "$T/sum2.txt" && ! grep -q "Creatures (" "$T/sum2.txt" && ok || bad "summary without a roster"


# --- level-scaled loot (roster loot / gold lines) --------------------------------------------------
cp "$HERE/data/roster_loot_test.txt" "$FX/"
{ cat "$FX/recipe_test.txt"; echo "roster roster_loot_test.txt"; echo "scripts scripts"; } > "$FX/recipe_loot.txt"
mkdir "$T/l"
alpha=0
for seed in 1 2 3 4 5 6 7 8 12345; do
  "$BUILD/nwndgen" --game nwn1 --base "$FX/base.mod" --recipe "$FX/recipe_loot.txt" --seed $seed --output "$T/l/l$seed.mod" --manifest "$T/l/l$seed.txt" >/dev/null || { bad "loot generate seed $seed"; continue; }
  "$BUILD/dgcheck" --set "$FX/tileset.set" --tileset tsttest --roster "$FX/roster_loot_test.txt" "$T/l/l$seed.mod" >"$T/dg.txt" 2>&1 && ok || bad "dgcheck loot seed $seed: $(grep BAD "$T/dg.txt" | head -3)"
  if grep -q '^theme=alpha' "$T/l/l$seed.txt"; then
    alpha=$((alpha+1))
    grep -q '^loot_algorithm=loot/1' "$T/l/l$seed.txt" && grep -q '^loot\.ca=items [0-9]* gold [1-9]' "$T/l/l$seed.txt" && ok || bad "loot manifest seed $seed"
    grep -Eq '^loot: [1-9][0-9]* item' "$T/dg.txt" && ok || bad "dgcheck sees no carried loot (seed $seed)"
  else
    grep -q '^loot_algorithm=' "$T/l/l$seed.txt" && bad "theme without loot lines wrote loot keys (seed $seed)" || ok
  fi
done
[ "$alpha" -gt 0 ] && ok || bad "no loot test seed used the loot theme"
"$BUILD/nwndgen" --game nwn1 --base "$FX/base.mod" --recipe "$FX/recipe_loot.txt" --seed 12345 --output "$T/l/again.mod" --force >/dev/null && cmp -s "$T/l/again.mod" "$T/l/l12345.mod" && ok || bad "loot build is not reproducible"
# same layout and creatures with and without loot lines
"$BUILD/dgdump" "$FX/recipe_deco.txt" 12345 "$FX/base.ifo" > "$T/l/d1.txt" 2>/dev/null; "$BUILD/dgdump" "$FX/recipe_loot.txt" 12345 "$FX/base.ifo" > "$T/l/d2.txt" 2>/dev/null
cmp -s "$T/l/d1.txt" "$T/l/d2.txt" && ok || bad "loot lines moved the layout"
# a roster without loot lines writes no loot keys (the compiled script mentions the variables, so the manifest is what we can grep)
grep -q '^loot' "$T/s1/d12345.txt" && bad "a roster without loot lines wrote loot manifest keys" || ok
# negative: the validator re-reads the roster, so loot outside its list/CR range, or gold the theme does not give, must show
for seed in 1 2 3 4 5 6 7 8 12345; do grep -q '^theme=alpha' "$T/l/l$seed.txt" && { aseed=$seed; break; }; done
sed 's/^loot t_l_low .*/loot t_l_low 100 2.00 2.00/' "$FX/roster_loot_test.txt" > "$FX/roster_loot_band.txt"
checkfail "dgcheck rejects loot outside its CR range" "$BUILD/dgcheck" --set "$FX/tileset.set" --tileset tsttest --roster "$FX/roster_loot_band.txt" "$T/l/l$aseed.mod"
sed '/^gold /d' "$FX/roster_loot_test.txt" > "$FX/roster_loot_nogold.txt"
checkfail "dgcheck rejects gold the theme does not give" "$BUILD/dgcheck" --set "$FX/tileset.set" --tileset tsttest --roster "$FX/roster_loot_nogold.txt" "$T/l/l$aseed.mod"


# --- spawn_mode loot_test: five weak creatures and a weak boss, all in the first area -------------
{ cat "$FX/recipe_loot.txt"; echo "spawn_mode loot_test"; } > "$FX/recipe_loottest.txt"
for seed in 1 2 3 12345; do
  "$BUILD/nwndgen" --game nwn1 --base "$FX/base.mod" --recipe "$FX/recipe_loottest.txt" --seed $seed --output "$T/l/t$seed.mod" --manifest "$T/l/t$seed.txt" >/dev/null || { bad "loot_test generate seed $seed"; continue; }
  grep -q '^spawn_mode=loot_test' "$T/l/t$seed.txt" && ok || bad "loot_test manifest seed $seed"
  "$BUILD/dgcheck" --set "$FX/tileset.set" --tileset tsttest --roster "$FX/roster_loot_test.txt" "$T/l/t$seed.mod" >"$T/dg.txt" 2>&1 && ok || bad "dgcheck loot_test seed $seed: $(grep BAD "$T/dg.txt" | head -3)"
  grep -q '^spawn: 5 creature marker' "$T/dg.txt" && grep -q '^spawn: test mode' "$T/dg.txt" && ok || bad "loot_test seed $seed: expected five creatures and a test-mode boss: $(grep '^spawn:' "$T/dg.txt")"
done
{ cat "$FX/recipe_test.txt"; echo "spawn_mode loot_test"; } > "$FX/r_ltnoroster.txt"
badruns "loot_test without roster" --recipe "$FX/r_ltnoroster.txt"

# --- loot_amount low|mid|high ------------------------------------------------------------------------
for amt in low mid high; do
  { cat "$FX/recipe_loot.txt"; echo "loot_amount $amt"; } > "$FX/recipe_la_$amt.txt"
  "$BUILD/nwndgen" --game nwn1 --base "$FX/base.mod" --recipe "$FX/recipe_la_$amt.txt" --seed 12345 --output "$T/l/la_$amt.mod" --manifest "$T/l/la_$amt.txt" >"$T/l/la_$amt.out" || { bad "loot_amount $amt generate"; continue; }
  grep -q "^loot_amount=$amt\$" "$T/l/la_$amt.txt" && ok || bad "loot_amount $amt manifest"
  "$BUILD/dgcheck" --set "$FX/tileset.set" --tileset tsttest --roster "$FX/roster_loot_test.txt" "$T/l/la_$amt.mod" >"$T/dg.txt" 2>&1 && ok || bad "dgcheck loot_amount $amt: $(grep BAD "$T/dg.txt" | head -3)"
done
"$BUILD/nwndgen" --game nwn1 --base "$FX/base.mod" --recipe "$FX/recipe_loot.txt" --seed 12345 --output "$T/l/la_default.mod" --force >/dev/null && cmp -s "$T/l/la_default.mod" "$T/l/la_mid.mod" && ok || bad "loot_amount mid differs from leaving the key out"
checkfail "loot_amount high differs from low" cmp -s "$T/l/la_high.mod" "$T/l/la_low.mod"
grep -q 'loot amount high' "$T/l/la_high.out" && ok || bad "summary does not mention loot amount high"
{ cat "$FX/recipe_test.txt"; echo "loot_amount high"; } > "$FX/r_laoroster.txt"
badruns "loot_amount without roster" --recipe "$FX/r_laoroster.txt"
{ cat "$FX/recipe_loot.txt"; echo "loot_amount lots"; } > "$FX/r_labad.txt"
badruns "loot_amount lots" --recipe "$FX/r_labad.txt"

# --- dressing off|low|mid|high: rooms get a type and furniture from the roster's roomprop lines -------
cp "$HERE/data/roster_dress_test.txt" "$FX/"
{ cat "$FX/recipe_test.txt"; echo "roster roster_dress_test.txt"; echo "scripts scripts"; } > "$FX/recipe_dress_base.txt"
mkdir -p "$T/dr"
"$BUILD/nwndgen" --game nwn1 --base "$FX/base.mod" --recipe "$FX/recipe_dress_base.txt" --seed 12345 --output "$T/dr/off.mod" --manifest "$T/dr/off.txt" >/dev/null || bad "dressing baseline generate"
! grep -q 'dressing' "$T/dr/off.txt" && ok || bad "dressing appears in the manifest without the recipe key"
for lvl in low mid high; do
  { cat "$FX/recipe_dress_base.txt"; echo "dressing $lvl"; } > "$FX/recipe_dress_$lvl.txt"
  for seed in 1 2 12345; do
    "$BUILD/nwndgen" --game nwn1 --base "$FX/base.mod" --recipe "$FX/recipe_dress_$lvl.txt" --seed $seed --output "$T/dr/${lvl}_$seed.mod" --manifest "$T/dr/${lvl}_$seed.txt" >"$T/dr/${lvl}_$seed.out" || { bad "dressing $lvl generate seed $seed"; continue; }
    grep -q "^dressing=$lvl\$" "$T/dr/${lvl}_$seed.txt" && grep -q '^dressing_algorithm=rooms/1$' "$T/dr/${lvl}_$seed.txt" && grep -q '^rooms\.[a-z_0-9]*=[a-z,]*$' "$T/dr/${lvl}_$seed.txt" && ok || bad "dressing $lvl manifest seed $seed"
    grep -q 'room types (dressing '"$lvl"')' "$T/dr/${lvl}_$seed.out" && ok || bad "dressing $lvl summary seed $seed"
    "$BUILD/dgcheck" --set "$FX/tileset.set" --tileset tsttest --roster "$FX/roster_dress_test.txt" "$T/dr/${lvl}_$seed.mod" >"$T/dg.txt" 2>&1 && ok || bad "dgcheck dressing $lvl seed $seed: $(grep BAD "$T/dg.txt" | head -3)"
  done
done
"$BUILD/nwndgen" --game nwn1 --base "$FX/base.mod" --recipe "$FX/recipe_dress_high.txt" --seed 12345 --output "$T/dr/again.mod" >/dev/null && cmp -s "$T/dr/again.mod" "$T/dr/high_12345.mod" && ok || bad "dressing build is not reproducible"
checkfail "dressing high differs from the plain module" cmp -s "$T/dr/high_12345.mod" "$T/dr/off.mod"
{ cat "$FX/recipe_dress_base.txt"; echo "dressing off"; } > "$FX/recipe_dress_off.txt"
"$BUILD/nwndgen" --game nwn1 --base "$FX/base.mod" --recipe "$FX/recipe_dress_off.txt" --seed 12345 --output "$T/dr/offkey.mod" >/dev/null && cmp -s "$T/dr/offkey.mod" "$T/dr/off.mod" && ok || bad "dressing off differs from leaving the key out"
# negative proof: checked against a roster without those furniture lines, dgcheck must object
"$BUILD/dgcheck" --set "$FX/tileset.set" --tileset tsttest --roster "$FX/roster_deco_test.txt" "$T/dr/high_12345.mod" >"$T/dg.txt" 2>&1 && bad "dgcheck accepted furniture outside the roster" || { grep -q 'is not in the prop list' "$T/dg.txt" && ok || bad "dgcheck furniture message"; }
{ cat "$FX/recipe_test.txt"; echo "dressing high"; } > "$FX/r_dnoroster.txt"
badruns "dressing without roster" --recipe "$FX/r_dnoroster.txt"
{ cat "$FX/recipe_dress_high.txt"; echo "dressing low"; } > "$FX/r_ddup.txt"
badruns "dressing given twice" --recipe "$FX/r_ddup.txt"
{ cat "$FX/recipe_dress_base.txt"; echo "dressing lots"; } > "$FX/r_dbad.txt"
badruns "dressing lots" --recipe "$FX/r_dbad.txt"

# --- henchmen: the first player gets stock henchmen from the roster's families -----------------------
cp "$HERE/data/roster_hench_test.txt" "$FX/"
{ cat "$FX/recipe_test.txt"; echo "roster roster_hench_test.txt"; echo "scripts scripts"; echo "henchmen 2"; } > "$FX/recipe_hench.txt"
mkdir -p "$T/h"
for seed in 1 2 3 12345; do
  "$BUILD/nwndgen" --game nwn1 --base "$FX/base.mod" --recipe "$FX/recipe_hench.txt" --seed $seed --min-level 3 --max-level 4 --output "$T/h/h$seed.mod" --manifest "$T/h/h$seed.txt" >"$T/h/out$seed.txt" || { bad "henchmen generate seed $seed"; continue; }
  grep -q '^henchmen=2$' "$T/h/h$seed.txt" && grep -q '^hench_algorithm=hench/1$' "$T/h/h$seed.txt" && grep -q '^henchman.2=t_hen_._01$' "$T/h/h$seed.txt" && ok || bad "henchmen manifest seed $seed: $(grep hench "$T/h/h$seed.txt")"
  grep -q '^  Companions: 2 stock henchmen join the first player: t_hen_._01, t_hen_._01$' "$T/h/out$seed.txt" && ok || bad "henchmen summary seed $seed"
  "$BUILD/dgcheck" --set "$FX/tileset.set" --tileset tsttest --roster "$FX/roster_hench_test.txt" "$T/h/h$seed.mod" >"$T/dg.txt" 2>&1 && ok || bad "dgcheck henchmen seed $seed: $(grep BAD "$T/dg.txt" | head -3)"
  grep -q '^henchmen: 2$' "$T/dg.txt" && ok || bad "dgcheck henchmen line seed $seed"
done
"$BUILD/nwndgen" --game nwn1 --base "$FX/base.mod" --recipe "$FX/recipe_hench.txt" --seed 1 --min-level 3 --max-level 4 --output "$T/h/again.mod" >/dev/null && cmp -s "$T/h/h1.mod" "$T/h/again.mod" && ok || bad "henchmen build is not reproducible"
# negative proof: checked against a roster that lacks those families, dgcheck must object
"$BUILD/dgcheck" --set "$FX/tileset.set" --tileset tsttest --roster "$FX/roster_loot_test.txt" "$T/h/h1.mod" >"$T/dg.txt" 2>&1 && bad "dgcheck accepted henchmen outside the roster" || { grep -q 'not from a family in the roster' "$T/dg.txt" && ok || bad "dgcheck henchman message"; }
# a roster module without the key carries no henchman data
"$BUILD/nwndgen" --game nwn1 --base "$FX/base.mod" --recipe "$FX/recipe_loot.txt" --seed 1 --output "$T/h/none.mod" --manifest "$T/h/none.txt" >/dev/null && ! grep -q hench "$T/h/none.txt" && ok || bad "henchmen appear without the recipe key"
{ cat "$FX/recipe_test.txt"; echo "henchmen 1"; } > "$FX/r_hnoroster.txt"
badruns "henchmen without roster" --recipe "$FX/r_hnoroster.txt"
{ cat "$FX/recipe_hench.txt"; echo "henchmen 1"; } > "$FX/r_hdup.txt"
badruns "henchmen given twice" --recipe "$FX/r_hdup.txt"
sed 's/^henchmen 2/henchmen 6/' "$FX/recipe_hench.txt" > "$FX/r_h6.txt"
badruns "henchmen above 5" --recipe "$FX/r_h6.txt"
sed 's/^henchmen 2/henchmen two/' "$FX/recipe_hench.txt" > "$FX/r_hx.txt"
badruns "henchmen not a number" --recipe "$FX/r_hx.txt"
sed 's/^henchmen 2/henchmen 4/' "$FX/recipe_hench.txt" > "$FX/r_h4.txt"
badruns "more henchmen than roster families" --recipe "$FX/r_h4.txt"

# --- run defaults in the recipe: seed, party_size, min_level, max_level -------------------------------
{ cat "$FX/recipe_hench.txt"; printf 'seed 2\nparty_size 1\nmin_level 3\nmax_level 4\n'; } > "$FX/recipe_rundef.txt"
"$BUILD/nwndgen" --game nwn1 --base "$FX/base.mod" --recipe "$FX/recipe_rundef.txt" --output "$T/h/rd.mod" --manifest "$T/h/rd.txt" >/dev/null && ok || bad "recipe-only run (no --seed)"
"$BUILD/nwndgen" --game nwn1 --base "$FX/base.mod" --recipe "$FX/recipe_hench.txt" --seed 2 --party-size 1 --min-level 3 --max-level 4 --output "$T/h/cl.mod" >/dev/null && cmp -s "$T/h/rd.mod" "$T/h/cl.mod" && ok || bad "recipe defaults differ from the same values on the command line"
grep -q '^seed=2$' "$T/h/rd.txt" && grep -q '^min_level=3$' "$T/h/rd.txt" && grep -q '^max_level=4$' "$T/h/rd.txt" && ok || bad "recipe defaults not in the manifest"
"$BUILD/nwndgen" --game nwn1 --base "$FX/base.mod" --recipe "$FX/recipe_rundef.txt" --seed 3 --output "$T/h/ov.mod" --manifest "$T/h/ov.txt" >/dev/null && grep -q '^seed=3$' "$T/h/ov.txt" && ! cmp -s "$T/h/rd.mod" "$T/h/ov.mod" && ok || bad "command line --seed does not override the recipe"
"$BUILD/nwndgen" --game nwn1 --base "$FX/base.mod" --recipe "$FX/recipe_rundef.txt" --max-level 3 --output "$T/h/ov2.mod" --manifest "$T/h/ov2.txt" >/dev/null && grep -q '^max_level=3$' "$T/h/ov2.txt" && ok || bad "command line --max-level does not override the recipe"
badruns "recipe run default outside support" --recipe "$FX/recipe_rundef.txt" --max-level 9
{ cat "$FX/recipe_hench.txt"; echo "seed nope"; } > "$FX/r_badseed.txt"
badruns "recipe seed not a number" --recipe "$FX/r_badseed.txt"
"$BUILD/nwndgen" --game nwn1 --base "$FX/base.mod" --recipe "$FX/recipe_hench.txt" --output "$T/bad.mod" >/dev/null 2>"$T/err.txt"; [ $? -eq 2 ] && grep -q "seed line in the recipe" "$T/err.txt" && ok || bad "missing seed message"

echo "dungeon e2e: $pass passed, $fail failed"
[ $fail -eq 0 ]
