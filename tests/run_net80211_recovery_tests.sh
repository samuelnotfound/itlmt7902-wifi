#!/bin/sh
# SPDX-License-Identifier: GPL-2.0-or-later
set -eu
test_root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
test_dir=$(mktemp -d "${TMPDIR:-/tmp}/mt7902-recovery.XXXXXX")
trap 'rm -rf "$test_dir"' EXIT HUP INT TERM
"${CXX:-c++}" -std=c++17 -Wall -Wextra -Werror -fsanitize=address,undefined \
    -fno-omit-frame-pointer -g "$test_root/tests/test_net80211_recovery.cpp" -o "$test_dir/test_recovery"
"$test_dir/test_recovery"
