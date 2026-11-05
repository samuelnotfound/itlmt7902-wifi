#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Regression checks for the only intentional edit to staged upstream PAE."""
from pathlib import Path
import subprocess
import unittest

from stage_net80211_pae import (
    ANCHOR, BLOB, DECLARATION, HOOK_DECLARATION, REPLACEMENT, stage_pae,
)


class PaeShimTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        upstream = Path(__file__).resolve().parents[1] / 'external/itlwm'
        cls.original = subprocess.check_output(['git', '-C', str(upstream), 'cat-file', 'blob', BLOB])

    def test_only_authenticated_secure_intent_is_hooked(self):
        original = self.original.decode('utf-8')
        staged = stage_pae(self.original).decode('utf-8')
        call = 'mt7902_net80211_deferred_port_valid(ic, ni);'
        self.assertEqual(staged.count(call), 1)
        self.assertIn(REPLACEMENT, staged)
        self.assertIn('if (info & EAPOL_KEY_SECURE)', REPLACEMENT)
        self.assertIn('if (deferlink != 0)\n                ' + call, REPLACEMENT)
        # INSTALL can enable encryption but cannot authorize the controlled port.
        install_only = ('    if (info & EAPOL_KEY_INSTALL)\n'
                        '        ni->ni_flags |= IEEE80211_NODE_TXRXPROT;')
        self.assertIn(install_only, original)
        self.assertIn(install_only, staged)
        self.assertEqual(staged.index(call) > staged.index(install_only), True)
        self.assertLess(staged.index('if (info & EAPOL_KEY_SECURE)', staged.index(install_only)),
                        staged.index(call))
        # Reversing exactly the notification and declaration recovers every byte.
        restored = staged.replace(REPLACEMENT, ANCHOR).replace(HOOK_DECLARATION, DECLARATION)
        self.assertEqual(restored.encode('utf-8'), self.original)

    def test_deterministic_and_does_not_mutate_source(self):
        before = bytes(self.original)
        self.assertEqual(stage_pae(before), stage_pae(before))
        self.assertEqual(before, self.original)
        with self.assertRaises(ValueError):
            stage_pae(stage_pae(before))

    def test_unexpected_source_fails_closed(self):
        original = self.original.decode('utf-8')
        changed = [
            original.replace(ANCHOR, ''),
            original + ANCHOR,
            original.replace(ANCHOR, ANCHOR.replace('EAPOL_KEY_SECURE', 'EAPOL_KEY_INSTALL')),
            original.replace(ANCHOR, ANCHOR.replace('deferlink == 0', 'deferlink != 0')),
            original.replace(DECLARATION, ''),
            original + DECLARATION,
        ]
        for text in changed:
            with self.subTest(source_length=len(text)), self.assertRaises(ValueError):
                stage_pae(text.encode('utf-8'))


if __name__ == '__main__':
    unittest.main()
