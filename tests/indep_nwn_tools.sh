#!/bin/sh
# Optional: validate generated modules with neverwinter.nim (nwn_erf, nwn_gff).
# usage: sh tests/indep_nwn_tools.sh BUILD/nwndgen /abs/path/to/neverwinter-tools
# Uses an identity-resource-ID fixture because neverwinter.nim pairs key i with
# resource-list entry i and ignores the key's resource ID field.
set -eu
BIN=$(cd "$(dirname "$1")" && pwd)/$(basename "$1")
NT=$2
HERE=$(cd "$(dirname "$0")" && pwd)
T=$(mktemp -d "${TMPDIR:-/tmp}/nwndgen-indep.XXXXXX"); trap 'rm -rf "$T"' EXIT INT TERM
fails=0; n=0
chk() { d=$1; shift; n=$((n+1)); if ! "$@" >/dev/null 2>&1; then fails=$((fails+1)); echo "FAIL: $d"; fi; }

"$(dirname "$BIN")/fixtool" fixture "$T/fx" nwn1 plain
mkdir "$T/xb" "$T/xv" "$T/xo"
(cd "$T/xb" && "$NT/nwn_erf" -f "$T/fx/base.mod" -x)
"$BIN" --game nwn1 --base "$T/fx/base.mod" --templates "$T/fx/rooms.tsv" --seed 12345 --output "$T/out.mod"
"$NT/nwn_erf" -f "$T/out.mod" -t | sort > "$T/list_out"
"$NT/nwn_erf" -f "$T/fx/base.mod" -t | sort > "$T/list_base"
chk "same resource set as base (per nwn_erf)" cmp "$T/list_out" "$T/list_base"
(cd "$T/xo" && "$NT/nwn_erf" -f "$T/out.mod" -x)
# slots chosen for seed 12345: encounter_a=rats encounter_b=beetles objective=rescue
for spec in encounter_a:rats:enc_a encounter_b:beetles:enc_b objective:rescue:obj_room; do
  slot=${spec%%:*}; rest=${spec#*:}; var=${rest%%:*}; area=${rest#*:}
  rm -rf "$T/xv"; mkdir "$T/xv"
  (cd "$T/xv" && "$NT/nwn_erf" -f "$T/fx/variants/${slot}_${var}.erf" -x)
  for e in are git gic; do chk "$area.$e == $var variant payload" cmp "$T/xo/$area.$e" "$T/xv/$area.$e"; done
done
for f in module.ifo adv_main.ncs cellar_entry.are cellar_entry.git cellar_entry.gic; do
  chk "$f untouched" cmp "$T/xo/$f" "$T/xb/$f"
done
for f in module.ifo enc_a.are enc_a.git enc_a.gic obj_room.are; do
  chk "nwn_gff parses $f" "$NT/nwn_gff" -i "$T/xo/$f" -o - -k json
done
# reverse direction: a base repacked by nwn_erf is accepted and yields identical bytes
mkdir "$T/pk"; cp "$T/xb"/* "$T/pk/"
(cd "$T/pk" && "$NT/nwn_erf" -f "$T/toolbase.mod" -e MOD -c .)
"$BIN" --game nwn1 --base "$T/toolbase.mod" --templates "$T/fx/rooms.tsv" --seed 12345 --output "$T/out2.mod"
# (nwn_erf -c drops the base's localized-string block, so compare payloads, not bytes)
mkdir "$T/xo2"; (cd "$T/xo2" && "$NT/nwn_erf" -f "$T/out2.mod" -x)
for f in $(cd "$T/xo" && ls); do
  chk "nwn_erf-built base: $f payload identical" cmp "$T/xo/$f" "$T/xo2/$f"
done
echo "independent nwn tools: $((n-fails))/$n checks passed"
[ "$fails" -eq 0 ]
