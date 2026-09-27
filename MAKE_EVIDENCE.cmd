@echo off
rem Clean build of both cores + manifest + host/JS tests + verify.
rem Evidence goes to docs\ (logs, MANIFEST.json).  Options: --resume (reuse the build).
setlocal
pushd "%~dp0"
python -B tools\make_evidence.py %*
set RC=%ERRORLEVEL%
popd
exit /b %RC%
