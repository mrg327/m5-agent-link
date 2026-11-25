#!/usr/bin/env bash
# Regenerate the zcbor codec from protocol/agentlink.cddl and refresh the vendored zcbor runtime.
# Generated files are committed so firmware builds need neither Python nor zcbor.
#   regen.sh           regenerate in place
#   regen.sh --check   regenerate into a temporary copy; fail if it differs from the committed files
set -euo pipefail

here="$(cd "$(dirname "$0")" && pwd)"
root="$(cd "$here/../../.." && pwd)"
out="$here"
if [ "${1-}" = --check ]; then
    out="$(mktemp -d)"
    trap 'rm -rf "$out"' EXIT
    cp -r "$here/src" "$here/include" "$out/"
fi
zcbor_spec=(--with 'cbor2<5.7' --from zcbor==0.9.1)

uvx "${zcbor_spec[@]}" zcbor code -c "$root/protocol/agentlink.cddl" -t envelope -d -e \
    --output-c "$out/src/agentlink_codec.c" --output-h "$out/include/agentlink_codec.h"

pkg="$(uvx "${zcbor_spec[@]}" python -c 'import zcbor,os;print(os.path.dirname(zcbor.__file__))')"
for f in zcbor_common zcbor_decode zcbor_encode; do
    cp "$pkg/src/$f.c" "$out/src/$f.c"
    cp "$pkg/include/$f.h" "$out/include/$f.h"
done
cp "$pkg/include/zcbor_tags.h" "$out/include/zcbor_tags.h"
cp "$pkg/include/zcbor_print.h" "$out/include/zcbor_print.h"
cp "$pkg/src/zcbor_print.c" "$out/src/zcbor_print.c"
# Backports not yet in a zcbor release (0.9.1 is the latest on PyPI):
# upstream a801b2422c + 95e020e89d, float16<->32 type punning (strict-aliasing UB, breaks -O2).
for p in "$here"/patches/*.patch; do
    patch -s -p1 -d "$out" < "$p"
done
if [ "$out" != "$here" ]; then
    if ! diff -ru "$here/src" "$out/src" || ! diff -ru "$here/include" "$out/include"; then
        echo "agentlink_proto is out of date: run firmware/components/agentlink_proto/regen.sh" >&2
        exit 1
    fi
    echo "agentlink_proto is up to date"
    exit 0
fi
echo "regenerated agentlink_proto"
