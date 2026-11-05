#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Symbol audits must fail missing stack dependencies, not wave them through as KPI."""
import unittest
from check_symbols import classify, symbol_set


class SymbolAuditTests(unittest.TestCase):
    def test_nm_input_requires_name_only(self):
        self.assertEqual(symbol_set("_IOLog\n__fWorkloop\n_IOLog\n"), {"_IOLog", "__fWorkloop"})
        for bad in ("file.o:", "0000000000000000 T _IOLog", " U _IOLog"):
            with self.assertRaises(ValueError):
                symbol_set(bad)

    def test_only_exact_documented_hooks_are_admitted(self):
        hooks = {"__fWorkloop": "Frontend-owned work loop"}
        audit = classify({"__fWorkloop", "__fWorkloop_wrong", "_IOLog"}, hooks)
        self.assertEqual(audit["frontend_imports"], ["__fWorkloop"])
        self.assertIn("__fWorkloop_wrong", audit["kernel_import_candidates"])
        self.assertFalse(audit["kernel_kpi_exports_verified"])
        for bad in ({"*": "all hooks"}, {"__fWorkloop": ""}):
            with self.assertRaises(ValueError):
                classify(set(), bad)

    def test_missing_crypto_timer_state_or_queue_is_fatal(self):
        missing = {"__Z17ieee80211_newstateP12ieee80211comii", "__Z11timeout_setPP8CTimeoutPFvPvES3_",
                   "__Z8SHA1InitP8SHA1_CTX", "__Z8ifq_initP8_ifqueueP6_ifnetj", "__ZN8CTimeout9metaClassE"}
        audit = classify(missing | {"_IOLog"}, {})
        self.assertEqual(set(audit["unresolved_stack_symbols"]), missing)
        self.assertEqual(audit["kernel_import_candidates"], ["_IOLog"])

    def test_authoritative_catalog_has_no_wildcard_admission(self):
        audit = classify({"_IOLog", "_MissingKernelAPI"}, {}, {"_IOLog"})
        self.assertTrue(audit["kernel_kpi_exports_verified"])
        self.assertEqual(audit["missing_kernel_exports"], ["_MissingKernelAPI"])

    def test_bridge_and_real_hardware_methods_cannot_be_kpi_candidates(self):
        missing = {"__ZN15Mt7902Net8021110initializeEv", "__ZN16Mt7902NetSession5beginEb",
                   "__ZN9itlmt79028m3SetKeyEv", "__Z33mt7902_net80211_normalize_rxPKh",
                   "_itl_fw_patch", "__Z11itl_wpa_pmkPKc"}
        audit = classify(missing | {"_IOLog"}, {})
        self.assertEqual(set(audit["unresolved_stack_symbols"]), missing)
        self.assertEqual(audit["kernel_import_candidates"], ["_IOLog"])


if __name__ == "__main__":
    unittest.main()
