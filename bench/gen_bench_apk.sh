#!/usr/bin/env bash
# Generate a non-trivial benchmark APK: N codegen'd classes that call each other
# and load string constants, compiled with javac + d8 and packaged with a
# manifest. Output: tests/data/bench.apk (gitignored; regenerate as needed).
set -euo pipefail
N="${1:-400}"
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
OUT="${2:-$ROOT/tests/data/bench.apk}"
PLATFORM="$HOME/Android/Sdk/platforms/android-34/android.jar"
WORK="$(mktemp -d)"
trap 'rm -rf "$WORK"' EXIT

mkdir -p "$WORK/src/bench"
for i in $(seq 0 $((N-1))); do
  next=$(( (i + 1) % N ))
  cat > "$WORK/src/bench/C$i.java" <<EOF
package bench;
public class C$i {
    static final String TAG = "bench.C$i";
    private int state;
    public int compute(int x) {
        int acc = x;
        for (int k = 0; k < 8; k++) { acc = (acc * 31 + k) ^ state; }
        return acc + new C$next().helper(acc);
    }
    public int helper(int v) {
        if (v < 0) { return -v; }
        System.out.println(TAG + ":" + v);
        return v % 1000;
    }
    public void setState(int s) { this.state = s; }
}
EOF
done

cat > "$WORK/AndroidManifest.xml" <<EOF
<?xml version="1.0" encoding="utf-8"?>
<manifest xmlns:android="http://schemas.android.com/apk/res/android" package="com.example.bench">
    <uses-sdk android:minSdkVersion="21" android:targetSdkVersion="34" />
    <application android:label="bench"><activity android:name=".Main" android:exported="true"/></application>
</manifest>
EOF

mkdir -p "$WORK/classes"
find "$WORK/src" -name '*.java' > "$WORK/sources.txt"
javac --release 8 -d "$WORK/classes" @"$WORK/sources.txt" 2>/dev/null
( cd "$WORK/classes" && d8 $(find . -name '*.class') --min-api 21 --output "$WORK" >/dev/null 2>&1 )
aapt2 link -o "$WORK/base.apk" --manifest "$WORK/AndroidManifest.xml" -I "$PLATFORM" --min-sdk-version 21 >/dev/null 2>&1
python3 - "$WORK/base.apk" "$WORK/classes.dex" "$OUT" <<'PY'
import sys, zipfile
base, dex, out = sys.argv[1:4]
src = zipfile.ZipFile(base)
with zipfile.ZipFile(out, "w", zipfile.ZIP_DEFLATED) as z:
    for it in src.infolist():
        z.writestr(it, src.read(it.filename))
    z.write(dex, "classes.dex")
PY
echo "wrote $OUT ($N classes)"
