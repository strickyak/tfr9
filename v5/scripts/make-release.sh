#!/bin/sh
# make-release.sh -- populate build/release/ and create build/tfr911h-release.zip (POSIX sh).
#
# Expects `make tfr911h tethers jamesbrown-dsk gen-ports` to have built:
#   - build/tfr911h.uf2
#   - build/tether.{linux-amd64,linux-386,linux-arm-7,linux-arm64,win-amd64,win-386,mac-arm64,mac-amd64}.exe
#   - build/tfr911-jamesbrown.dsk
#   - build/generated/io-map.md
set -eu

V5=$(cd "$(dirname "$0")/.." && pwd)
. "$V5/scripts/env.sh"

REL="$V5_BUILD/release"
ZIP_OUT="$V5_BUILD/tfr911h-release.zip"

FLAT_BOOT=${V5_IMG_FLAT_BOOT:-$SHELF/nitros9/recipes/flat65280/flat65280.img}
FLAT_DISK=${V5_IMG_FLAT_DISK:-$SHELF/nitros9/recipes/flat65280/flat0.dsk}
DEEP_BOOT=${V5_IMG_DEEP_BOOT:-$SHELF/nitros9/recipes/deep65280/deep65280.img}
DEEP_DISK=${V5_IMG_DEEP_DISK:-$SHELF/nitros9/recipes/deep65280/deep0.dsk}
JB_DISK=${V5_IMG_JAMESBROWN_DISK:-$V5_BUILD/tfr911-jamesbrown.dsk}
COCO2_DISK=${V5_IMG_COCO2_DISK:-$V5_BUILD/coco2_basic.dsk}
HATVAN_DIR=${V5_IMG_HATVAN_DIR:-$HOME/github.com/strickyak/hatvan-os/build}
FUZIX_BIN=${V5_IMG_FUZIX_BIN:-$HOME/nand/github.com/EtchedPixels/FUZIX/Kernel/fuzix.bin}
FUZIX_DISK=${V5_IMG_FUZIX_DISK:-$HOME/nand/github.com/EtchedPixels/FUZIX/Images/multicomp09/disk.img}

TETHER_BINS="
tether.linux-amd64.exe
tether.linux-386.exe
tether.linux-arm-7.exe
tether.linux-arm64.exe
tether.win-amd64.exe
tether.win-386.exe
tether.mac-arm64.exe
tether.mac-amd64.exe
"

need_file() {
  if [ ! -f "$1" ]; then
    echo "make-release: missing required file: $1 ($2)" >&2
    exit 1
  fi
}

need_file "$V5/doc/release-tfr911h-README.md" "release README"
need_file "$V5/doc/jamesbrown-fujinet-guide.md" "jamesbrown FujiNet guide"
need_file "$V5_BUILD/generated/io-map.md" "run: make gen-ports"
need_file "$V5_BUILD/tfr911h.uf2" "run: make tfr911h"
for tb in $TETHER_BINS; do
  need_file "$V5_BUILD/$tb" "run: make tethers"
done
need_file "$FLAT_BOOT" "set V5_IMG_FLAT_BOOT"
need_file "$FLAT_DISK" "set V5_IMG_FLAT_DISK"
need_file "$DEEP_BOOT" "set V5_IMG_DEEP_BOOT"
need_file "$DEEP_DISK" "set V5_IMG_DEEP_DISK"
need_file "$JB_DISK" "run: make jamesbrown-dsk"
need_file "$COCO2_DISK" "run: make coco2-basic-dsk"
need_file "$HATVAN_DIR/kernel_6809.decb" "set V5_IMG_HATVAN_DIR"
need_file "$HATVAN_DIR/rbf_6809.decb" "set V5_IMG_HATVAN_DIR"
need_file "$HATVAN_DIR/procfs_6809.decb" "set V5_IMG_HATVAN_DIR"
need_file "$HATVAN_DIR/disk0.dsk" "set V5_IMG_HATVAN_DIR"
need_file "$FUZIX_BIN" "set V5_IMG_FUZIX_BIN"
need_file "$FUZIX_DISK" "set V5_IMG_FUZIX_DISK"

