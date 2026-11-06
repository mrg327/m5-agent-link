#!/usr/bin/env bash
# Regenerate the zcbor codec from protocol/agentlink.cddl and refresh the vendored zcbor runtime.
# Generated files are committed so firmware builds need neither Python nor zcbor.
set -euo pipefail

here="$(cd "$(dirname "$0")" && pwd)"
root="$(cd "$here/../../.." && pwd)"
zcbor_spec=(--with 'cbor2<5.7' --from zcbor==0.9.1)

uvx "${zcbor_spec[@]}" zcbor code -c "$root/protocol/agentlink.cddl" -t envelope -d -e \
    --output-c "$here/src/agentlink_codec.c" --output-h "$here/include/agentlink_codec.h"

pkg="$(uvx "${zcbor_spec[@]}" python -c 'import zcbor,os;print(os.path.dirname(zcbor.__file__))')"
for f in zcbor_common zcbor_decode zcbor_encode; do
    cp "$pkg/src/$f.c" "$here/src/$f.c"
    cp "$pkg/include/$f.h" "$here/include/$f.h"
done
cp "$pkg/include/zcbor_tags.h" "$here/include/zcbor_tags.h"
cp "$pkg/include/zcbor_print.h" "$here/include/zcbor_print.h"
cp "$pkg/src/zcbor_print.c" "$here/src/zcbor_print.c"
echo "regenerated agentlink_proto"
