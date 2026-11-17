#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")"
: "${ANDROID_JAR:?Caminho do SDK android.jar API 35}"
: "${ANDROID_BUILD_TOOLS:?Pasta do Build Tools 35}"
: "${EDDSA_JAR:?Arquivo net.i2p.crypto:eddsa:0.3.0}"
: "${SIGNING_KEYSTORE:?Keystore original da instalacao}"
: "${ARROZ_STORE_PASSWORD:?Senha da keystore no ambiente}"
: "${ARROZ_KEY_PASSWORD:?Senha da chave no ambiente}"
mkdir -p build/gen build/classes build/dex
"$ANDROID_BUILD_TOOLS/aapt2" compile --dir app/src/main/res -o build/resources.zip
"$ANDROID_BUILD_TOOLS/aapt2" link -I "$ANDROID_JAR" --manifest app/src/main/AndroidManifest.xml --java build/gen --custom-package com.rdzim.lucasmenu.keyauth --min-sdk-version 30 --target-sdk-version 35 -A app/src/main/assets -o build/base.apk build/resources.zip
find app/src/main/java build/gen -name '*.java' > build/sources.txt
if [ -n "${ECJ_JAR:-}" ]; then
 java -jar "$ECJ_JAR" -1.8 -proc:none -warn:none -classpath "$ANDROID_JAR:$EDDSA_JAR" -d build/classes @build/sources.txt
else
 javac -source 8 -target 8 -classpath "$ANDROID_JAR:$EDDSA_JAR" -d build/classes @build/sources.txt
fi
find build/classes -name '*.class' > build/classes.txt
java -cp "$ANDROID_BUILD_TOOLS/lib/d8.jar" com.android.tools.r8.D8 --min-api 30 --lib "$ANDROID_JAR" --output build/dex @build/classes.txt "$EDDSA_JAR"
python3 - <<'PY'
import shutil,zipfile
from pathlib import Path
shutil.copyfile('build/base.apk','build/unsigned.apk')
with zipfile.ZipFile('build/unsigned.apk','a',zipfile.ZIP_DEFLATED) as z:
 for f in Path('build/dex').glob('classes*.dex'):z.write(f,f.name)
 for f in Path('app/src/main/jniLibs').rglob('*.so'):z.write(f,'lib/'+f.parent.name+'/'+f.name)
PY
"$ANDROID_BUILD_TOOLS/zipalign" -f -p 4 build/unsigned.apk build/aligned.apk
java -jar "$ANDROID_BUILD_TOOLS/lib/apksigner.jar" sign --ks "$SIGNING_KEYSTORE" --ks-key-alias "${ARROZ_KEY_ALIAS:-androiddebugkey}" --ks-pass env:ARROZ_STORE_PASSWORD --key-pass env:ARROZ_KEY_PASSWORD --out build/Arroz-Menu-3.1.7-ESP-HS-Teste.apk build/aligned.apk
java -jar "$ANDROID_BUILD_TOOLS/lib/apksigner.jar" verify --verbose build/Arroz-Menu-3.1.7-ESP-HS-Teste.apk
