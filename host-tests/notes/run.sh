#!/bin/sh
# Builds and runs the NOTES parser tests. No device and no PlatformIO:
# NotesCore is freestanding C++17, which is the whole reason the CalDAV
# reading lives there rather than in the activity.
#
#   host-tests/notes/run.sh
set -e
cd "$(dirname "$0")"
# Keyed to this checkout, not just the suite name: two worktrees sharing one
# build dir means one tree can run -- and pass -- a binary the other built.
BUILD_DIR="${TMPDIR:-/tmp}/$(basename "${CXX:-c++}")-notes-tests-$(cd ../.. && pwd | cksum | cut -d" " -f1)"
mkdir -p "$BUILD_DIR"
SRC=../../src/apps_local/notes

"${CXX:-c++}" -std=c++17 -Wall -Wextra -Werror $SRC/NotesCore.cpp \
  test_notes.cpp -o "$BUILD_DIR/test_notes"
"$BUILD_DIR/test_notes"

# ---- Not here yet: the two scans that belong beside NotesActivity ----------
#
# When NotesActivity.cpp lands, two properties need a source scan here, because
# both are an ORDER between statements in a class that needs WiFi, Storage and
# a renderer to exist -- nothing freestanding can instantiate it:
#
#   1. MANUAL SYNC ONLY. No xTaskCreate, no esp_timer, and no startSync() from
#      onEnter(). The app's one promise is that it never reaches for the radio
#      by itself, and breaking it is invisible until the battery is flat.
#   2. THE FRAME BEFORE THE BLOCK. startSync() blocks the loop inside
#      HttpDownloader for as long as the server takes, so requestUpdate(true)
#      has to publish the busy frame BEFORE the first request.
#
# They are written down rather than stubbed because a scan over a file that
# does not exist passes by finding nothing, which is this repo's own documented
# way of shipping a green suite that checks nothing (docs/shelf.md).
