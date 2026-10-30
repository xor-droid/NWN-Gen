#!/bin/sh
# Generate and validate many seeds against a real install.
# usage: sweep_seeds.sh BUILD RECIPE BASE.mod INSTALL FIRST LAST [ROSTER]
# Extra nwndgen options (e.g. --party-size 3 --max-level 4) can be passed in $NWNDGEN_ARGS.
# With ROSTER, dgcheck also checks the creature/objective markers against it and the install.
# Prints a summary; exit 1 if any seed fails to generate or to validate.
set -u
BUILD=$1; RECIPE=$2; BASE=$3; ROOT=$4; FIRST=$5; LAST=$6; ROSTER=${7:-}
T=$(mktemp -d "${TMPDIR:-/tmp}/nwndgen-sweep.XXXXXX"); trap 'rm -rf "$T"' EXIT INT TERM
ok=0; genfail=0; bad=0; s=$FIRST
while [ "$s" -le "$LAST" ]; do
  # shellcheck disable=SC2086
  if "$BUILD/nwndgen" --game nwn1 --base "$BASE" --recipe "$RECIPE" --seed "$s" --output "$T/m.mod" --force ${NWNDGEN_ARGS:-} >/dev/null 2>"$T/err"; then
    if "$BUILD/dgcheck" --root "$ROOT" ${ROSTER:+--roster "$ROSTER"} "$T/m.mod" >"$T/out" 2>&1; then ok=$((ok+1)); else bad=$((bad+1)); echo "seed $s:"; grep BAD "$T/out" | head -3; fi
  else genfail=$((genfail+1)); echo "seed $s: $(cat "$T/err")"; fi
  s=$((s+1))
done
echo "seeds $FIRST..$LAST: $ok valid, $genfail failed to generate, $bad invalid"
[ "$genfail" -eq 0 ] && [ "$bad" -eq 0 ]
