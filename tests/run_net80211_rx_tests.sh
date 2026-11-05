#!/bin/sh
# SPDX-License-Identifier: BSD-3-Clause-Clear
set -eu
test_root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
test_dir=$(mktemp -d "${TMPDIR:-/tmp}/mt7902-net80211-rx.XXXXXX")
trap 'rm -rf "$test_dir"' EXIT HUP INT TERM
"${CXX:-c++}" -std=c++11 -Wall -Wextra -Werror -pedantic \
    -fsanitize=address,undefined -fno-omit-frame-pointer -g \
    "$test_root/codec/mt7902_rx_codec.cpp" \
    "$test_root/foundation/mt7902_net80211_rx.cpp" \
    "$test_root/tests/test_net80211_rx.cpp" -o "$test_dir/test_net80211_rx"
"$test_dir/test_net80211_rx"
