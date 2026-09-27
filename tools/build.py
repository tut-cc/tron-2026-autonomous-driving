"""Offline source build; never connects to or programs hardware."""
from pathlib import Path
import argparse,atexit,concurrent.futures,hashlib,json,os,subprocess,shutil,tempfile
ROOT=Path(__file__).resolve().parent.parent  # tools/ -> demo root
EVIDENCE_TAG=json.loads((ROOT/'tools/evidence_tag.json').read_text(encoding='utf-8'))['tag']
p=argparse.ArgumentParser();p.add_argument('--core',choices=['CPU0','CPU1','all'],default='all');p.add_argument('--jobs',type=int,default=4);p.add_argument('--clean',action='store_true');p.add_argument('--profile',choices=['dry-run','vehicle-output'],default='dry-run',help='CLI default is the safe dry-run; IDE Solution explicitly selects vehicle-output');p.add_argument('--allow-physical-output',action='store_true');args=p.parse_args()
if args.profile == 'vehicle-output' and not args.allow_physical_output:
 raise SystemExit('vehicle-output requires explicit --allow-physical-output; no motor output is enabled by default')
if args.profile == 'dry-run' and args.allow_physical_output:
 raise SystemExit('--allow-physical-output is valid only with --profile vehicle-output')
PROFILE_FINGERPRINT=hashlib.sha256(('tron.vehicle.profile.v1\0'+args.profile+'\0'+str(int(args.profile=='vehicle-output'))).encode()).hexdigest().upper()
def find_toolchain():
 required=('clang.exe','clang++.exe','llvm-size.exe','llvm-objcopy.exe')
 explicit=os.environ.get('LLVM_ARM_BIN')
 if explicit:
  candidate=Path(explicit)
  if all((candidate/name).is_file() for name in required):return candidate
  raise SystemExit(f'LLVM_ARM_BIN is incomplete: {candidate}')
 preferred=Path('C:/Renesas/RA/e2studio_v2026-04.2_fsp_v6.5.0/toolchains/llvm_arm/ATfE-21.1.1-Windows-x86_64/bin')
 if all((preferred/name).is_file() for name in required):return preferred
 candidates=sorted(Path('C:/Renesas/RA').glob('e2studio_*/toolchains/llvm_arm/ATfE-21.1.1-*/bin'))
 candidates=[path for path in candidates if all((path/name).is_file() for name in required)]
 if candidates:return candidates[-1]
 raise SystemExit('Arm Toolchain for Embedded LLVM 21.1.1 not found; set LLVM_ARM_BIN')
tool=find_toolchain()
temp=Path(tempfile.mkdtemp(prefix='vehicleoutput-build-'))
atexit.register(shutil.rmtree,temp,True)
os.environ['TEMP']=os.environ['TMP']=str(temp)
def run(command,cwd):
 r=subprocess.run([str(x) for x in command],cwd=cwd,stdout=subprocess.PIPE,stderr=subprocess.STDOUT,text=True,errors='replace')
 if r.returncode:raise RuntimeError(r.stdout)
 return r.stdout
