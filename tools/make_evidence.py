"""Regenerate the vehicle-output release evidence from a real clean build.

Run on the Windows build PC from the package root (the same Python that
e2 studio uses): e2 studio External Tool "demo_make_evidence", or
python -B tools/make_evidence.py [--resume].  This is the single pre-flash
check (the former MAKE_EVIDENCE.cmd / CHECK_BEFORE_FLASH.cmd were removed).

--resume skips step 2 only when both clean-build logs report EXIT_CODE=0 and
their ELF/stamp hashes still match the files in CPU0/Build and CPU1/Build.

Steps (stops at the first failure and leaves the failing log in docs/):
  1. move stray artifacts that verify.py rejects (J-Link logs, superseded
     evidence logs) to ../demo_evidence_archive/  (nothing is deleted)
  2. clean build CPU0 and CPU1 -> docs/<TAG>_cpu0.log / _cpu1.log
  3. manifest.py
  4. test_host.py + Web JS tests -> docs/<TAG>_host_tests.log
  5. verify.py -> docs/<TAG>_verify.log
Every log records the command, its exit code and its real output; ELF and
build-stamp hashes are computed from the files the build just produced.
The build does not touch the board or motor power.
"""
import sys
sys.dont_write_bytecode = True  # verify.py rejects __pycache__ in the delivery
import datetime
import hashlib
import json
import re
import shutil
import subprocess
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent  # tools/ -> demo root
TAG_FILE = ROOT / 'tools/evidence_tag.json'  # single source read by manifest.py, verify.py, build.py
_tag = json.loads(TAG_FILE.read_text(encoding='utf-8'))
TAG, TAG_DATE = _tag['tag'], _tag['date']
PROFILE = 'vehicle-output'
ARCHIVE = ROOT.parent / 'demo_evidence_archive'
STRAY_ALWAYS = ['CPU0/JLinkLog.log', 'CPU1/JLinkLog.log']  # e2 studio writes these while debugging
JS_TESTS = ['M85Web/Application/mini-4wd-webapp/tests/state-machine.test.mjs',
            'M85Web/Application/mini-4wd-webapp/tests/comm.test.mjs',
            'M85Web/Application/mini-4wd-webapp/tests/video.test.mjs']


def sha256(path):
    return hashlib.sha256(path.read_bytes()).hexdigest().upper()


def run(cmd):
    print('>', ' '.join(cmd), flush=True)
    proc = subprocess.run(cmd, cwd=ROOT, stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                          text=True, encoding='utf-8', errors='replace')
    print(proc.stdout, end='', flush=True)
    return proc.returncode, proc.stdout


def write_log(name, header, body):
    # BUILD_DATE is the date of the clean build these logs belong to (the tag);
    # RUN_DATE is when this particular command actually ran.
    path = ROOT / 'docs' / name
    path.write_text('\n'.join([f'BUILD_EVIDENCE: {TAG}', f'BUILD_DATE={TAG_DATE}',
                               f'RUN_DATE={datetime.date.today().isoformat()}',
                               f'PROFILE={PROFILE}', *header]) + '\n' + body,
                    encoding='utf-8', newline='\n')
    return path


def find_node():
    import os
    for cand in (os.environ.get('NODE'), shutil.which('node'),
                 r'C:\Program Files\nodejs\node.exe',
                 os.path.expandvars(r'%LOCALAPPDATA%\Programs\nodejs\node.exe')):
        if cand and Path(cand).is_file():
            return cand
    return None


def clean_log_still_valid(core):
    log = ROOT / 'docs' / f'{TAG}_{core.lower()}.log'
    if not log.exists():
        return False
    text = log.read_text(encoding='utf-8', errors='replace')
    build = ROOT / core / 'Build'
    return ('EXIT_CODE=0' in text and 'CLEAN=1' in text and
            f'ELF_SHA256={sha256(build / f"{core}.elf")}' in text and
            f'PROFILE_STAMP_SHA256={sha256(build / "build_profile.json")}' in text)


def stray_paths():
    """Paths verify.py rejects: its 'stale evidence' list plus the J-Link logs."""
    src = (ROOT / 'tools/verify.py').read_text(encoding='utf-8')
    m = re.search(r"for path in \((.*?)\):\s*\n\s*assert not \(R/path\)\.exists\(\)", src, re.S)
    listed = re.findall(r"'([^']+)'", m.group(1)) if m else []
    return STRAY_ALWAYS + [p for p in listed if p not in STRAY_ALWAYS]


