#!/bin/bash
# Builds the Linux release artifacts: a .deb and a portable tarball.
#
# Run from the repository root, or pass the source directory as $1.
set -e

SRC="${1:-$(cd "$(dirname "$0")/.." && pwd)}"
VERSION="${VERSION:-0.1.0}"
ARCH=amd64

BUILD="$SRC/build-linux"
OUT="$BUILD/release"
rm -rf "$OUT"
mkdir -p "$OUT"

# Two builds, because the two artifacts want different things. The .deb links
# dynamically so glibc security updates reach it; the portable tarball links
# statically so it runs on any distro regardless of glibc vintage.
echo "=== build (dynamic, for .deb) ==="
cmake -S "$SRC" -B "$BUILD" -G Ninja -DCMAKE_BUILD_TYPE=Release \
      -DSEICURE_STATIC=OFF > /dev/null
cmake --build "$BUILD" > /dev/null
"$BUILD/tests/seicure_tests" | tail -2

echo "=== build (static, for tarball) ==="
cmake -S "$SRC" -B "$BUILD-static" -G Ninja -DCMAKE_BUILD_TYPE=Release \
      -DSEICURE_STATIC=ON > /dev/null
cmake --build "$BUILD-static" > /dev/null
"$BUILD-static/tests/seicure_tests" | tail -2

BIN="$BUILD/src/cli/seicure"
BIN_STATIC="$BUILD-static/src/cli/seicure"

# What glibc does this binary actually demand? Building on a newer distro
# silently raises the floor, and a package that will not install on the user's
# system is worse than no package.
GLIBC=$(objdump -T "$BIN" 2>/dev/null | grep -o 'GLIBC_[0-9.]*' | sort -V | tail -1 | cut -d_ -f2)
GLIBCXX=$(objdump -T "$BIN" 2>/dev/null | grep -o 'GLIBCXX_[0-9.]*' | sort -V | tail -1 | cut -d_ -f2)
echo "=== runtime floor ==="
echo "  glibc   >= ${GLIBC:-unknown}"
echo "  libstdc++ >= ${GLIBCXX:-unknown}"

# --- .deb ------------------------------------------------------------------
PKGROOT="$BUILD/deb/seicure_${VERSION}_${ARCH}"
rm -rf "$PKGROOT"
mkdir -p "$PKGROOT/DEBIAN" "$PKGROOT/usr/bin" "$PKGROOT/usr/share/doc/seicure"

install -m 0755 "$BIN" "$PKGROOT/usr/bin/seicure"

cat > "$PKGROOT/DEBIAN/control" <<EOF
Package: seicure
Version: $VERSION
Section: utils
Priority: optional
Architecture: $ARCH
Depends: libc6 (>= ${GLIBC:-2.34}), libstdc++6 (>= 13)
Maintainer: SEIZIA <noreply@seizia.invalid>
Description: File encryption that hides names, sizes and timestamps
 Seicure encrypts files into containers that reveal nothing about their
 contents: the original name, size, timestamps and permissions are stored
 inside the ciphertext and restored on decryption. Container sizes are padded
 so the on-disk length only coarsely approximates the original.
 .
 Uses XChaCha20-Poly1305 with Argon2id key derivation.
EOF

cat > "$PKGROOT/usr/share/doc/seicure/copyright" <<'EOF'
Format: https://www.debian.org/doc/packaging-manuals/copyright-format/1.0/
Upstream-Name: seicure

Files: *
Copyright: SEIZIA
License: proprietary
 All rights reserved.

Files: third_party/libsodium/*
Copyright: 2013-2025 Frank Denis
License: ISC
EOF

gzip -9 -n -c "$SRC/README.md" > "$PKGROOT/usr/share/doc/seicure/README.md.gz" 2>/dev/null || true
chmod -R g-w "$PKGROOT"

dpkg-deb --root-owner-group --build "$PKGROOT" "$OUT/seicure_${VERSION}_${ARCH}.deb" > /dev/null
echo "=== deb ==="
dpkg-deb --info "$OUT/seicure_${VERSION}_${ARCH}.deb" | grep -E 'Package|Version|Depends|Installed-Size'

# --- portable tarball ------------------------------------------------------
TARDIR="$BUILD/tar/seicure-${VERSION}-linux-x86_64"
rm -rf "$TARDIR"; mkdir -p "$TARDIR"
install -m 0755 "$BIN_STATIC" "$TARDIR/seicure"
cat > "$TARDIR/README.txt" <<EOF
Seicure ${VERSION} — portable (Linux x86_64)

  ./seicure enc <file>...        encrypt
  ./seicure dec <container>...   decrypt

Statically linked: no runtime dependencies, any x86_64 Linux.

Encrypted files are named with random hex. The original name, size,
timestamps and permissions live inside the ciphertext and are restored
on decryption.

There is no password recovery. That is by design.
EOF
tar -C "$(dirname "$TARDIR")" -czf \
    "$OUT/seicure-${VERSION}-linux-x86_64.tar.gz" "$(basename "$TARDIR")"

# --- checksums -------------------------------------------------------------
( cd "$OUT" && sha256sum -- * > SHA256SUMS.txt )

echo "=== release ==="
ls -la "$OUT" | tail -n +2