for core in (['CPU0','CPU1'] if args.core=='all' else [args.core]):
 base=ROOT/core;out=base/'Build';out.mkdir(exist_ok=True);spec=json.loads((base/'sources.json').read_text())
 stamp=out/'build_profile.json'
 generated=(out/'build_profile.json',out/'build_metadata.json',out/f'{core}.elf',out/f'{core}.srec',out/f'{core}.map',out/'link.rsp')
 other_core='CPU1' if core=='CPU0' else 'CPU0'
 other_stamp=ROOT/other_core/'Build/build_profile.json'
 if not args.clean and other_stamp.exists():
  other=json.loads(other_stamp.read_text(encoding='utf-8'))
  if other.get('profile') != args.profile or other.get('profile_fingerprint') != PROFILE_FINGERPRINT:
   raise SystemExit(f'{core}: sibling {other_core} Build profile {other.get("profile")} does not match {args.profile}; clean both cores before switching profiles')
 if args.clean:
  if (out/'obj').exists(): shutil.rmtree(out/'obj')
  for path in generated:
   if path.exists(): path.unlink()
 elif stamp.exists():
  previous=json.loads(stamp.read_text(encoding='utf-8'))
  if previous.get('profile') != args.profile or previous.get('profile_fingerprint') != PROFILE_FINGERPRINT:
   raise SystemExit(f'{core}: existing Build profile {previous.get("profile")} does not match {args.profile}; rerun with --clean')
 elif any(path.exists() for path in generated[2:]):
  raise SystemExit(f'{core}: generated artifacts have no build_profile.json; rerun with --clean')
 if core=='CPU0':
  # CPU0 is one link image: µT-Kernel RA FSP/ARMv8-M, lwIP/httpd and the
  # application gateway are source-linked here, not a side-by-side project.
  kernel=base/'mtk3_bsp2'
  extra=[]
  extra += [f.relative_to(base).as_posix() for f in (kernel/'mtkernel/kernel').rglob('*.c')
            if '\\sysdepend\\' not in str(f)]
  extra += [f.relative_to(base).as_posix() for f in (kernel/'mtkernel/lib').rglob('*.c')
            if ('cpu/core/armv8m' in f.as_posix() or f.name in ('fastlock.c','fastmlock.c','kmalloc.c','libtm.c','libtm_printf.c'))]
  extra += [f.relative_to(base).as_posix() for f in (kernel/'sysdepend/ra_fsp/cpu/core/armv8m').rglob('*.c')]
  extra += [f.relative_to(base).as_posix() for f in (kernel/'sysdepend/ra_fsp/cpu/core/armv8m').rglob('*.S')]
  extra += [f.relative_to(base).as_posix() for f in (kernel/'sysdepend/ra_fsp/device').rglob('*.c')]
  extra += [f.relative_to(base).as_posix() for f in (kernel/'sysdepend/ra_fsp').glob('*.c')]
  extra += [(kernel/'mtkernel/device/common/drvif/msdrvif.c').relative_to(base).as_posix()]
  extra += [f.relative_to(base).as_posix() for f in (kernel/'sysdepend/ra_fsp/lib/libtk/cpu/core/armv8m').rglob('*.c')]
  extra += [f.relative_to(base).as_posix() for f in (kernel/'sysdepend/ra_fsp/lib/libtm/ek_ra8p1').rglob('*.c')]
  # Only the production lwIP/httpd path is part of this image.  Do not pull
  # in examples, generators, PPP/SNMP/IPv6, or generated fsdata.c as if they
  # were application sources.
  lwip = kernel/'uct/lwip/src/lwip/src'
  lwip_sources = []
  for subdir in ('api', 'core', 'core/ipv4'):
   lwip_sources += list((lwip/subdir).glob('*.c'))
  lwip_sources += [lwip/'netif/ethernet.c', lwip/'apps/http/httpd.c', lwip/'apps/http/fs.c']
  lwip_sources += list((kernel/'uct/lwip/src').glob('sys_arch.c'))
  lwip_sources += list((kernel/'uct/lwip/src').glob('sys_rand.c'))
  lwip_sources += list((kernel/'uct/lwip/src').glob('tknetif.c'))
  extra += [f.relative_to(base).as_posix() for f in lwip_sources]
  spec['sources'] += sorted(set(extra))
 flags=['--target=arm-none-eabi','-mcpu='+('cortex-m85' if core=='CPU0' else 'cortex-m33'),'-mthumb','-mfloat-abi=hard','-Os','-g3','-fdebug-compilation-dir=.','-ffunction-sections','-fdata-sections','-fno-strict-aliasing','-funsigned-char','-fshort-enums','-Wno-parentheses-equality','-D_RENESAS_RA_',f'-D_RA_CORE={core}','-D_RA_ORDINAL='+('1' if core=='CPU0' else '2')]
 flags += ['-DMOTOR_PHYSICAL_OUTPUT_ENABLE='+('1' if args.profile=='vehicle-output' else '0')]
 if core=='CPU0': flags += ['-DM85_UKERNEL','-DMTKBSP_RAFSP','-DMTKBSP_CPU_CORE_ARMV8M','-DMTKBSP_CPU_CORE_ARMV81M','-D_RAFSP_EK_RA8P1_']
 flags+=['-DARM_NPU','-DARM_MODEL_USE_PMU_COUNTERS','-DTF_LITE_STATIC_MEMORY'] if core=='CPU0' else ['-mfpu=fpv5-sp-d16','-D_RAFSP_EK_RA8P1_M33_']
 # Keep compiler and link response inputs independent of the extraction path.
 # This lets the same package be rechecked from another short ASCII folder.
 flags+=['-I'+str(i).replace('\\','/') for i in spec['includes']]
 hh=hashlib.sha256()
 for f in sorted(list(base.rglob('*.h'))+list((ROOT/'control').rglob('*.h'))):hh.update(f.read_bytes())
 hh.update(' '.join(flags).encode());header_hash=hh.digest()
 # Per-pixel / per-anchor loops of the camera AI path are built with -O2 (the
 # rest stays -Os).  Same semantics (no fast-math); lets clang use Helium (MVE)
 # on the M85.  The optimisation level is part of the object digest.
 HOT_O2={'src/obstacle_kernels.c','src/frame_rotation.c','src/road_navigation.c'} if core=='CPU0' else set()
 def compile_one(source):
  src=(base/source).resolve();obj=out/'obj'/Path(source.replace('../','shared/')).with_suffix('.o');obj.parent.mkdir(parents=True,exist_ok=True);stamp=obj.with_suffix('.sha256')
  opt=['-O2'] if source in HOT_O2 else []
  digest=hashlib.sha256(src.read_bytes()+header_hash+' '.join(opt).encode()).hexdigest()
  if not args.clean and obj.exists() and stamp.exists() and stamp.read_text()==digest:return obj
  cxx=src.suffix in ['.cc','.cpp'];language=['-std=c++17'] if cxx else (['-std=c11'] if src.suffix=='.c' else (['-x','assembler-with-cpp'] if src.suffix=='.S' else []))
  file_flags=[('-O2' if f=='-Os' else f) for f in flags] if opt else flags
  try:run([tool/('clang++.exe' if cxx else 'clang.exe')]+file_flags+language+['-c',source,'-o',obj.relative_to(base)],base)
  except RuntimeError as e:raise RuntimeError(source+'\n'+str(e))
  stamp.write_text(digest);return obj
 print(core,'compiling',len(spec['sources']),'units',flush=True)
 with concurrent.futures.ThreadPoolExecutor(max_workers=args.jobs) as pool:objects=list(pool.map(compile_one,spec['sources']))
 if core=='CPU0':objects.append(base/'src/prebuilt/camera_sensor.o')
 elf=out/(core+'.elf')
 # Keep linker response-file paths relative to the core directory.  The
 # workspace name is Japanese and LLVM's Windows response-file reader may
 # otherwise decode absolute UTF-8 paths with the active ANSI code page.
 # Keep all link products under Build/.  The previous root-level ELF was
 # outside the verification/manifest path and could leave stale evidence.
 link=[tool/'clang++.exe']+flags+['-o','Build/'+core+'.elf']+[p.relative_to(base).as_posix() for p in objects]+['-nostartfiles','-T','script/fsp.lld','-LBuild','-Wl,-Map=Build/'+core+'.map','-Wl,--gc-sections','-Wl,--start-group','-lcrt0','-lc++abi','-Wl,--end-group']
 if core=='CPU0':link+=['-Wl,--wrap=R_IIC_MASTER_SlaveAddressSet']
 rsp=out/'link.rsp';rsp.write_text('\n'.join('"'+str(x).replace('\\','/')+'"' for x in link[1:]))
 print(run([link[0],'@Build/link.rsp'],base),end='');print(run([tool/'llvm-size.exe','Build/'+core+'.elf'],base),end='')
 run([tool/'llvm-objcopy.exe','-O','srec','Build/'+core+'.elf','Build/'+core+'.srec'],base)
 metadata={
  'schema':'tron.build.profile.v1','core':core,'profile':args.profile,
  'physical_motor_output_enabled':args.profile=='vehicle-output',
  'profile_fingerprint':PROFILE_FINGERPRINT,
  'allow_physical_output':bool(args.allow_physical_output),
  'compiler':str(tool/'clang.exe'),'compiler_version':'21.1.1',
  'flags_sha256':hashlib.sha256((' '.join(flags)+' HOT_O2='+','.join(sorted(HOT_O2))).encode()).hexdigest().upper(),
  'source':'build.py','hardware_verified':False,
 }
 stamp.write_text(json.dumps(metadata,indent=2,sort_keys=True)+'\n',encoding='utf-8')
 (out/'build_metadata.json').write_text(json.dumps(metadata,indent=2,sort_keys=True)+'\n',encoding='utf-8')
 print(core,'OK',flush=True)
if args.core == 'all':
 logs=(ROOT/f'docs/{EVIDENCE_TAG}_cpu0.log',ROOT/f'docs/{EVIDENCE_TAG}_cpu1.log')
 if all(path.exists() for path in logs):
  subprocess.run([str(os.environ.get('PYTHON', 'python')), str(ROOT/'tools/manifest.py'), '--profile', args.profile], cwd=ROOT, check=True)
 else:
  print('BUILD COMPLETE; run tools/manifest.py after creating fresh CPU0/CPU1 evidence logs',flush=True)
