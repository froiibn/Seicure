#!/bin/bash
# Builds the macOS release artifacts: a .dmg holding the app, and a universal
# CLI tarball.
#
# Verified on macOS 26.5 / Apple Silicon with Apple clang 21: both slices build,
# both declare a 11.0 minimum, and the full test suite passes on the arm64 one.
# The x86_64 slice has been built but never executed -- running it needs Rosetta
# 2, which was not installed on the test machine.
#
# Signing and notarisation are described at the bottom rather than performed --
# both need an Apple Developer certificate.
set -e

SRC="${1:-$(cd "$(dirname "$0")/.." && pwd)}"
VERSION="${VERSION:-0.1.0}"

# --- signing ---------------------------------------------------------------
# Optional. With no Developer ID certificate in the keychain the build still
# succeeds and simply ships unsigned, which is what unattended builds do.
#
# Signing needs an unlocked login keychain, so this has to run from a Terminal
# session on the Mac itself -- over ssh the keychain stays locked and codesign
# cannot reach the private key.
TEAM_ID="${TEAM_ID:-F3Z4RU59X6}"
# The notary credentials live in the keychain under this profile name, put there
# once with:
#   xcrun notarytool store-credentials seicure --apple-id <id> --team-id <team>
# The app-specific password is therefore never in this script, nor in a shell
# history.
NOTARY_PROFILE="${NOTARY_PROFILE:-seicure}"

SIGN_ID="${SIGN_ID:-$(security find-identity -v -p codesigning 2>/dev/null \
    | grep 'Developer ID Application' | head -1 \
    | sed 's/.*"\(.*\)".*/\1/')}"

echo "=== signing identity ==="
if [ -n "$SIGN_ID" ]; then
  echo "  $SIGN_ID"
else
  echo "  none found -- building UNSIGNED"
  echo "  (Gatekeeper will refuse this build on other machines)"
fi

BUILD="$SRC/build-macos"
OUT="$BUILD/release"
rm -rf "$OUT"; mkdir -p "$OUT"

# 11.0 is the first release with Apple Silicon, so it is the floor for a
# universal binary. There is no fully static option on macOS -- libSystem is
# always dynamic -- which is why the deployment target does the compatibility
# work that static linking does on Linux.
DEPLOYMENT_TARGET="${DEPLOYMENT_TARGET:-11.0}"

echo "=== build (universal: arm64 + x86_64) ==="
cmake -S "$SRC" -B "$BUILD" -G Ninja \
      -DCMAKE_BUILD_TYPE=Release \
      -DSEICURE_STATIC=OFF \
      -DCMAKE_OSX_ARCHITECTURES="arm64;x86_64" \
      -DCMAKE_OSX_DEPLOYMENT_TARGET="$DEPLOYMENT_TARGET"
cmake --build "$BUILD"

# The test binary is universal too, so this exercises whichever slice the host
# runs natively. Rosetta would be needed to test the other one.
"$BUILD/tests/seicure_tests" | tail -2

BIN="$BUILD/src/cli/seicure"
APP="$BUILD/src/gui/Seicure.app"
APPBIN="$APP/Contents/MacOS/Seicure"

echo "=== binaries ==="
for B in "$BIN" "$APPBIN"; do
  printf '  %-12s %s\n' "$(basename "$B")" "$(lipo -archs "$B")"
  otool -L "$B" | tail -n +2 | sed 's/^/    /'
done

# --- sign ------------------------------------------------------------------
# --options runtime is the hardened runtime, without which notarisation is
# refused outright. --timestamp matters for the same reason it does on Windows:
# without it every signature dies the day the certificate expires.
#
# No --deep. Apple discourages it, and this bundle has nothing nested in it --
# one executable and an icon -- so signing the bundle itself is the whole job.
if [ -n "$SIGN_ID" ]; then
  echo "=== sign ==="
  codesign --sign "$SIGN_ID" --options runtime --timestamp --force "$BIN"
  codesign --sign "$SIGN_ID" --options runtime --timestamp --force "$APP"
  codesign --verify --strict --verbose=1 "$BIN" 2>&1 | sed 's/^/  /'
  codesign --verify --strict --verbose=1 "$APP" 2>&1 | sed 's/^/  /'
fi

# --- CLI tarball -----------------------------------------------------------
TARDIR="$BUILD/tar/seicure-${VERSION}-macos-universal"
rm -rf "$TARDIR"; mkdir -p "$TARDIR"
install -m 0755 "$BIN" "$TARDIR/seicure"

cat > "$TARDIR/README.txt" <<EOF
Seicure ${VERSION} — macOS command line (universal)

  ./seicure enc <file>...        encrypt
  ./seicure dec <container>...   decrypt

