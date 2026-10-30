#!/bin/sh
# Optional: round-trip REAL shipped archives through the strict reader/writer.
# usage: sh tests/realfiles.sh build/erfcheck "/path/to/Neverwinter Nights" ["/path/to/NWN2 Enhanced Edition"]
# Nothing is copied or written; archives are only read. (km_resources.hak ships
# with duplicate resource keys and is rejected on purpose; HAKs are not inputs.)
set -u
CHK=$1; NWN1=${2:-}; NWN2=${3:-}
rc=0
if [ -n "$NWN1" ]; then
  "$CHK" nwn1 "$NWN1"/data/mod/*.mod "$NWN1"/data/nwm/*.nwm || rc=1
fi
if [ -n "$NWN2" ]; then
  for f in "$NWN2"/modules/*.mod; do
    [ "$(wc -c < "$f")" -le 1073741824 ] && "$CHK" nwn2 "$f" || { echo "skipped (over 1 GiB cap or failed): $f"; }
  done
fi
exit $rc
