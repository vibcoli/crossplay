#!/bin/sh
# Builds and runs the CalDAV/CardDAV parser tests. No device and no
# PlatformIO: DavCore is freestanding C++17, which is the whole reason the
# protocol and the parsing live there rather than in the activity.
#
#   host-tests/dav/run.sh
set -e
cd "$(dirname "$0")"
# Keyed to this checkout, not just the suite name: two worktrees sharing one
# build dir means one tree can run -- and pass -- a binary the other built.
BUILD_DIR="${TMPDIR:-/tmp}/$(basename "${CXX:-c++}")-dav-tests-$(cd ../.. && pwd | cksum | cut -d" " -f1)"
mkdir -p "$BUILD_DIR"
SRC=../../src/apps_local/dav

"${CXX:-c++}" -std=c++17 -Wall -Wextra -Werror $SRC/DavCore.cpp \
  test_dav.cpp -o "$BUILD_DIR/test_dav"
"$BUILD_DIR/test_dav"

# ---- Not here yet: the scans that belong beside DavActivity ----------------
#
# When DavActivity.cpp lands, three properties need a source scan here, because
# each is an ORDER between statements in a class that needs WiFi, Storage and a
# renderer to exist -- nothing freestanding can instantiate it:
#
#   1. MANUAL SYNC ONLY. No xTaskCreate, no esp_timer, and no sync from
#      onEnter(). The app's one promise is that it never reaches for the radio
#      by itself, and breaking it is invisible until the battery is flat.
#   2. THE FRAME BEFORE THE BLOCK. A sync blocks the loop inside HttpDownloader
#      for as long as the server takes, so requestUpdate(true) has to publish
#      the busy frame BEFORE the first request.
#   3. DEDUPE IS ACTUALLY CALLED. dedupeOccurrences() exists and is tested, but
#      a function nobody calls is the repo's own documented failure mode ("a
#      repair placed where it cannot run"). The sync has to run it ONCE over
#      the merged list, after every collection has been parsed -- per
#      collection it would find nothing, because the duplicate lives in the
#      OTHER collection.
#   4. THE WINDOW IS ASKED FOR. The agenda REPORT must carry reportEventsQuery's
#      bounds rather than a bare component query: without <expand> a recurring
#      event arrives once, as its master instance, and shows up on one day a
#      year instead of every week.
#
# They are written down rather than stubbed because a scan over a file that
# does not exist passes by finding nothing, which is this repo's own documented
# way of shipping a green suite that checks nothing (docs/shelf.md).
