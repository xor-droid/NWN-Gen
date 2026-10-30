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
