#!/usr/bin/env python3
# SPDX-License-Identifier: BSD-3-Clause-Clear
"""Bundle the standalone driver and pinned HeliPort app, preserving framework symlinks."""
from pathlib import Path
import hashlib,json,os,plistlib,shutil,stat,subprocess,zipfile
r=Path(__file__).resolve().parents[1]
version=plistlib.loads((r/'src/Info.plist').read_bytes())['CFBundleVersion']
app=r/'userspace/HeliPort/build/Build/Products/Release/HeliPort-MT7902.app'
stage=r/'build'/f'itlmt7902-HeliPort-{version}'
if stage.exists(): shutil.rmtree(stage)
stage.mkdir(parents=True)
for source in (r/'build/itlmt7902.kext',app): shutil.copytree(source,stage/source.name,symlinks=True)
subprocess.run(['codesign','--verify','--deep','--strict',str(stage/app.name)],check=True)
for name in ('README.md','LICENSE','NOTICE.md'): shutil.copy2(r/name,stage/name)
shutil.copy2(r/'scripts/collect-logs.sh',stage/'collect-logs.sh')
licenses=stage/'licenses'; licenses.mkdir()
for source in (r/'fw/LICENCE.mediatek',r/'fw/SHA256SUMS',r/'licenses/mt76-LICENSE.txt'):
 shutil.copy2(source,licenses/source.name)
for name in ('LICENSE','NOTICE-MT7902.md'):
 shutil.copy2(r/'userspace/HeliPort'/name,licenses/('HeliPort-'+name))
kernel=(r/'build/itlmt7902.kext/Contents/MacOS/itlmt7902').read_bytes()
for blob in (r/'fw').glob('*.bin'): assert kernel.count(blob.read_bytes())==1,blob.name
assert not any('MtkWlan' in p.name for p in stage.rglob('*'))
manifest={'driver_version':version,'source_commit':subprocess.check_output(['git','rev-parse','HEAD'],cwd=r).decode().strip(),
 'driver_baseline_commit':'fc396ba','driver_backend':'standalone itlmt7902, Ethernet',
 'ui_commit':subprocess.check_output(['git','rev-parse','HEAD'],cwd=r/'userspace/HeliPort').decode().strip(),
 'heliport_adapter_hardware_validated':False,'driver_hotspot_test':'user-reported pass on the 0.2.11 baseline',
 'firmware_embedded':True,'native_wifi':False}
(stage/'build-manifest.json').write_text(json.dumps(manifest,indent=2)+'\n')
dist=r/'dist'; dist.mkdir(exist_ok=True)
archive=dist/(stage.name+'.zip')
with zipfile.ZipFile(archive,'w',zipfile.ZIP_DEFLATED) as z:
 for p in sorted(stage.rglob('*')):
  if p.is_symlink():
   assert p.resolve().is_relative_to(stage)
   item=zipfile.ZipInfo(p.relative_to(stage).as_posix()); item.create_system=3; item.external_attr=(stat.S_IFLNK|0o777)<<16
   z.writestr(item,os.readlink(p))
  elif p.is_file(): z.write(p,p.relative_to(stage))
with zipfile.ZipFile(archive) as z: assert z.testzip() is None
appzip=dist/'HeliPort-MT7902.zip'; shutil.copy2(r/'userspace/HeliPort/build/dist/HeliPort-MT7902.zip',appzip)
(dist/'ETHERNET-SHA256SUMS.txt').write_text(''.join(hashlib.sha256(p.read_bytes()).hexdigest()+'  '+p.name+'\n' for p in (archive,appzip)))
(dist/'ethernet-build-manifest.json').write_text(json.dumps(manifest,indent=2)+'\n')
print(archive)
