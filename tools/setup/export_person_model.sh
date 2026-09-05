#!/usr/bin/env bash
# Regenerates the bundled person-detection model: yolov8n -> ONNX at 320x320.
#
# The export is committed, so re-run this only when the input size or the
# ultralytics version changes. Requires uv; Python 3.12 because torch has no
# 3.14 wheels yet.

set -euo pipefail

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
VENV="${TMPDIR:-/tmp}/qgc-yolo-venv"
DEST="${REPO_ROOT}/src/PersonDetection/models/yolov8n_320.onnx"

if [ ! -x "${VENV}/bin/python" ]; then
    uv venv -p 3.12 "${VENV}"
fi
uv pip install --python "${VENV}/bin/python" ultralytics onnx onnxslim onnxruntime

# Export writes yolov8n.onnx beside the auto-downloaded yolov8n.pt, so use a scratch dir.
WORK="$(mktemp -d)"
cd "${WORK}"
"${VENV}/bin/python" -c "
from ultralytics import YOLO
YOLO('yolov8n.pt').export(format='onnx', imgsz=320, opset=17, dynamic=False, simplify=True, half=False, batch=1)
"

mkdir -p "$(dirname "${DEST}")"
cp "${WORK}/yolov8n.onnx" "${DEST}"
echo "Wrote ${DEST}"
