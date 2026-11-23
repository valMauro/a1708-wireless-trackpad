#!/bin/sh
# Build and install Trackpad Companion with the A1708 patch.
# Usage: ./build.sh [path/to/macos-trackpad-companion]
# Clones the upstream app (at the commit the patch was made against) if the folder does not exist.
# Signs with the "Trackpad Companion Dev" certificate if it exists (see docs/en/macos.md),
# otherwise ad-hoc (then macOS asks for the permissions again after every rebuild).
set -e
HERE="$(cd "$(dirname "$0")" && pwd)"
APP_SRC="${1:-$HERE/../../macos-trackpad-companion}"
UPSTREAM_COMMIT=1c1bc17
PATCH="$HERE/macos-trackpad-companion.patch"

if [ ! -d "$APP_SRC/.git" ]; then
	git clone https://github.com/mathias-sm/macos-trackpad-companion.git "$APP_SRC"
	git -C "$APP_SRC" checkout -q "$UPSTREAM_COMMIT"
fi
cd "$APP_SRC"
if git apply --check "$PATCH" 2>/dev/null; then
	git apply "$PATCH"
	echo "patch applied"
elif git apply --reverse --check "$PATCH" 2>/dev/null; then
	echo "patch already applied"
else
	echo "the patch does not apply: check out upstream commit $UPSTREAM_COMMIT first" >&2
	exit 1
fi

if security find-identity -p codesigning 2>/dev/null | grep -q "Trackpad Companion Dev"; then
	export SIGN_IDENTITY="Trackpad Companion Dev"
fi
osascript -e 'quit app "Trackpad Companion"' 2>/dev/null || true
INSTALL=1 INSTALL_DIR=/Applications ./scripts/bundle.sh
open "/Applications/Trackpad Companion.app"
