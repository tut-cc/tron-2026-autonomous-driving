"""Offline ELF/layout/source checks; does not claim physical-board validation."""
from pathlib import Path
import argparse,hashlib,re,struct,subprocess,os,json
import xml.etree.ElementTree as ET
R=Path(__file__).resolve().parent.parent  # tools/ -> demo root
EVIDENCE=json.loads((R/'tools/evidence_tag.json').read_text(encoding='utf-8'))
TAG,TAG_DATE=EVIDENCE['tag'],EVIDENCE['date']

parser=argparse.ArgumentParser(description='Verify offline RA8P1 artifacts for one explicit build profile')
parser.add_argument('--profile',choices=('dry-run','vehicle-output'),default='vehicle-output')
parser.add_argument('--verify-source-zips',action='store_true',
                    help='explicitly verify the two provenance ZIPs in ~/Downloads')
args=parser.parse_args()
expected_profile=args.profile

def sha256_file(path):
    digest=hashlib.sha256()
    with path.open('rb') as stream:
        for chunk in iter(lambda: stream.read(1024*1024), b''):
            digest.update(chunk)
    return digest.hexdigest().upper()

manifest=json.loads((R/'docs/MANIFEST.json').read_text(encoding='utf-8'))
assert manifest['schema']=='tron.integration.manifest.v1'
assert manifest['build_evidence_date']==TAG_DATE
assert manifest['profile']==expected_profile
assert manifest['physical_motor_output_enabled'] is (expected_profile=='vehicle-output')
assert manifest['hardware_verified'] is False

# Keep only this run's acceptance evidence in the delivery. Historical logs and
# local build/test work directories are easy to mistake for current evidence.
for path in ('build_temp','host_test_build','__pycache__',
             'CPU0/JLinkLog.log','CPU1/JLinkLog.log',
             'docs/build_log.txt','docs/host_tests.txt','docs/elf_checks.txt',
             'docs/vehicle_build_20260921_cpu0.log',
             'docs/vehicle_build_20260921_cpu1.log',
             'docs/vehicle_host_tests_20260921.log',
             'docs/vehicle_verify_20260921.log',
             'docs/vehicle_build_20260922_cpu0.log',
             'docs/vehicle_build_20260922_cpu1.log',
             'docs/vehicle_host_tests_20260922.log',
             'docs/vehicle_verify_20260922.log',
             'docs/vehicle_output_report_20260922.md',
             'docs/integration_report_20260921.md',
             'docs/integration_build_20260921_cpu0_attempt10.log',
             'docs/integration_build_20260921_cpu0_attempt11.log',
             'docs/integration_build_20260921_cpu0_attempt12.log',
             'docs/integration_build_20260921_cpu0_attempt13.log',
             'docs/integration_build_20260921_cpu0_attempt14.log',
             'docs/integration_build_20260921_cpu0_attempt15.log',
             'docs/integration_build_20260921_cpu0_attempt2.log',
             'docs/integration_build_20260921_cpu0_attempt3.log',
             'docs/integration_build_20260921_cpu0_attempt4.log',
             'docs/integration_build_20260921_cpu0_attempt5.log',
             'docs/integration_build_20260921_cpu0_attempt6.log',
             'docs/integration_build_20260921_cpu0_attempt7.log',
             'docs/integration_build_20260921_cpu0_attempt8.log',
             'docs/integration_build_20260921_cpu0_attempt9.log',
             'docs/integration_build_20260921_cpu1_attempt1.log',
             'docs/integration_host_tests_20260921.log',
             'docs/integration_verify_20260921.log',
             'docs/vehicle_output_report_20260921.md'):
    assert not (R/path).exists(), f'stale evidence or temp artifact remains: {path}'
assert not list(R.rglob('JLinkLog.log')), 'copied historical J-Link logs remain'
assert not list(R.rglob('__pycache__')), 'Python bytecode cache remains in delivery'
expected_logs={
    f'{TAG}_cpu0.log',
    f'{TAG}_cpu1.log',
    f'{TAG}_host_tests.log',
}
verify_log=R/f'docs/{TAG}_verify.log'
observed_logs={path.name for path in (R/'docs').glob('*.log')}
assert observed_logs in (expected_logs, expected_logs|{verify_log.name}), observed_logs

