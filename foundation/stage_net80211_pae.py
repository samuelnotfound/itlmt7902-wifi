#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Stage one audited async-SECURE notification without modifying upstream."""
import argparse
import hashlib
import json
from pathlib import Path
import subprocess

PIN = '53c51c2cdd6e4b69beb91f310d74c53422b0f8bd'
PATH = 'itl80211/openbsd/net80211/ieee80211_pae_input.c'
BLOB = '5c97240e351027f488daa65ad8dfb45c29c9bd05'
ANCHOR = '''    if (info & EAPOL_KEY_SECURE) {
        ni->ni_flags |= IEEE80211_NODE_TXRXPROT;
#ifndef IEEE80211_STA_ONLY
        if (ic->ic_opmode != IEEE80211_M_IBSS ||
            ++ni->ni_key_count == 2)
#endif
        {
            if (deferlink == 0) {'''
REPLACEMENT = ANCHOR.replace(
    '            if (deferlink == 0) {',
    '            if (deferlink != 0)\n'
    '                mt7902_net80211_deferred_port_valid(ic, ni);\n'
    '            if (deferlink == 0) {')
DECLARATION = '#include <net80211/ieee80211_priv.h>'
HOOK_DECLARATION = (DECLARATION + '\n\n'
    '// MT7902 async hardware-key completion preserves authenticated SECURE intent.\n'
    'void mt7902_net80211_deferred_port_valid(struct ieee80211com *, struct ieee80211_node *);')


def stage_pae(original: bytes) -> bytes:
    """Apply exactly one SECURE/deferred-key notification to immutable source."""
    text = original.decode('utf-8')
    if (text.count(ANCHOR) != 1 or text.count(DECLARATION) != 1 or
            'mt7902_net80211_deferred_port_valid' in text):
        raise ValueError('Async SECURE patch anchor changed')
    return text.replace(ANCHOR, REPLACEMENT).replace(
        DECLARATION, HOOK_DECLARATION).encode('utf-8')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--source', type=Path, default=Path('external/itlwm'))
    parser.add_argument('--output', type=Path, default=Path('build/net80211-foundation/staged'))
    args = parser.parse_args()

    def git(*arguments):
        return subprocess.check_output(['git', '-C', str(args.source), *arguments])

    if git('rev-parse', 'HEAD').decode().strip() != PIN:
        raise SystemExit('Unexpected upstream commit; async SECURE shim requires review')
    if git('status', '--porcelain', '--untracked-files=no').strip():
        raise SystemExit('Upstream worktree is modified')
    if git('rev-parse', 'HEAD:' + PATH).decode().strip() != BLOB:
        raise SystemExit('Unexpected PAE source blob')
    original = git('cat-file', 'blob', BLOB)
    try:
        staged_bytes = stage_pae(original)
    except ValueError as error:
        raise SystemExit(str(error)) from error
    output = args.output
    output.mkdir(parents=True, exist_ok=True)
    (output / 'ieee80211_pae_input.c').write_bytes(staged_bytes)
    manifest = {
        'upstream_commit': PIN, 'upstream_pae_blob': BLOB,
        'upstream_pae_sha256': hashlib.sha256(original).hexdigest(),
        'staged_pae_sha256': hashlib.sha256(staged_bytes).hexdigest(),
        'patch': 'async SECURE intent notification for deferred hardware keys',
        'upstream_worktree_modified': False,
        'hook_calls': staged_bytes.count(b'mt7902_net80211_deferred_port_valid(ic, ni);'),
    }
    (output / 'pae-shim-manifest.json').write_text(json.dumps(manifest, indent=2) + '\n')
    print(json.dumps(manifest, indent=2))


if __name__ == '__main__':
    main()
