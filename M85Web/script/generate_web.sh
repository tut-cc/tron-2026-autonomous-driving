#!/bin/bash
# Linux/macOS equivalent of generate_web.ps1: same inputs and substitutions; the
# file list is sorted so the output is byte-identical to the Windows (NTFS) run.
# Usage: M85Web/script/generate_web.sh   (from the package root, needs perl + python3)
set -e
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"; OUT="$ROOT/M85Web/Application/web/fsdata.h"
APP="$ROOT/M85Web/Application"; FE="$APP/mini-4wd-webapp"
ST=$(mktemp -d /tmp/webgen.XXXX); mkdir -p "$ST/fs"
cp "$FE/index.html" "$FE/style.css" "$ST/fs/"; cp -r "$FE/js" "$ST/fs/js"
[ -d "$FE/assets" ] && cp -r "$FE/assets" "$ST/fs/assets"
cp "$APP/web/404.html" "$ST/fs/404.html"
python3 - "$ROOT/CPU0/mtk3_bsp2/uct/lwip/src/lwip/src/apps/http/makefsdata/makefsdata" "$ST/makefsdata" <<'PY'
import sys
g=open(sys.argv[1],encoding='utf-8',newline='').read()
mime='} elsif($file =~ /\\.js$/) {\n print(HEADER "Content-type: application/javascript\\r\\n");\n} elsif($file =~ /\\.css$/) {\n print(HEADER "Content-type: text/css\\r\\n");\n} elsif($file =~ /\\.gif$/) {'
g=g.replace('} elsif($file =~ /\\.gif$/) {',mime).replace('/tmp/','../').replace('find . -type f |','find . -type f | LC_ALL=C sort |').replace('$fvar =~ s-\\.-_-g;','$fvar =~ s-\\.-_-g; $fvar =~ s/[^A-Za-z0-9_]/_/g;')
open(sys.argv[2],'w',encoding='utf-8',newline='').write(g.replace('\r\n','\n'))
PY
(cd "$ST" && perl makefsdata >/dev/null)
cp "$ST/fsdata.c" "$OUT"; rm -rf "$ST"
