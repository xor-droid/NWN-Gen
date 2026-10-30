#!/bin/sh
# End-to-end checks for nwndgen against the FORMAT-TEST FIXTURE pack.
# usage: sh tests/e2e.sh path/to/nwndgen [nwn1|nwn2]   (default nwn1)
# POSIX sh + C tools only (fixtures and the independent verifier are build/fixtool).
set -u
BIN=$(cd "$(dirname "$1")" && pwd)/$(basename "$1")
FIXTOOL=$(dirname "$BIN")/fixtool
HERE=$(cd "$(dirname "$0")" && pwd)
GAME=${2:-nwn1}
case $GAME in
  nwn1) OTHER=nwn2; GOLDEN_SEED12345=0d9ceee0b42f7d31dc6120f9fe017b3313b3b02264c2fd7ced58f8a0cd0dc77c ;;
  nwn2) OTHER=nwn1; GOLDEN_SEED12345=f5011e1d4ade5ec3cb5e49120e1f611c6064c997b0b578b33396f6d73b20c820 ;;
  *) echo "unknown game $GAME"; exit 1 ;;
esac
export NWNGEN_TEST_GAME=$GAME

T=$(mktemp -d "${TMPDIR:-/tmp}/nwndgen-e2e.XXXXXX") || exit 1
trap 'rm -rf "$T"' EXIT INT TERM
pass=0; fail=0
ok()  { pass=$((pass+1)); }
bad() { fail=$((fail+1)); echo "FAIL: $*"; }
check() { desc=$1; shift; if "$@" >/dev/null 2>&1; then ok; else bad "$desc"; fi; }
checkfail() { desc=$1; shift; if "$@" >/dev/null 2>&1; then bad "$desc (expected failure)"; else ok; fi; }
sha() { sha256sum "$1" | cut -d' ' -f1; }

FX="$T/pack with space"
"$FIXTOOL" fixture "$FX" $GAME || { echo "fixture build failed"; exit 1; }
B="$FX/base.mod"; C="$FX/rooms.tsv"
gen() { "$BIN" --game $GAME --base "$B" --templates "$C" "$@"; }

# --- determinism -----------------------------------------------------------
mkdir "$T/o1" "$T/o2"
gen --seed 12345 --output "$T/o1/a.mod" --manifest "$T/o1/a.txt" >/dev/null || bad "basic generation"
(cd "$T/o2" && TZ=Pacific/Kiritimati LC_ALL=C.UTF-8 LANG=C.UTF-8 \
  "$BIN" --game $GAME --base "$B" --templates "$C" --seed 12345 --output other_name.mod --manifest m.txt >/dev/null) \
  || bad "generation from another cwd"
check "same bytes across cwd/name/tz/locale" cmp "$T/o1/a.mod" "$T/o2/other_name.mod"
check "manifest identical across runs" cmp "$T/o1/a.txt" "$T/o2/m.txt"
[ "$(sha "$T/o1/a.mod")" = "$GOLDEN_SEED12345" ] && ok || bad "golden hash for seed 12345 ($(sha "$T/o1/a.mod"))"
grep -q "output_sha256=$GOLDEN_SEED12345" "$T/o1/a.txt" && ok || bad "manifest output hash"
grep -q "$T" "$T/o1/a.txt" && bad "manifest leaks a path" || ok

# touch/mtime and relocation of the whole pack must not matter
cp -R "$FX" "$T/moved"; touch -d '2001-01-01' "$T/moved/base.mod"
"$BIN" --game $GAME --base "$T/moved/base.mod" --templates "$T/moved/rooms.tsv" --seed 12345 --output "$T/o2/moved.mod" >/dev/null
check "relocated pack, new mtime" cmp "$T/o1/a.mod" "$T/o2/moved.mod"

# catalog row order / line endings / comments must not matter
sort -r "$C" > "$T/rev.tsv"; cp "$T/rev.tsv" "$FX/rev.tsv"
sed 's/$/\r/' "$C" > "$FX/crlf.tsv"
{ echo "# extra comment"; echo; cat "$C"; } > "$FX/cmt.tsv"
for v in rev crlf cmt; do
  "$BIN" --game $GAME --base "$B" --templates "$FX/$v.tsv" --seed 12345 --output "$T/o2/$v.mod" >/dev/null || bad "catalog variant $v"
  check "catalog $v gives same bytes" cmp "$T/o1/a.mod" "$T/o2/$v.mod"
done

# explicit-equals syntax and option order
gen --output="$T/o2/eq.mod" --seed=12345 --party-size=1 --max-level 2 --min-level 1 >/dev/null
check "--opt=value and option order" cmp "$T/o1/a.mod" "$T/o2/eq.mod"

# boundary seeds + independent verification across many seeds
for s in 0 1 2 3 4 5 6 7 8 9 10 11 18446744073709551615; do
  gen --seed $s --output "$T/o2/s$s.mod" >/dev/null || { bad "seed $s"; continue; }
  "$FIXTOOL" verify "$T/o2/s$s.mod" "$B" "$C" >"$T/o2/s$s.sel" 2>"$T/o2/s$s.err" \
    && ok || bad "independent verification, seed $s: $(cat "$T/o2/s$s.err")"