toolchain=manifest['toolchain']
assert re.search(r'21\.1\.1',toolchain['version'])
assert toolchain['same_compiler_for_both_cores'] is True
assert toolchain['cores']['CPU0']==toolchain['cores']['CPU1']
assert manifest['checks']['llvm_version']=='21.1.1'
assert manifest['checks']['llvm_compiler_same_for_both_cores'] is True
build_script=(R/'tools/build.py').read_text()
assert 'def find_toolchain()' in build_script and "os.environ.get('LLVM_ARM_BIN')" in build_script
assert 'ATfE-21.1.1-Windows-x86_64' in build_script
for core,record in manifest['clean_build_evidence'].items():
    log=R/record['path']
    assert log.exists()
    assert sha256_file(log)==record['sha256']
    text=log.read_text(errors='replace')
    assert f'{core} compiling' in text and f'{core} OK' in text
    assert f'BUILD_EVIDENCE: {TAG}' in text
    assert f'BUILD_DATE={TAG_DATE}' in text
    assert f'PROFILE={expected_profile}' in text
    assert f'CORE={core}' in text
    assert 'CLEAN=1' in text and 'EXIT_CODE=0' in text
    assert f"ELF_SHA256={manifest['artifacts'][core]['elf']['sha256']}" in text
    assert f"PROFILE_STAMP_SHA256={manifest['build_profiles'][core]['sha256']}" in text

profiles=manifest['build_profiles']
stamps=[]
for core in ('CPU0','CPU1'):
    record=profiles[core]
    stamp_path=R/record['path']
    assert stamp_path.exists()
    assert sha256_file(stamp_path)==record['sha256']
    stamp=record['metadata']
    assert stamp['schema']=='tron.build.profile.v1'
    assert stamp['core']==core
    assert stamp['profile']==expected_profile
    assert bool(stamp['physical_motor_output_enabled'])==(expected_profile=='vehicle-output')
    assert stamp['hardware_verified'] is False
    stamps.append(stamp)
assert len({stamp['profile_fingerprint'] for stamp in stamps})==1
assert manifest['checks']['build_profiles_match'] is True
assert manifest['checks']['build_profile_fingerprint']==stamps[0]['profile_fingerprint']

fsp=manifest['fsp']
assert manifest['checks']['fsp_version']=='6.5.0'
assert manifest['checks']['fsp_version_equal_across_cores'] is True
assert manifest['checks']['fsp_version_header_sha256_equal'] is True
assert fsp['CPU0']['version']==fsp['CPU1']['version']=='6.5.0'
assert fsp['CPU0']['sha256']==fsp['CPU1']['sha256']
for core,record in fsp.items():
    path=R/record['path']
    assert path.exists() and sha256_file(path)==record['sha256']

expected_inputs={
    'uct_mtk3bsp2_lwip_ra8p1_ek.zip':('7D258A80D752AFC0F887C317F86C54C2D8473D9F19827AE3E496CA6AA9A8B896', Path.home()/'Downloads'/'uct_mtk3bsp2_lwip_ra8p1_ek.zip'),
    'ra8p1_ov5640_dualcore.zip':('999F0266E8D45679F662901B90EC7EBCA4AD681F086BEA7AFA36CE40408A5754', Path.home()/'Downloads'/'ra8p1_ov5640_dualcore.zip'),
}
for name,(expected,path) in expected_inputs.items():
    record=manifest['inputs'][name]
    assert record['expected_sha256']==expected
    assert record.get('provenance_checked') is record['available']
    if record['available']:
        assert record['sha256']==expected and record['bytes']>0
    else:
        assert record['sha256'] is None and record['bytes'] is None
    if args.verify_source_zips:
        assert path.is_file(), path
        actual=sha256_file(path)
        assert actual==expected, (name,actual,expected)

for core in ('CPU0','CPU1'):
    for suffix in ('elf','srec','map'):
        record=manifest['artifacts'][core][suffix]
        path=R/record['path']
        assert path.exists(), path
        assert path.stat().st_size==record['bytes']
        assert sha256_file(path)==record['sha256']

