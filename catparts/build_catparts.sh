#!/usr/bin/env bash
set -e
DIR="/home/aju/Android/Cat_S22_Flip_Android15_Fixes/catparts"
BUILD_TOOLS="/home/aju/Android/Sdk/build-tools/35.0.0"
PLATFORM="/home/aju/Android/Sdk/platforms/android-35/android.jar"
KEY_PK8="/home/aju/Android/Cat/treble_app/keys/platform.pk8"
KEY_PEM="/home/aju/Android/Cat/treble_app/keys/platform.x509.pem"

rm -rf "$DIR/build"
mkdir -p "$DIR/build/gen" "$DIR/build/obj" "$DIR/build/dex"

"$BUILD_TOOLS/aapt2" compile --dir "$DIR/res" -o "$DIR/build/res.zip"
"$BUILD_TOOLS/aapt2" link \
  -I "$PLATFORM" \
  --manifest "$DIR/AndroidManifest.xml" \
  --java "$DIR/build/gen" \
  -o "$DIR/build/CatParts.unaligned.apk" \
  "$DIR/build/res.zip"

javac --release 11 \
  -cp "$PLATFORM" \
  -d "$DIR/build/obj" \
  "$DIR/build/gen/com/cat/parts/R.java" \
  "$DIR/src/com/cat/parts/CatPartsActivity.java" \
  "$DIR/src/com/cat/parts/BootReceiver.java" \
  "$DIR/src/com/cat/parts/ImsNetworkRequester.java"

"$BUILD_TOOLS/d8" \
  --lib "$PLATFORM" \
  --min-api 29 \
  --output "$DIR/build/dex" \
  "$DIR/build/obj/com/cat/parts/"*.class

(cd "$DIR/build/dex" && zip -q "$DIR/build/CatParts.unaligned.apk" classes.dex)
"$BUILD_TOOLS/zipalign" -f 4 "$DIR/build/CatParts.unaligned.apk" "$DIR/build/CatParts.aligned.apk"
"$BUILD_TOOLS/apksigner" sign \
  --key "$KEY_PK8" \
  --cert "$KEY_PEM" \
  --out "$DIR/CatParts.apk" \
  "$DIR/build/CatParts.aligned.apk"
ls -lh "$DIR/CatParts.apk"
