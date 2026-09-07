#!/usr/bin/env bash
# Fetches the bundled person-detection model and checks it is the one we measured.
#
# The weights are vendored, not exported here: they are an ONNX re-export of the
# person detector MMPose ships with RTMPose (RTMDet-nano, 320x320, Apache-2.0),
# published at the URL below. The export bakes its own NMS in, which is why the
# descriptor names the detsLabels layout, and does NOT fold the normalisation in,
# which is why it names bgrMeanStd. Both were measured against the file this hash
# pins; a different file may need different descriptor values.

set -euo pipefail

URL="https://huggingface.co/bukuroo/RTMDet-ONNX/resolve/main/rtmdet-n-person.onnx"
SHA256="02c067786c150044fea7ede0e802dd18a1114ee7d776a3125d2d2989b846eb26"

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
DEST="${REPO_ROOT}/src/PersonDetection/models/rtmdet-n-person.onnx"

mkdir -p "$(dirname "${DEST}")"
curl -fL "${URL}" -o "${DEST}.tmp"

if ! echo "${SHA256}  ${DEST}.tmp" | shasum -a 256 -c -; then
    rm -f "${DEST}.tmp"
    echo "Hash mismatch: upstream changed the file. Re-measure before accepting it." >&2
    exit 1
fi

mv "${DEST}.tmp" "${DEST}"
echo "Wrote ${DEST}"