map0=(R/'CPU0/Build/CPU0.map').read_text(errors='replace')
map1=(R/'CPU1/Build/CPU1.map').read_text(errors='replace')
assert 'FLASH_START = 0x02000000' in map0 and 'FLASH_LENGTH = 0x000f0000' in map0
assert 'FLASH_START = 0x020f0000' in map1 and 'FLASH_LENGTH = 0x00010000' in map1
assert '221d3000 221d3000     1000' in map0 and '221d3000 221d3000     1000' in map1
def map_symbol_address(text,symbol):
    match=re.search(rf'^\s*([0-9a-fA-F]+)\s+\1\s+0\s+\d+\s+{re.escape(symbol)} = \.$',text,re.MULTILINE)
    assert match is not None, symbol
    return int(match.group(1),16)
cpu0_flash_end=map_symbol_address(map0,'__ddsc_FLASH_END')
cpu1_flash_end=map_symbol_address(map1,'__ddsc_FLASH_END')
assert 0x02000000 < cpu0_flash_end < 0x020f0000
assert 0x020f0000 < cpu1_flash_end <= 0x02100000
regions0=(R/'CPU0/Build/memory_regions.lld').read_text()
regions1=(R/'CPU1/Build/memory_regions.lld').read_text()
assert 'FLASH_START = 0x02000000' in regions0 and 'FLASH_LENGTH = 0x000f0000' in regions0
assert 'FLASH_START = 0x020f0000' in regions1 and 'FLASH_LENGTH = 0x00010000' in regions1
for core in ('CPU0','CPU1'):
    assert 'BSP_PARTITION_FLASH_CPU1_S_START (0x020f0000U)' in (R/core/'Build/bsp_linker_info.h').read_text()
solution=(R/'Solution/solution.xml').read_text()
assert 'name="FLASH_CPU0_S" offset="0x0" parent="FLASH" security="s" size="0xf0000"' in solution
assert 'name="FLASH_CPU1_S" offset="0xf0000" parent="FLASH" security="s" size="0x10000"' in solution
solution_project=(R/'Solution/.project').read_text()
assert 'python' in solution_project
assert '../tools/build.py --core all --profile vehicle-output --allow-physical-output' in solution_project
for core in ('CPU0','CPU1'):
    project=(R/core/'.project').read_text()
    build_core='all' if core=='CPU0' else core
    assert f'../tools/build.py --core {build_core} --profile vehicle-output --allow-physical-output' in project
active_launches=list(R.rglob('*.launch'))
assert [p.relative_to(R).as_posix() for p in active_launches]==['CPU0/ra8p1_vision_BothCore_Download.launch']
both=active_launches[0].read_text(encoding='utf-8')
assert '${workspace_loc:/ra8p1_vision_CPU0}/Build/CPU0.elf' in both
assert '${workspace_loc:/ra8p1_vision_CPU1}/Build/CPU1.elf' in both
assert 'org.eclipse.cdt.launch.ATTR_BUILD_BEFORE_LAUNCH_ATTR" value="1"' in both
assert 'FLASH_START = 0x020f0000' in map1
assert 'R7KA8P1KF_CPU0' in both
assert 'com.renesas.cdt.core.runCommands\" value=\"\"' in both
assert 'org.eclipse.cdt.debug.gdbjtag.core.runCommands\" value=\"\"' in both
assert both.count('setResume" value="false"')==2
assert 'setResume" value="true"' not in both
assert 'core.initCommands" value=""' in both
assert 'gdbjtag.core.initCommands" value=""' in both
assert 'setTZBoundaries" value="false"' in both
assert 'eraseDataRomOnDownload" value="false"' in both
assert 'eraseRomOnDownload" value="false"' in both
assert not re.search(r'[A-Za-z]:\\Users\\',both)
assert not re.search(r'set\s*\{[^}]+\}\s*0x4000f0(?:44|54|64)',both,re.IGNORECASE)
assert active_launches[0].with_suffix('.jlink').is_file()
assert not (R/'CPU1/Build/gdbcmds.txt').exists()
for path in R.rglob('*'):
    if path.is_file() and path.suffix.lower() in ('.launch','.gdb','.cmd','.txt') and path.stat().st_size < 2_000_000:
        text=path.read_text(errors='replace')
        assert not re.search(r'set\s*\{[^}]+\}\s*0x4000f0(?:44|54|64)',text,re.IGNORECASE),path