if ! command -v zip >/dev/null 2>&1; then
  echo "make-release: 'zip' command not found in PATH" >&2
  exit 1
fi

rm -rf "$REL" "$ZIP_OUT" "$V5_BUILD/tfr911h-release"
mkdir -p \
  "$REL/bin" \
  "$REL/scripts" \
  "$REL/images/flat65280" \
  "$REL/images/deep65280" \
  "$REL/images/jamesbrown" \
  "$REL/images/hatvan" \
  "$REL/images/fuzix" \
  "$REL/images/coco2_basic"

# Documentation & firmware
cp "$V5/doc/release-tfr911h-README.md" "$REL/README.md"
cp "$V5/doc/jamesbrown-fujinet-guide.md" "$REL/jamesbrown-fujinet-guide.md"
cp "$V5_BUILD/generated/io-map.md" "$REL/io-map.md"
cp "$V5_BUILD/tfr911h.uf2" "$REL/tfr911h.uf2"
chmod 0644 "$REL/README.md" "$REL/jamesbrown-fujinet-guide.md" "$REL/io-map.md" "$REL/tfr911h.uf2"

# Top-level helpers & all 8 run-tfr911h-*.sh launchers
for shfile in \
  tether.sh \
  tether-tfr911h.sh \
  cold-install-tfr911h.sh \
  upgrade-tfr911h.sh \
  run-tfr911h-turbo9os.sh \
  run-tfr911h-flat65280-nitros9-l1.sh \
  run-tfr911h-deep65280-nitros9-l2.sh \
  run-tfr911h-jamesbrown.sh \
  run-tfr911h-gep9-hatvan.sh \
  run-tfr911h-fuzix.sh \
  run-tfr911h-coco2-basic.sh \
  run-tfr911h-turbolab.sh
do
  cp "$V5/$shfile" "$REL/$shfile"
  chmod 0755 "$REL/$shfile"
done

# Support scripts
for sc in \
  find-tether.sh \
  find-board.sh \
  run-tfr911h-demo.sh \
  diagnose_fujinet.sh
do
  cp "$V5/scripts/$sc" "$REL/scripts/$sc"
  chmod 0755 "$REL/scripts/$sc"
done

# 8 static cross-platform tether binaries
for tb in $TETHER_BINS; do
  cp "$V5_BUILD/$tb" "$REL/bin/$tb"
  chmod 0755 "$REL/bin/$tb"
done

# Guest OS images
cp "$FLAT_BOOT" "$REL/images/flat65280/flat65280.img"
cp "$FLAT_DISK" "$REL/images/flat65280/flat0.dsk"
cp "$DEEP_BOOT" "$REL/images/deep65280/deep65280.img"
cp "$DEEP_DISK" "$REL/images/deep65280/deep0.dsk"
cp "$JB_DISK"   "$REL/images/jamesbrown/tfr911-jamesbrown.dsk"
cp "$HATVAN_DIR/kernel_6809.decb" "$REL/images/hatvan/kernel_6809.decb"
cp "$HATVAN_DIR/rbf_6809.decb"    "$REL/images/hatvan/rbf_6809.decb"
cp "$HATVAN_DIR/procfs_6809.decb" "$REL/images/hatvan/procfs_6809.decb"
cp "$HATVAN_DIR/disk0.dsk"        "$REL/images/hatvan/disk0.dsk"
cp "$FUZIX_BIN"  "$REL/images/fuzix/fuzix.bin"
cp "$FUZIX_DISK" "$REL/images/fuzix/disk.img"
cp "$COCO2_DISK" "$REL/images/coco2_basic/f0.dsk"
find "$REL/images" -type f -exec chmod 0644 {} +

# Create build/tfr911h-release.zip with top-level directory `tfr911h-release/`
# (without `-y`, `zip` follows the top-level symlink and stores regular files under `tfr911h-release/`).
ln -s release "$V5_BUILD/tfr911h-release"
(
  cd "$V5_BUILD"
  zip -r -q tfr911h-release.zip tfr911h-release
)
rm -f "$V5_BUILD/tfr911h-release"

echo "staged: $REL"
ls -lh "$ZIP_OUT"
