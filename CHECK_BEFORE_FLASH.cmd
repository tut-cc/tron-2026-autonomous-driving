@echo off
setlocal
pushd "%~dp0"
python -B tools\build.py --core all --profile vehicle-output --allow-physical-output
if errorlevel 1 goto fail
python -B tools\verify.py --profile vehicle-output
if errorlevel 1 goto fail
python -B tools\test_host.py
if errorlevel 1 goto fail
where node >nul 2>nul
if errorlevel 1 goto fail
node --test M85Web\Application\mini-4wd-webapp\tests\state-machine.test.mjs M85Web\Application\mini-4wd-webapp\tests\comm.test.mjs M85Web\Application\mini-4wd-webapp\tests\video.test.mjs
if errorlevel 1 goto fail
echo PASS: vehicle-output build and verification
popd
exit /b 0
:fail
echo FAIL: do not flash the board or connect motor power
popd
exit /b 1