# Verify the CDT metadata that the e² studio audit imported and built.
for project_name, core, config_id in (
    ('CPU0','all','vehicle.cpu0.source'),
    ('CPU1','CPU1','vehicle.cpu1.source'),
    ('Solution','all','vehicle.solution.source'),
):
    project_path=R/project_name/'.project'
    cproject_path=R/project_name/'.cproject'
    project_text=project_path.read_text(encoding='utf-8')
    cproject_text=cproject_path.read_text(encoding='utf-8')
    assert not re.search(r'[A-Za-z]:\\Users\\',project_text+cproject_text), project_name
    project_root=ET.fromstring(project_text)
    natures={node.text for node in project_root.findall('./natures/nature')}
    assert {'org.eclipse.cdt.core.cnature','org.eclipse.cdt.core.ccnature',
            'org.eclipse.cdt.make.core.makeNature'} <= natures, project_name
    build_commands=project_root.findall('./buildSpec/buildCommand')
    assert len(build_commands)==1 and build_commands[0].findtext('name')=='org.eclipse.cdt.make.core.makeBuilder'
    values={item.findtext('key'):item.findtext('value') for item in
            build_commands[0].findall('./arguments/dictionary')}
    assert values.get('org.eclipse.cdt.make.core.build.command')=='python', project_name
    assert values.get('org.eclipse.cdt.make.core.build.arguments')==(
        f'../tools/build.py --core {core} --profile vehicle-output --allow-physical-output'), project_name
    cproject_root=ET.fromstring(cproject_text)
    assert cproject_root.get('storage_type_id')=='org.eclipse.cdt.core.XmlProjectDescriptionStorage', project_name
    configs=cproject_root.findall('./storageModule/cconfiguration')
    assert len(configs)==1 and configs[0].get('id')==config_id, project_name
    source_build=configs[0].find("./storageModule[@name='Source Build']")
    assert source_build is not None and source_build.get('id')==config_id, project_name
    assert source_build.get('name')=='Source Build', project_name
    assert source_build.get('buildSystemId')=='org.eclipse.cdt.core.defaultConfigDataProvider', project_name

e2_audit=(R/'docs/E2STUDIO_BUILD_AUDIT.md').read_text(encoding='utf-8')
for marker in ('E2_BUILD_BEGIN project=ra8p1_vision_Solution',
               'E2_BUILD_END project=ra8p1_vision_Solution','exit=0',
               'EASE headless','Generate Project Content'):
    assert marker in e2_audit, marker
for artifact in ('CPU0/Build/CPU0.elf','CPU1/Build/CPU1.elf',
                 'CPU0/Build/CPU0.map','CPU1/Build/CPU1.map'):
    assert artifact in e2_audit, artifact
assert len(re.findall(r'`[0-9A-F]{64}`', e2_audit)) >= 4
for name,record in manifest['source_config_hashes'].items():
    path=R/name
    assert path.exists(), path
    if 'sha256' in record:
        assert path.stat().st_size==record['bytes']
        assert sha256_file(path)==record['sha256'], name
    else:
        digest=hashlib.sha256()
        for child in sorted(p for p in path.rglob('*') if p.is_file()):
            data=child.read_bytes()
            digest.update(child.relative_to(path).as_posix().encode()+b'\0'+str(len(data)).encode()+b'\0'+data)
        assert digest.hexdigest().upper()==record['tree_sha256'], name

