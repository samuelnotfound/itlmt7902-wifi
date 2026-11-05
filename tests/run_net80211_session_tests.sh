#!/bin/sh
# SPDX-License-Identifier: GPL-2.0-or-later
set -eu
test_root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
test_dir=$(mktemp -d "${TMPDIR:-/tmp}/mt7902-net80211-session.XXXXXX")
trap 'rm -rf "$test_dir"' EXIT HUP INT TERM
"${CXX:-c++}" -std=c++17 -Wall -Wextra -Werror -fsanitize=address,undefined \
    -fno-omit-frame-pointer -g "$test_root/tests/test_net80211_session.cpp" \
    "$test_root/foundation/mt7902_net80211_session.cpp" -o "$test_dir/session_test"
"$test_dir/session_test"
