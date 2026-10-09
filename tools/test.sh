#!/usr/bin/env bash
# Build and run the test suite in the AppImage's toolchain image, as CI does.
#
#   tools/test.sh [ctest args...]
set -euo pipefail

here=$(cd "$(dirname "$0")" && pwd)
project=$(cd "$here/.." && pwd)

# run.sh forwards QUACK_* variables, not arguments.
export QUACK_CTEST_ARGS="$*"
exec "$project/docker/run.sh" common tools/test-in-image.sh