memory=manifest['memory']
assert int(memory['CPU0_flash']['start'],16)==0x02000000
assert int(memory['CPU0_flash']['end_exclusive'],16)==0x020f0000
assert int(memory['CPU1_flash']['start'],16)==0x020f0000
assert int(memory['CPU1_flash']['end_exclusive'],16)==0x02100000
assert int(memory['CPU0_flash']['end_exclusive'],16)==int(memory['CPU1_flash']['start'],16)
assert int(memory['dualcore_shared']['start'],16)==0x221d3000
assert memory['dualcore_shared']['bytes']==0x1000
def elf(path):
 b=path.read_bytes();assert b[:6]==b'\x7fELF\x01\x01'
 h=struct.unpack_from('<HHIIIIIHHHHHH',b,16);assert h[1]==40
 ph=[struct.unpack_from('<IIIIIIII',b,h[4]+i*h[8]) for i in range(h[9])]
 sh=[struct.unpack_from('<IIIIIIIIII',b,h[5]+i*h[10]) for i in range(h[11])]
 names=sh[h[12]];strings=b[names[4]:names[4]+names[5]]
 def name(i):return strings[i:].split(b'\0',1)[0].decode()
 sections={name(x[0]):x for x in sh}
 syms={}
 for x in sh:
  if x[1]!=2:continue
  st=sh[x[6]];ss=b[st[4]:st[4]+st[5]]
  for off in range(x[4],x[4]+x[5],x[9]):
   n,v,size,info,other,idx=struct.unpack_from('<IIIBBH',b,off)
   syms[ss[n:].split(b'\0',1)[0].decode()]=(v,size,idx)
 return b,ph,sections,syms
result={}
for c,flash,flash_end,ram,end in [('CPU0',0x02000000,0x020f0000,0x22000000,0x22180000),('CPU1',0x020f0000,0x02100000,0x22180000,0x221d3000)]:
 b,ph,sec,sym=elf(R/c/'Build'/f'{c}.elf')
 shared=sec['.dualcore_shared'];assert shared[1]==8 and shared[3]==0x221d3000 and shared[5]==4096
 vector=sec['__flash_vectors$$'];assert vector[3]==flash
 sp,reset=struct.unpack_from('<II',b,vector[4]);assert ram<=sp<=end and reset&1
 assert sym['Reset_Handler'][0]==reset
 flash_load_end=flash
 for typ,off,vma,lma,fs,ms,flags,align in ph:
  if typ!=1 or ms==0:continue
  if 0x22000000<=vma<0x22200000 and vma!=0x221d3000:assert ram<=vma and vma+ms<=end
  if 0x02000000<=lma<0x02100000:
   assert flash<=lma and lma+fs<=flash_end
   flash_load_end=max(flash_load_end,lma+fs)
 map_flash_end=cpu0_flash_end if c=='CPU0' else cpu1_flash_end
 assert flash_load_end>flash and flash_load_end<=map_flash_end,(c,hex(flash_load_end),hex(map_flash_end))
 for s in ['knl_dispatch_entry','knl_systim_inthdr','knl_svcall_handler','knl_start_mtkernel']:
  assert s in sym and sym[s][2]!=0 and sym[s][0]!=0,(c,s)
 assert sym['knl_exctbl'][0]%512==0 and sym['knl_exctbl'][1]==(448*4 if c=='CPU0' else 112*4),c
 if c=='CPU1':
  for s in ['control_runtime_start','bus_tof_poll']:
   assert s in sym and sym[s][2]!=0,s
  assert sym['knl_system_mem'][1]==128*1024
  assert 'mipi_csi_ep_entry' not in sym
 else:
  assert 'mipi_csi_ep_entry' in sym and 'producer_send_ai' in sym
  assert 'control_runtime_start' not in sym and 'control_hw_apply' not in sym
  assert 'R_IIC_MASTER_Open' not in sym and '__wrap_R_IIC_MASTER_SlaveAddressSet' in sym
 result[c]={'initial_sp':hex(sp),'reset':hex(reset),'elf_bytes':len(b)}
source=(R/'CPU0/src/mipi_csi.c').read_text()
assert 'g_capture_timestamp_ms = autonomy_controller_now_ms()' in source
stages=['frame_rotate_180_rgb565(', 'obstacle_detector_run_rgb565(', 'road_navigation_analyze_rgb565(', 'autonomy_controller_update(']
positions=[source.index(stage) for stage in stages];assert positions==sorted(positions)
assert 'tof4m_service_poll' not in (R/'CPU0/src/autonomy_controller.c').read_text()
for core in ('CPU0','CPU1'):
    interrupt_source=(R/core/'mtk3_bsp2/sysdepend/ra_fsp/cpu/core/armv8m/interrupt.c').read_text()
    assert re.search(r'knl_exctbl\s*\[\s*15\s*\]\s*=\s*\(UW\)\s*knl_systim_inthdr',interrupt_source),core
