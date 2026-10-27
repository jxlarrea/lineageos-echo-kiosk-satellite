#!/usr/bin/env bash
# The standard wake word and voice test for an Echo (.internal/docs/audio-testing.md).
# It records the Echo's microphones once, then replays mixes of those
# recordings through the image's own microphone processing and scores them
# against the minimum every release must reach.
#
#   scripts/audio-test.sh capture --serial <serial>   record the corpus
#   scripts/audio-test.sh run --serial <serial>       score the processing
#   scripts/audio-test.sh live --serial <serial>      check the live path
#
# The Python side runs in its own environment under the workspace, made with
# uv the first time.
set -euo pipefail

REPO_ROOT="$(cd "$(dirname "$0")/.." && pwd)"
WORKSPACE_DIR="$(realpath "${ECHO_WORKSPACE:-$REPO_ROOT/workspace}")"
VENV="$WORKSPACE_DIR/audio-test/venv"

if [[ ! -x "$VENV/bin/python" ]]; then
  command -v uv >/dev/null || { echo "ERROR: uv is needed to set up the test environment" >&2; exit 1; }
  uv venv -q "$VENV"
  VIRTUAL_ENV="$VENV" uv pip install -q numpy pymicro-wakeword pymicro-features
fi

exec "$VENV/bin/python" -I "$REPO_ROOT/scripts/audio_test.py" \
  --repo "$REPO_ROOT" --workspace "$WORKSPACE_DIR" "$@"
