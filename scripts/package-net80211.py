#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Package the linked net80211 runtime, source snapshots and build evidence."""
from pathlib import Path
import hashlib
import json
import plistlib
import shutil
import struct
import subprocess
import zipfile

ROOT = Path(__file__).resolve().parents[1]
OUT = ROOT / 'build/net80211-foundation'
VERSION = '0.3.0'
PIN = '53c51c2cdd6e4b69beb91f310d74c53422b0f8bd'


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def git(*args, cwd=ROOT):
    return subprocess.check_output(['git', '-C', str(cwd), *args])


def main():
    audit = json.loads((OUT / 'manifest.json').read_text())
    for key in ('mt7902_runtime_integrated', 'runtime_binding_enabled',
                'stack_dependency_closure_passed'):
        assert audit[key] is True, key
    for key in ('frontend_imports', 'unresolved_stack_symbols',
                'missing_entry_points', 'missing_source_units',
                'forbidden_native_wifi_imports', 'missing_kernel_exports'):
        assert not audit[key], key
    assert audit['upstream_commit'] == PIN
    bundle = OUT / 'itlmt7902.kext'
    info = plistlib.loads((bundle / 'Contents/Info.plist').read_bytes())
    assert info['CFBundleVersion'] == info['CFBundleShortVersionString'] == VERSION
    personality = info['IOKitPersonalities']['itlmt7902']
    assert personality['IOPCIMatch'] == '0x790214c3'
    assert all(not value for key, value in personality.items()
               if key.startswith(('itl_psk', 'itl_ssid')))
    assert not any('IO80211' in key or 'Skywalk' in key for key in info['OSBundleLibraries'])
    binary_path = bundle / 'Contents/MacOS/itlmt7902'
    binary = binary_path.read_bytes()
    magic, cpu, _, kind = struct.unpack_from('<IIII', binary)
    assert (magic, cpu, kind) == (0xfeedfacf, 0x01000007, 11), 'Expected x86_64 MH_KEXT_BUNDLE'
    assert VERSION.encode() + b'\0' in binary
    assert b'0.3.0-net80211\0' in binary and b'net80211-runtime-test\0' in binary
    firmware = {}
    for line in (ROOT / 'fw/SHA256SUMS').read_text().splitlines():
        expected, name = line.split()
        path = ROOT / 'fw' / name
        assert digest(path) == expected and binary.count(path.read_bytes()) == 1, name
        firmware[name] = expected
    assert len(firmware) == 2
    # Audit the final linked image too, rather than relying only on the -r image.
    imports = subprocess.check_output(['xcrun', 'nm', '-ju', str(binary_path)]).decode()
    assert not any(name in imports for name in ('IO80211', 'IOSkywalk', 'Apple80211'))
    expected_imports = set(audit['kernel_import_candidates'])
    assert set(imports.split()) <= expected_imports, 'Final link introduced an unaudited import'

    dist = OUT / 'dist'
    package = dist / ('itlmt7902-net80211-' + VERSION)
    # This exact generated directory is the only recursive replacement target.
    assert package.resolve().parent == (ROOT / 'build/net80211-foundation/dist').resolve()
    if package.exists():
        shutil.rmtree(package)
    package.mkdir(parents=True)
    shutil.copytree(bundle, package / bundle.name)
    for name in ('LICENSE', 'NOTICE.md'):
        shutil.copy2(ROOT / name, package / name)
    shutil.copy2(ROOT / 'docs/NET80211-0.3.0.md', package / 'README.md')
    shutil.copy2(ROOT / 'scripts/collect-logs.sh', package / 'collect-logs.sh')
    shutil.copytree(ROOT / 'licenses', package / 'licenses')
    shutil.copy2(ROOT / 'external/itlwm/LICENSE', package / 'licenses/itlwm-GPL-2.0.txt')
    shutil.copy2(ROOT / 'fw/LICENCE.mediatek', package / 'licenses/LICENCE.mediatek')
    shutil.copy2(ROOT / 'fw/SHA256SUMS', package / 'licenses/FIRMWARE-SHA256SUMS')
    evidence = package / 'build-evidence'
    evidence.mkdir()
    for name in ('manifest.json', 'compiled-sources.txt', 'undefined-symbols.txt', 'defined-symbols.txt'):
        shutil.copy2(OUT / name, evidence / name)
    shutil.copy2(OUT / 'staged/pae-shim-manifest.json', evidence / 'pae-shim-manifest.json')
    sources = package / 'SOURCES'
    sources.mkdir()
    commit = git('rev-parse', 'HEAD').decode().strip()
    assert not git('status', '--porcelain', '--untracked-files=no'), 'Package committed source only'
    for repo, name in ((ROOT, 'driver'), (ROOT / 'external/itlwm', 'itlwm')):
        archive = git('archive', '--format=tar.gz', '--prefix=' + name + '/', 'HEAD', cwd=repo)
        (sources / (name + '.tar.gz')).write_bytes(archive)
    # The generated PAE modification and module descriptor are also preferred source.
    shutil.copy2(OUT / 'staged/ieee80211_pae_input.c', sources / 'ieee80211_pae_input.c')
    shutil.copy2(OUT / 'runtime-kmod-info.c', sources / 'runtime-kmod-info.c')
    (sources / 'BUILD.txt').write_text(
        'Driver source: https://github.com/samuelnotfound/itlmt7902-wifi/tree/' + commit + '\n'
        'Official stack: https://github.com/OpenIntelWireless/itlwm/tree/' + PIN + '\n'
        'Extract driver.tar.gz; extract itlwm.tar.gz into driver/external/ (itlwm/).\n'
        'Obtain MacKernelSDK commit 3f750085caa17ec3a7880f11c11bf4f48cd6a164.\n'
        'Build with Xcode 16.4 on Intel macOS:\n'
        'MACKERNELSDK=/path/to/MacKernelSDK bash scripts/build-net80211-foundation.sh\n'
        'For archive-only source without .git, restore the exact Git commits first;\n'
        'the build deliberately verifies the upstream Git pin and clean worktree.\n'
        'All original per-file copyright and BSD/GPL notices are retained in the archives.\n'
        'HeliPort is a separate app, not required to compile this kernel bundle.\n')
    manifest = dict(audit, version=VERSION, source_commit=commit,
                    installable_kext=True, kernel_bundle_linked=True,
                    hardware_validated=False, macos_load_validated=False,
                    validation_scope='Host sanitizer tests, compile, final kext link and package audits; hardware unverified',
                    firmware_sha256=firmware, binary_sha256=digest(binary_path),
                    heliport_abi=1, heliport_version='2.1.5', connection_engine='net80211')
    (package / 'build-manifest.json').write_text(json.dumps(manifest, indent=2) + '\n')
    (package / 'SHA256SUMS.txt').write_text(''.join(
        digest(path) + '  ' + path.relative_to(package).as_posix() + '\n'
        for path in sorted(package.rglob('*')) if path.is_file()))
    archive = dist / (package.name + '.zip')
    with zipfile.ZipFile(archive, 'w', zipfile.ZIP_DEFLATED, compresslevel=9) as zipped:
        for path in sorted(package.rglob('*')):
            if path.is_file():
                zipped.write(path, path.relative_to(package.parent))
    with zipfile.ZipFile(archive) as zipped:
        assert zipped.testzip() is None
    (dist / 'SHA256SUMS.txt').write_text(digest(archive) + '  ' + archive.name + '\n')
    (dist / 'build-manifest.json').write_text(json.dumps(manifest, indent=2) + '\n')
    print('Runtime candidate:', archive, digest(archive))


if __name__ == '__main__':
    main()
