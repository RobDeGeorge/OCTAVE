**Status:** deferred (quick-fix in place since v0.9)
**Last updated:** 2026-05-02

# Persist Android signing keystore as a GitHub Secret

## Why this is parked

The CI APK signing currently generates a **fresh keystore each release**. That means:

- Every release uses a **different signing identity**.
- Android refuses to update an installed app from one release to the next when the signing key differs — users must uninstall first.
- We cannot ship to the Play Store, since Play requires a stable signing identity.

This was an acceptable shortcut for v0.9 (the goal was just "make the APK installable for a sideload test"), but for any release where users are expected to update over the previous version (v0.9.1 onward, or any beta/RC pipeline), we need a persistent keystore.

## What needs to happen

1. **Generate the keystore once, locally:**
   ```bash
   keytool -genkeypair -v \
     -keystore octave-release.keystore \
     -alias octave \
     -keyalg RSA -keysize 2048 \
     -validity 10000 \
     -storepass <STRONG_PASS> \
     -keypass <STRONG_PASS> \
     -dname "CN=OCTAVE, O=Rob DeGeorge, C=US"
   ```
   Pick passwords that are NOT `octavepass` (currently hardcoded). Use a password manager.

2. **Back up the keystore in three places** (losing it = losing the ability to ship updates ever):
   - 1Password / Bitwarden vault
   - Encrypted offline backup (USB, encrypted drive)
   - Anyone else on the team who is a release manager

3. **Store as GitHub Secrets** on `RobDeGeorge/OCTAVE`:
   - `ANDROID_KEYSTORE_BASE64` — `base64 -w0 octave-release.keystore`
   - `ANDROID_KEYSTORE_PASS` — store password
   - `ANDROID_KEY_ALIAS` — `octave`
   - `ANDROID_KEY_PASS` — key password

4. **Update `.github/workflows/build.yml`** in the `cpp-build-android` job, replacing the "Sign APK with test keystore" step:

   ```yaml
   - name: Decode signing keystore
     env:
       KEYSTORE_B64: ${{ secrets.ANDROID_KEYSTORE_BASE64 }}
     run: |
       if [ -z "$KEYSTORE_B64" ]; then
         echo "ERROR: ANDROID_KEYSTORE_BASE64 secret missing"
         exit 1
       fi
       echo "$KEYSTORE_B64" | base64 -d > octave.keystore

   - name: Sign APK
     env:
       KS_PASS: ${{ secrets.ANDROID_KEYSTORE_PASS }}
       KEY_PASS: ${{ secrets.ANDROID_KEY_PASS }}
       KEY_ALIAS: ${{ secrets.ANDROID_KEY_ALIAS }}
     run: |
       set -e
       UNSIGNED=$(find build-android -path '*/build/outputs/apk/release/*-unsigned.apk' | head -n1)
       BUILD_TOOLS="$ANDROID_HOME/build-tools/33.0.2"
       "$BUILD_TOOLS/zipalign" -v -p 4 "$UNSIGNED" octave-aligned.apk
       "$BUILD_TOOLS/apksigner" sign \
         --ks octave.keystore \
         --ks-pass "pass:$KS_PASS" \
         --key-pass "pass:$KEY_PASS" \
         --ks-key-alias "$KEY_ALIAS" \
         --out octave-signed.apk \
         octave-aligned.apk
       "$BUILD_TOOLS/apksigner" verify --verbose octave-signed.apk
       mkdir -p dist
       cp octave-signed.apk "dist/OCTAVE-${GITHUB_REF_NAME:-dev}-arm64-v8a.apk"
   ```

5. **Delete this file when done.**

## Cross-references

- `.github/workflows/build.yml` — `cpp-build-android` job, "Sign APK with test keystore" step (the placeholder we're replacing).
- `TODO/ci-multiplatform-builds.md` — original CI plan; mark Android-signed builds as done after this lands.

## Recommended order

Do this **before** v0.9.1. The longer we ship test-keystore APKs, the more confusing it gets when users discover they have to uninstall on every update.