application_sources=[R/'control/ports/dualcore_board.c']
for root in (R/'CPU0/src',R/'M85Web/Application'):
    application_sources.extend(p for p in root.rglob('*') if p.is_file() and p.suffix in ('.c','.h','.S','.s'))
for path in application_sources:
    text=path.read_text(errors='replace')
    assert not re.search(r'\bSysTick_(?:Handler|Config)\s*\(',text),path
profile=(R/'control/include/motor_build_profile.h').read_text()
assert re.search(r'#define\s+MOTOR_PHYSICAL_OUTPUT_ENABLE\s+0',profile)
assert re.search(r'#define\s+MOTOR_LEFT_INVERTED\s+0',profile)
assert re.search(r'#define\s+MOTOR_RIGHT_INVERTED\s+0',profile)
assert re.search(r'#define\s+MOTOR_DUTY_CAP_PERCENT\s+60U',profile)
motor_define=1 if expected_profile=='vehicle-output' else 0
assert f'-DMOTOR_PHYSICAL_OUTPUT_ENABLE={motor_define}' in (R/'CPU1/Build/link.rsp').read_text()
assert manifest['checks']['motor_physical_output_define_in_cpu1_link']==motor_define
assert 'MOTOR_PHYSICAL_OUTPUT_ENABLE' in (R/'control/ports/control_hw_ra8p1.c').read_text()
hardware_source=(R/'control/ports/control_hw_ra8p1.c').read_text()
assert '.duty_limit = MOTOR_DUTY_LIMIT' in hardware_source
assert '.invert_left = MOTOR_LEFT_INVERTED' in hardware_source
assert '.invert_right = MOTOR_RIGHT_INVERTED' in hardware_source
assert '.swap_sides = MOTOR_SWAP_SIDES' in hardware_source
assert re.search(r'#define\s+MOTOR_SWAP_SIDES\s+[01]',profile)
host_log=(R/f'docs/{TAG}_host_tests.log').read_text(errors='replace')
assert f'BUILD_EVIDENCE: {TAG}' in host_log
assert f'BUILD_DATE={TAG_DATE}' in host_log
assert f"CONTROL_TREE_SHA256={manifest['source_config_hashes']['control']['tree_sha256']}" in host_log
assert f"WEB_TREE_SHA256={manifest['source_config_hashes']['M85Web/Application']['tree_sha256']}" in host_log
assert 'inversion paths' in host_log and 'brake/coast' in host_log and 'no-reverse' in host_log

frontend=R/'M85Web/Application/mini-4wd-webapp'
html=(frontend/'index.html').read_text(encoding='utf-8')
input_js=(frontend/'js/input.js').read_text(encoding='utf-8')
browser_js='\n'.join(p.read_text(encoding='utf-8') for p in (frontend/'js').glob('*.js'))
style=(frontend/'style.css').read_text(encoding='utf-8')
# The onboard BMP snapshot is polled by js/video.js (2026-09-27).
assert '/video_feed' in html+browser_js and 'js/video.js' in html
assert 'data-dir="down"' not in html and 'id="dpad-down"' not in html
assert 'WASD' not in html and 'ArrowDown' not in input_js and 'KeyS' not in input_js
assert '.dpad-down' not in style
assert re.search(r"\['up',\s*'left',\s*'right'\]",input_js)
assert re.search(r'this\.throttle\s*=\s*up\s*\?\s*1\.0\s*:\s*0\.0',input_js)
assert not re.search(r'this\.throttle\s*=\s*[^;\r\n]*-\s*1(?:\.0)?',input_js)

fsdata=(R/'M85Web/Application/web/fsdata.h').read_text(encoding='ascii',errors='replace')
embedded=bytes(int(value,16) for value in re.findall(r'0x([0-9a-fA-F]{1,2})',fsdata))
assert b'/video_feed' in embedded
for forbidden in (b'data-dir="down"',b'id="dpad-down"',b'.dpad-down',b'ArrowDown',b'KeyS',b'WASD'):
    assert forbidden not in embedded, forbidden