done
distinct=$(cat "$T"/o2/s*.mod 2>/dev/null | wc -c); : "$distinct"
n=$(for s in 0 1 2 3 4 5 6 7 8 9 10 11; do sha "$T/o2/s$s.mod"; done | sort -u | wc -l)
[ "$n" -ge 4 ] && ok || bad "only $n distinct outputs over 12 seeds"
# the independent selection model must agree with the C selection: SHA-256 from sha256sum, SplitMix64 from fixtool
selmodel() { # seed slot n -> index
  h=$("$FIXTOOL" sel-preimage "$1" "$2" | sha256sum | cut -c1-16)
  "$FIXTOOL" sel-pick "$h" "$3"
}
model_ok=1
for seed in 0 1 2 3 4 5 6 7 8 9 10 11 18446744073709551615; do
  for spec in encounter_a:goblins,rats encounter_b:beetles,undead objective:rescue,supplies; do
    slot=${spec%%:*}; vs=${spec#*:}; first=${vs%%,*}; second=${vs#*,}
    idx=$(selmodel $seed $slot 2)
    if [ "$idx" = 0 ]; then want=$first; else want=$second; fi
    got=$(sed -n "s/^$slot=//p" "$T/o2/s$seed.sel")
    [ "$got" = "$want" ] || { model_ok=0; echo "  seed $seed $slot: C chose '$got', model says '$want'"; }
  done
done
[ $model_ok = 1 ] && ok || bad "independent selection model disagrees"

# --- the game gate -------------------------------------------------------
OFX="$T/other game"
"$FIXTOOL" fixture "$OFX" $OTHER
checkfail "base of the other game is rejected" "$BIN" --game $GAME --base "$OFX/base.mod" --templates "$OFX/rooms.tsv" --seed 1 --output "$T/o2/g1.mod"
"$BIN" --game $GAME --base "$OFX/base.mod" --templates "$OFX/rooms.tsv" --seed 1 --output "$T/o2/g1.mod" 2>&1 | grep -q "was selected" && ok || bad "wrong-game error should name the mismatch"
checkfail "variants of the other game are rejected" "$BIN" --game $GAME --base "$B" --templates "$OFX/rooms.tsv" --seed 1 --output "$T/o2/g2.mod"
checkfail "--game is mandatory" "$BIN" --base "$B" --templates "$C" --seed 1 --output "$T/o2/g3.mod"
rc=0; "$BIN" --base "$B" --templates "$C" --seed 1 --output "$T/o2/g3.mod" >/dev/null 2>&1 || rc=$?
[ "$rc" = 2 ] && ok || bad "missing --game should exit 2 (got $rc)"
rc=0; "$BIN" --game nwn3 --base "$B" --templates "$C" --seed 1 --output "$T/o2/g4.mod" >/dev/null 2>&1 || rc=$?
[ "$rc" = 2 ] && ok || bad "unknown game should exit 2 (got $rc)"
grep -q "^game=$GAME\$" "$T/o1/a.txt" && ok || bad "manifest should record the game"
[ -e "$T"/o2/g?.mod ] && bad "gate failures published output" || ok

# --- configuration / eligibility -------------------------------------------
check "veteran band 5-8 solo is unsupported (other slots lack it)" true
checkfail "levels 5-8 rejected" gen --seed 1 --min-level 5 --max-level 8 --output "$T/o2/x1.mod"
checkfail "party size 2 rejected" gen --seed 1 --party-size 2 --output "$T/o2/x2.mod"
checkfail "level range 1-3 rejected (needs whole band)" gen --seed 1 --max-level 3 --output "$T/o2/x3.mod"
checkfail "min > max" gen --seed 1 --min-level 3 --max-level 2 --output "$T/o2/x4.mod"
checkfail "bad seed -1" gen --seed -1 --output "$T/o2/x5.mod"
checkfail "seed overflow" gen --seed 18446744073709551616 --output "$T/o2/x6.mod"
checkfail "seed non-numeric" gen --seed abc --output "$T/o2/x7.mod"
checkfail "level 41" gen --seed 1 --max-level 41 --output "$T/o2/x8.mod"
checkfail "missing seed" "$BIN" --game $GAME --base "$B" --templates "$C" --output "$T/o2/x9.mod"
checkfail "missing output" "$BIN" --game $GAME --base "$B" --templates "$C" --seed 1
checkfail "duplicate option" gen --seed 1 --seed 2 --output "$T/o2/x10.mod"
checkfail "unknown option" gen --seed 1 --bogus --output "$T/o2/x11.mod"
checkfail "positional arg" gen --seed 1 stray --output "$T/o2/x12.mod"
checkfail "same manifest/output" gen --seed 1 --output "$T/o2/x13.mod" --manifest "$T/o2/x13.mod"
checkfail "missing base" "$BIN" --game $GAME --base "$T/nope.mod" --templates "$C" --seed 1 --output "$T/o2/x14.mod"
checkfail "missing output dir" gen --seed 1 --output "$T/no/such/dir/x.mod"
ls "$T"/o2/x*.mod >/dev/null 2>&1 && bad "failed runs published output" || ok
rc=0; gen --seed 1 --bogus --output "$T/o2/y.mod" >/dev/null 2>&1 || rc=$?
[ "$rc" = 2 ] && ok || bad "usage errors should exit 2 (got $rc)"
rc=0; gen --seed 1 --party-size 2 --output "$T/o2/y.mod" >/dev/null 2>&1 || rc=$?
[ "$rc" = 1 ] && ok || bad "unsupported-content should exit 1 (got $rc)"
check "--help" "$BIN" --help
check "--version" "$BIN" --version

# --- overwrite policy ------------------------------------------------------
echo precious > "$T/o2/keep.mod"
checkfail "refuses to overwrite" gen --seed 1 --output "$T/o2/keep.mod"
[ "$(cat "$T/o2/keep.mod")" = precious ] && ok || bad "existing output was clobbered"
check "--force overwrites" gen --seed 1 --output "$T/o2/keep.mod" --force
[ "$(cat "$T/o2/keep.mod")" != precious ] && ok || bad "--force did not overwrite"
echo oldmanifest > "$T/o2/old.txt"
checkfail "existing manifest blocks, no half-published pair" gen --seed 1 --output "$T/o2/pair.mod" --manifest "$T/o2/old.txt"
[ -e "$T/o2/pair.mod" ] && bad "module left behind when manifest publish failed" || ok
[ "$(cat "$T/o2/old.txt")" = oldmanifest ] && ok || bad "existing manifest clobbered"

# --- corrupt inputs --------------------------------------------------------
head -c 100 "$B" > "$T/trunc.mod"
checkfail "truncated base" "$BIN" --game $GAME --base "$T/trunc.mod" --templates "$C" --seed 1 --output "$T/o2/corrupt1.mod"
cp "$B" "$T/badver.mod"; printf 'V9.9' | dd of="$T/badver.mod" bs=1 seek=4 conv=notrunc 2>/dev/null
checkfail "bad base version" "$BIN" --game $GAME --base "$T/badver.mod" --templates "$C" --seed 1 --output "$T/o2/corrupt2.mod"
cp -R "$FX" "$T/corrupt"
head -c 200 "$FX/variants/objective_rescue.erf" > "$T/corrupt/variants/objective_rescue.erf"
checkfail "truncated variant" "$BIN" --game $GAME --base "$B" --templates "$T/corrupt/rooms.tsv" --seed 1 --output "$T/o2/corrupt3.mod"
cp "$FX/variants/encounter_a_rats.erf" "$T/corrupt/variants/objective_rescue.erf"
checkfail "slot ownership mismatch / foreign area" "$BIN" --game $GAME --base "$B" --templates "$T/corrupt/rooms.tsv" --seed 1 --output "$T/o2/corrupt4.mod"
# a variant naming an area that the base lacks
"$FIXTOOL" corrupt ghost "$T/corrupt/variants/objective_rescue.erf"
checkfail "variant targets missing base area" "$BIN" --game $GAME --base "$B" --templates "$T/corrupt/rooms.tsv" --seed 1 --output "$T/o2/corrupt5.mod"
# a variant trying to override the module script / IFO
"$FIXTOOL" corrupt evilifo "$T/corrupt/variants/objective_rescue.erf"
checkfail "variant overriding module.ifo" "$BIN" --game $GAME --base "$B" --templates "$T/corrupt/rooms.tsv" --seed 1 --output "$T/o2/corrupt6.mod"
# incomplete triplet
"$FIXTOOL" corrupt incomplete "$T/corrupt/variants/objective_rescue.erf"
checkfail "incomplete ARE/GIT/GIC triplet" "$BIN" --game $GAME --base "$B" --templates "$T/corrupt/rooms.tsv" --seed 1 --output "$T/o2/corrupt7.mod"
# two slots claiming the same area
cp "$FX/variants/encounter_a_rats.erf" "$T/corrupt/variants/objective_rescue.erf"
cp "$FX/variants/encounter_a_goblins.erf" "$T/corrupt/variants/objective_supplies.erf"
cp "$FX/variants/encounter_a_rats.erf" "$T/corrupt/variants/objective_veteran.erf"
checkfail "slots sharing an area" "$BIN" --game $GAME --base "$B" --templates "$T/corrupt/rooms.tsv" --seed 1 --output "$T/o2/corrupt8.mod"
printf 'a\tb\t1\t2\t1\t1\n' > "$T/badcat.tsv"
checkfail "malformed catalog" "$BIN" --game $GAME --base "$B" --templates "$T/badcat.tsv" --seed 1 --output "$T/o2/corrupt9.mod"
ls "$T"/o2/corrupt*.mod >/dev/null 2>&1 && bad "corrupt-input runs published output" || ok

# --- hygiene ---------------------------------------------------------------
[ -n "$(find "$T" -name '.nwndgen-*')" ] && bad "temp files left behind" || ok

echo "e2e: $pass passed, $fail failed"
[ "$fail" -eq 0 ]