def start_new_tag():
    """A clean build is evidence of today: move the old tag's logs aside and
    switch evidence_tag.json (and this run) to demo_autofix_<today>."""
    global TAG, TAG_DATE
    today = datetime.date.today()
    new_tag, new_date = f'demo_autofix_{today:%Y%m%d}', today.isoformat()
    if new_tag == TAG:
        return
    for old in sorted((ROOT / 'docs').glob(f'{TAG}_*.log')):
        shutil.move(str(old), str(ARCHIVE / f'docs_{old.name}'))
        print(f'archived {old.name} (previous evidence tag)', flush=True)
    TAG_FILE.write_text(json.dumps({'tag': new_tag, 'date': new_date}, indent=2) + '\n',
                        encoding='utf-8', newline='\n')
    print(f'evidence tag: {TAG} -> {new_tag}', flush=True)
    TAG, TAG_DATE = new_tag, new_date


def fail(message):
    print(f'FAIL: {message}\nFAIL: do not flash the board or connect motor power', flush=True)
    sys.exit(1)


def main():
    ARCHIVE.mkdir(exist_ok=True)
    for rel in stray_paths():
        src = ROOT / rel
        if src.exists():
            dst = ARCHIVE / rel.replace('/', '_')
            shutil.move(str(src), str(dst))
            print(f'archived {rel} -> {dst}', flush=True)

    py = sys.executable
    resume = '--resume' in sys.argv[1:]
    reuse = resume and all(clean_log_still_valid(core) for core in ('CPU0', 'CPU1'))
    if not reuse:
        start_new_tag()
    for core in ('CPU0', 'CPU1'):
        if reuse:
            print(f'{core}: clean-build log still matches the current ELF/stamp; not rebuilding', flush=True)
            continue
        cmd = [py, '-B', 'tools/build.py', '--core', core, '--clean',
               '--profile', PROFILE, '--allow-physical-output']
        code, out = run(cmd)
        header = [f'CORE={core}', 'CLEAN=1',
                  'COMMAND=python -B ' + ' '.join(cmd[2:]), f'EXIT_CODE={code}']
        body = out
        if code == 0:
            body += (f'ELF_SHA256={sha256(ROOT / core / "Build" / f"{core}.elf")}\n'
                     f'PROFILE_STAMP_SHA256={sha256(ROOT / core / "Build" / "build_profile.json")}\n')
        write_log(f'{TAG}_{core.lower()}.log', header, body)
        if code:
            fail(f'{core} clean build')

    code, _ = run([py, '-B', 'tools/manifest.py', '--profile', PROFILE])
    if code:
        fail('manifest.py')

    sys.path.insert(0, str(ROOT))
    from manifest import tree_hash  # noqa: E402  (bytecode disabled above)
    code, host_out = run([py, '-B', 'tools/test_host.py'])
    header = [f'CONTROL_TREE_SHA256={tree_hash(ROOT / "control")}',
              f'WEB_TREE_SHA256={tree_hash(ROOT / "M85Web/Application")}',
              'COMMAND=python -B tools/test_host.py', f'EXIT_CODE={code}']
    body = host_out
    js_code = 1
    if code == 0:
        node = find_node()
        if node is None:
            body += 'COMMAND=node --test (node.exe not found; set NODE=<path to node.exe>)\nEXIT_CODE=127\n'
        else:
            js_code, js_out = run([node, '--test', *JS_TESTS])
            counts = {k: re.search(rf'^# {k} (\d+)', js_out, re.M) for k in ('tests', 'pass', 'fail')}
            body += (f'COMMAND=node --test {" ".join(JS_TESTS)}\nEXIT_CODE={js_code}\n' +
                     ''.join(f'JS_{k.upper()}={m.group(1) if m else "?"}\n' for k, m in counts.items()))
    write_log(f'{TAG}_host_tests.log', header, body)
    if code:
        fail('host C tests')
    # verify.py does not depend on the JS tests, so still run it; the overall
    # result stays FAIL until the JS tests have passed on this PC.

    # verify.py cross-checks an existing verify log against MANIFEST.json, so a
    # log left by an earlier (failed) run must not be in place while it runs.
    old_verify = ROOT / 'docs' / f'{TAG}_verify.log'
    if old_verify.exists():
        stamp = datetime.datetime.now().strftime('%Y%m%d_%H%M%S')
        shutil.move(str(old_verify), str(ARCHIVE / f'docs_{TAG}_verify_{stamp}.log'))
    code, out = run([py, '-B', 'tools/verify.py', '--profile', PROFILE])
    write_log(f'{TAG}_verify.log',
              ['COMMAND=python -B tools/verify.py --profile vehicle-output', f'EXIT_CODE={code}'], out)
    if code:
        fail('verify.py')
    if js_code:
        fail('Web JS tests did not pass (node.exe missing or tests failed); '
             'install Node.js or set NODE=<path to node.exe>, then rerun with --resume')
    print('PASS: clean build, manifest, host/JS tests and verify; evidence logs in docs/', flush=True)


if __name__ == '__main__':
    main()