embedded_text=embedded.decode('utf-8',errors='replace').replace('\r\n','\n').replace('\r','\n')
source_assets=[frontend/'index.html',frontend/'style.css']
source_assets += sorted(p for p in (frontend/'js').rglob('*') if p.is_file())
assets=frontend/'assets'
if assets.exists(): source_assets += sorted(p for p in assets.rglob('*') if p.is_file())
for asset in source_assets:
    source_text=asset.read_text(encoding='utf-8')
    assert source_text in embedded_text, f'asset missing or stale in fsdata.h: {asset}'
api_doc=(frontend/'docs/protocol.md').read_text(encoding='utf-8')
http_doc=(R/'M85Web/Application/README_HTTP.md').read_text(encoding='utf-8')
api_source=(R/'M85Web/Application/http/control_api.c').read_text(encoding='utf-8')
assert 'POST /api/control' in api_doc and 'POST /api/command' not in api_doc
frame_stream_source=(R/'CPU0/src/frame_stream.c').read_text()
assert 'video_feed' in http_doc and 'image/bmp' in frame_stream_source
assert 'frame_stream_http_snapshot_acquire' in api_source
assert 'frame_stream_http_publish' in frame_stream_source
assert '"/api/control"' in api_source
# The WebApp loopback test task was removed (2026-09-27); it must not return.
assert not (R/'M85Web/Application/control_test_task.c').exists()
http_main=(R/'M85Web/Application/app_main_httpd.c').read_text()
assert 'control_test_task' not in http_main
assert '.itskpri = 12' in http_main and '.itskpri = 20' in http_main
assert re.search(r'ctsk_usb_stream\s*=\s*\{[^}]*\.itskpri\s*=\s*22\s*,[^}]*\.task\s*=\s*task_usb_stream', http_main, re.S)
assert 'frame_stream_video_task_poll()' in http_main
assert 'frame_stream_usb_task_poll()' in http_main
assert manifest['checks']['usb_stream_task_priority'] == 22
assert 'camera_task_delay' in source and 'tk_dly_tsk' in source
assert source.count('camera_task_delay(')>=5
assert 'frame_id == last_processed_frame' in source
assert 'control_if_request_is_urgent' in (R/'M85Web/Application/interface/controller_if.c').read_text()
assert 'control_if_request_is_urgent' in (R/'CPU0/src/m85_gateway_task.c').read_text()
assert 'reset_abort_request' in (R/'control/tests/test_web_adapter.c').read_text()
assert re.search(r'client_mode\s*=\s*2U?', (R/'control/tests/test_web_adapter.c').read_text())
assert '.channel = 1' in (R/'CPU1/ra_gen/hal_data.c').read_text()
cpu0_pins=(R/'CPU0/ra_gen/pin_data.c').read_text()
cpu1_pins=(R/'CPU1/ra_gen/pin_data.c').read_text()
assert 'IOPORT_PERIPHERAL_IIC' not in cpu0_pins
assert 'IOPORT_PERIPHERAL_IIC' in cpu1_pins
for pin in ('BSP_IO_PORT_00_PIN_06','BSP_IO_PORT_04_PIN_02','BSP_IO_PORT_04_PIN_12','BSP_IO_PORT_04_PIN_13'):
    assert pin in cpu1_pins and pin not in cpu0_pins, pin
for pin in ('BSP_IO_PORT_05_PIN_11','BSP_IO_PORT_05_PIN_12'):
    assert pin in cpu1_pins and pin not in cpu0_pins, pin
if verify_log.exists():
    evidence=verify_log.read_text(encoding='utf-8',errors='replace')
    assert f'BUILD_EVIDENCE: {TAG}' in evidence
    assert f'BUILD_DATE={TAG_DATE}' in evidence
    assert 'PASS: manifest/input/artifact hashes' in evidence
    assert 'MANIFEST_SHA256 '+sha256_file(R/'docs/MANIFEST.json') in evidence
print(f'PASS: manifest/input/artifact hashes, ELF vector bases, SRAM/flash separation, shared NOLOAD RAM, kernel symbols, scheduler yield, urgent STOP retention, pin ownership, UI/fsdata source match (BMP /video_feed, no reverse UI path), MOTOR_PHYSICAL_OUTPUT_ENABLE={motor_define}, loopback test task absent')
print('MANIFEST_SHA256',sha256_file(R/'docs/MANIFEST.json'))
print(json.dumps(result,indent=2))