Universal binary: runs natively on Apple Silicon and Intel.
Requires macOS ${DEPLOYMENT_TARGET} or later.

The graphical version ships separately, in the .dmg.

If macOS refuses to run it ("cannot be opened because the developer
cannot be verified"), the build is not yet notarised. Remove the
quarantine flag to run it anyway:

  xattr -d com.apple.quarantine ./seicure

Encrypted files are named with random hex. The original name, size,
timestamps and permissions live inside the ciphertext and are restored
on decryption.

There is no password recovery. That is by design.
EOF

tar -C "$(dirname "$TARDIR")" -czf \
    "$OUT/seicure-${VERSION}-macos-universal.tar.gz" "$(basename "$TARDIR")"

# --- .dmg ------------------------------------------------------------------
# The staging folder is what the mounted volume looks like: the app, and a
# symlink to /Applications so the whole install is one drag across the window.
STAGE="$BUILD/dmg/Seicure"
rm -rf "$(dirname "$STAGE")"; mkdir -p "$STAGE"
cp -R "$APP" "$STAGE/"
ln -s /Applications "$STAGE/Applications"

cat > "$STAGE/README.txt" <<EOF
Seicure ${VERSION} — macOS

Drag Seicure.app onto the Applications folder shown beside it.

Requires macOS ${DEPLOYMENT_TARGET} or later. Universal: runs natively on
Apple Silicon and Intel.

This build is not yet signed or notarised, so Gatekeeper will refuse the
first launch. Right-click the app and choose Open -- that offers an
"Open anyway" button where a double-click gives only "Cancel".

Encrypted files are named with random hex. The original name, size,
timestamps and permissions live inside the ciphertext and are restored
on decryption.

There is no password recovery. That is by design.
EOF

DMG="$OUT/Seicure-${VERSION}-macos.dmg"
# UDZO is the compressed read-only format every macOS opens without a prompt.
hdiutil create -volname "Seicure ${VERSION}" -srcfolder "$STAGE" \
               -ov -format UDZO -quiet "$DMG"

echo "=== dmg ==="
hdiutil verify "$DMG" 2>&1 | tail -1 | sed 's/^/  /'

# --- notarise and staple ---------------------------------------------------
# The .dmg is what gets submitted, because the ticket has to be stapled to the
# thing people actually download. Stapling embeds it so the first launch
# validates offline; without it macOS calls home, and a slow or absent network
# becomes a blocked launch.
if [ -n "$SIGN_ID" ]; then
  codesign --sign "$SIGN_ID" --timestamp --force "$DMG"

  echo "=== notarise ==="
  if xcrun notarytool submit "$DMG" --keychain-profile "$NOTARY_PROFILE" \
                                    --wait 2>&1 | tail -6 | sed 's/^/  /'; then
    xcrun stapler staple "$DMG" 2>&1 | tail -1 | sed 's/^/  /'
    echo "=== gatekeeper verdict ==="
    spctl --assess --type open --context context:primary-signature \
          -v "$DMG" 2>&1 | sed 's/^/  /'
  else
    echo "  notarisation failed -- the dmg is signed but not stapled"
    echo "  log: xcrun notarytool log <id> --keychain-profile $NOTARY_PROFILE"
  fi
fi

# --- checksums -------------------------------------------------------------
# Last, deliberately: signing and stapling both rewrite the file.
( cd "$OUT" && shasum -a 256 -- * > SHA256SUMS.txt )

echo "=== release ==="
ls -la "$OUT" | tail -n +2 | awk '{printf "  %-44s %10s\n", $9, $5}'

: <<'SIGNING'
One-time setup, on the Mac itself
---------------------------------
Both steps need the login keychain, so run them in Terminal on the machine, not
over ssh.

  1. A Developer ID Application certificate must exist in the keychain:

       security find-identity -v -p codesigning

     If another Mac already has one, export it there (Keychain Access, right
     click, Export as .p12) and double-click the file on this one.

     Otherwise: Keychain Access > Certificate Assistant > Request a Certificate
     From a Certificate Authority, save to disk, upload the CSR at
     developer.apple.com under Certificates as a Developer ID Application, then
     double-click the downloaded .cer.

  2. Store the notary credentials once, so no password ever reaches a script:

       xcrun notarytool store-credentials seicure \
             --apple-id <apple-id> --team-id F3Z4RU59X6

     It prompts for an app-specific password -- created at appleid.apple.com
     under Sign-In and Security. That is not the Apple ID password.

After that this script signs, notarises and staples on its own.

The CLI tarball can be signed but not stapled: only bundles, disk images and
installer packages carry a ticket. That is why the GUI ships as a .dmg, and why
the tarball's README explains the quarantine flag.
SIGNING
