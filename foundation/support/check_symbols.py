#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Audit an actual relocatable net80211 link without pretending it is a loaded kext.

The MacKernelSDK supplies kernel headers, not necessarily a complete KPI export
catalog. Missing stack-owned functions are fatal regardless. Every residual
import is recorded. An authoritative export catalog, when provided, additionally
makes any non-frontend import outside that exact symbol set fatal.
"""
from pathlib import Path
import argparse
import hashlib
import json
import re
import subprocess


# These families belong to the shared stack, bridge or MT7902 source,
# not to the macOS KPI. Matching here rejects an import; it never admits one.
STACK_SYMBOLS = re.compile(
    r"(?:ieee80211_|CTimeout|timeout_|timingsafe_bcmp|"
    r"ifq_|if_input|taskq_|task_set|task_add|task_del|systq|m_defrag|"
    r"aes_|AES_|rijndael|SHA[12]|sha[12]_|hmac_|pbkdf2|"
    r"MD5|md5_|cmac_|gmac_|ghash_|michael_|arc4|RC4|"
    r"CAST_|cast_|DES_|des_|Blowfish_|blf_|RMD160|rmd160_|"
    r"poly1305|chacha|idgen_|itlmt7902|Mt7902Net80211|Mt7902NetSession|mt7902_|itl_)"
)


def symbol_set(text):
    """Read nm -j output, rejecting mixed column/header output by construction."""
    result = set()
    for raw in text.splitlines():
        name = raw.strip()
        if not name:
            continue
        if re.search(r"\s", name) or name.endswith(":"):
            raise ValueError("Expected name-only nm output")
        result.add(name)
    return result


def classify(undefined, hooks, exports=None):
    # Frontend hooks must be exact ABI symbol names with documented ownership.
    if any(not isinstance(key, str) or not key.startswith("_") or
           not isinstance(value, str) or not value for key, value in hooks.items()):
        raise ValueError("Malformed exact frontend hook allowlist")
    frontend = sorted(undefined & hooks.keys())
    missing_stack = sorted(name for name in undefined - hooks.keys() if STACK_SYMBOLS.search(name))
    residual = sorted(undefined - hooks.keys() - set(missing_stack))
    missing_exports = sorted(set(residual) - exports) if exports is not None else []
    return {
        "frontend_imports": frontend,
        "unresolved_stack_symbols": missing_stack,
        # The name describes their role only; exports_verified states evidence.
        "kernel_import_candidates": residual,
        "kernel_kpi_exports_verified": exports is not None,
        "missing_kernel_exports": missing_exports,
        "forbidden_native_wifi_imports": sorted(name for name in undefined
            if any(family in name for family in ("IO80211", "IOSkywalk", "Apple80211"))),
    }


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--source-list", type=Path, required=True)
    parser.add_argument("--frontend-hooks", type=Path, required=True)
    parser.add_argument("--kernel-export-catalog", type=Path,
                        help="Authoritative exact raw Mach-O KPI symbol names, one per line")
    args = parser.parse_args()
    output = args.output
    undefined = symbol_set((output / "undefined-symbols.txt").read_text())
    defined = symbol_set((output / "defined-symbols.txt").read_text())
    if undefined & defined:
        raise ValueError("Relocatable link did not resolve its own external definitions")
    hooks = json.loads(args.frontend_hooks.read_text())
    exports = symbol_set(args.kernel_export_catalog.read_text()) if args.kernel_export_catalog else None
    audit = classify(undefined, hooks, exports)
    sources = args.source_list.read_text().splitlines()
    staged = output / "staged/ieee80211_pae_input.c"
    shim = json.loads((output / "staged/pae-shim-manifest.json").read_text())
    if (str(staged.as_posix()) not in sources or shim.get("hook_calls") != 1 or
        shim.get("upstream_worktree_modified") is not False or
        hashlib.sha256(staged.read_bytes()).hexdigest() != shim.get("staged_pae_sha256")):
        raise ValueError("Deferred SECURE intent shim was not compiled as audited")
    archive = output / "libitlwm-net80211.a"
    linked = output / "net80211-relocatable.o"
    required = ["ieee80211_newstate", "ieee80211_input", "ieee80211_encap",
                "ieee80211_eapol_key_input", "ieee80211_ifattach", "ieee80211_ioctl",
                "timeout_set", "ifq_init", "if_input", "taskq_init", "SHA1Init", "itlmt7902",
                "mt7902_net80211_normalize_rx", "mt7902_net80211_encode_key"]
    missing_entries = [name for name in required if not any(name in symbol for symbol in defined)]
    required_methods = [("Mt7902Net80211", "initialize"), ("Mt7902Net80211", "receive"),
                        ("Mt7902Net80211", "completeState"), ("Mt7902NetSession", "begin"),
                        ("Mt7902Net80211Hardware", "bindSession"),
                        ("Mt7902Net80211Hardware", "changeState"),
                        ("Mt7902Net80211Hardware", "updateKey"),
                        ("Mt7902Net80211Hardware", "transmit"),
                        ("Mt7902Net80211Hardware", "releaseAfterQuiesce")]
    required_methods.extend([("Mt7902Net80211Runtime", "serve"),
                             ("Mt7902Net80211Runtime", "receiveDMA"),
                             ("itlmt7902", "net80211Serve")])
    for owner, method in required_methods:
        label = owner + "::" + method
        required.append(label)
        if not any(owner in symbol and method in symbol for symbol in defined):
            missing_entries.append(label)
    bridge_sources = {"foundation/net80211_bridge.cpp", "foundation/mt7902_net80211_session.cpp",
                      "foundation/mt7902_net80211_rx.cpp", "foundation/mt7902_net80211_key.cpp",
                      "foundation/net80211_runtime.cpp"}
    hardware_sources = {"foundation/mt7902_net80211_hardware.cpp", "src/itlmt7902.cpp",
                        "src/itlmt7902_m1.cpp", "src/itlmt7902_m2.cpp", "src/itlmt7902_m3.cpp",
                        "src/itlmt7902_txrx.cpp", "src/itlmt7902_uc.cpp", "src/itlmt7902_eth.cpp",
                        "src/kmod_info.c", "src/fw_blob.c"}
    missing_sources = sorted((bridge_sources | hardware_sources) - set(sources))
    closure_passed = not audit["unresolved_stack_symbols"] and not missing_entries and not missing_sources and not audit["forbidden_native_wifi_imports"]
    manifest = {
        "upstream": "OpenIntelWireless/itlwm",
        "upstream_commit": subprocess.check_output(
            ["git", "-C", "external/itlwm", "rev-parse", "HEAD"]).decode().strip(),
        "architecture": "x86_64",
        "archive_sha256": hashlib.sha256(archive.read_bytes()).hexdigest(),
        "relocatable_sha256": hashlib.sha256(linked.read_bytes()).hexdigest(),
        "compiled_source_units": sources,
        "net80211_units": sum("/net80211/" in name for name in sources),
        "dependency_units": sum("/net80211/" not in name for name in sources),
        "intel_hardware_backend_included": False,
        "mt7902_bridge_compiled_and_linked": closure_passed,
        "mt7902_hardware_executor_compiled_and_linked": closure_passed,
        "runtime_entry_compiled": closure_passed,
        "runtime_binding_enabled": closure_passed,
        "mt7902_runtime_integrated": closure_passed,
        "installable_kext": False,
        "validation_scope": "runtime entry, stack and MT7902 backend compiled with relocatable link; no hardware or RF validation",
        "async_secure_intent_shim": shim,
        "verified_entry_points": sorted(set(required) - set(missing_entries)),
        "missing_entry_points": missing_entries,
        "missing_source_units": missing_sources,
        "frontend_hook_ownership": {name: hooks[name] for name in audit["frontend_imports"]},
        **audit,
    }
    manifest["stack_dependency_closure_passed"] = closure_passed
    manifest_path = output / "manifest.json"
    manifest_path.write_text(json.dumps(manifest, indent=2) + "\n")
    print(json.dumps(manifest, indent=2))
    if not manifest["stack_dependency_closure_passed"] or audit["missing_kernel_exports"]:
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
