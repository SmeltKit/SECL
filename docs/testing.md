# Test record and limits

Updated: 2026-11-03. Host: Apple Silicon macOS, Xcode 27.2.
Flutter 3.19.6 (revision `54e66469a933b60ddf175f858f82eaeb97e48c8d`), Dart 3.3.4.

Commands:

```sh
cmake -S . -B build/core -DBUILD_TESTING=ON
cmake --build build/core --parallel 3
ctest --test-dir build/core --output-on-failure
python3 tools/core_smoke.py build/core/core/libsolar_core.dylib
cd app
/path/to/flutter-3.19.6/bin/flutter analyze
/path/to/flutter-3.19.6/bin/flutter test
/path/to/flutter-3.19.6/bin/flutter build macos --debug
```

The latest Release-native test run passes 144 authentication/core checks and 75
engine checks. The earlier baseline (110 core / 47 engine checks) also passed
AddressSanitizer and UndefinedBehaviorSanitizer in a separate Debug build.

Native tests cover PKCE's RFC 7636 vector, state/callback validation, the complete
Microsoft→Xbox→XSTS→game entitlement→profile contract with injected responses,
HTTP 403/429/503 failures, identity mismatch and expiration gates. Engine tests
cover hashes, bounded ZIP/CRC/path checks, Java properties, memory limits,
argument injection restrictions, source ordering, nested JAR dependencies,
duplicate IDs, loader mismatches, old M²CL manifest migration, local enable/disable,
shader/resource pack detection, stale plan rejection, metadata-only account
storage, streaming redaction, log decoding and per-instance launch arguments.

**Test identities, JARs and Java-probe output are synthetic fixtures isolated in
temporary directories. They are not bundled games or real authentication.**

The real C ABI smoke script checks ownership gates, missing Client ID errors,
async task dispatch, allocation ownership, and real local Java detection. This
host detects two Homebrew Java runtimes.

Java discovery resolves the JVM's reported `java.home` before deduplication, so
the macOS `/usr/bin/java` launcher does not appear as another installed copy.
Regression checks cover launcher aliases, paths containing spaces and a missing
reported runtime home. A real `JAVA_HOME` reproduction returns Java 21 and 26
once each. The sidebar and launch footer now share the same height and divider;
the unofficial-project notice remains in About and the README.

Thirty-six Flutter tests cover disabled launch without ownership, structured error
presentation, all three markets at 880×640, explicit retry clearing stale errors,
returning from details with search preserved, loader/version matching, grouped
game-version selectors and required/optional dependency checkbox defaults. Static
analysis reports no issues. The real native ARM64 Debug app was opened and
inspected, including home, missing Client ID and configuration states. The README
gallery was subsequently recaptured from the ARM64 Release after localization;
see `screenshots/README.md`. Captures are actual UI, not mockups or synthetic accounts.

The native ARM64 Release app was subsequently built after the user approved
installation of Apple's Rosetta 2 for Flutter 3.19.6's Intel AOT compilation
tools. The app itself is ARM64 and contains no Debug Dart kernel. It was opened
on this host, loaded the bundled C++ core, and displayed the two detected Java
runtimes without duplicates. The bundled library also passed the isolated C ABI
smoke check. ZIP extraction followed by strict deep code-signature validation
passed. This is an ad hoc signed local test build, not a notarized release.

Measured bundle sizes exclude duplicate traversal through framework symlinks;
MB below means 1,000,000 bytes. ZIPs use `ditto -c -k --sequesterRsrc --keepParent`.

| Build | App | ZIP |
| --- | ---: | ---: |
| Debug ARM64 | 103,749,732 bytes (103.7 MB) | 32,008,521 bytes (32.0 MB) |
| Release ARM64 | 21,276,388 bytes (21.3 MB) | 8,524,889 bytes (8.5 MB) |

`otool -L` on the native library shows system libcurl, zlib, Security, libc++ and
libSystem; no Qt libraries. The complete source tree has no Qt includes or CMake
Qt dependency.

Not yet verified:

- World creation/gameplay and long-duration game stability.
- Complete online install/upgrade/rollback matrices for all Java vendors, game
  versions, Forge/NeoForge installers and OptiFine builds.
- System credential persistence with a real account; UI is implemented; real credential restore remains unverified.
- Windows 7/8, 32-bit Windows, Windows ARM64, Intel macOS and Linux packaging.

Old Flutter reference: https://github.com/flutter/flutter/issues/140830.
Do not infer full-project legacy OS compatibility solely from Flutter's version.

## Modrinth resource-market integration

The three market pages share cards, selectors, install dialogs and an 88-point
footer. Counts and grouped pagination are in the footer, aligned with the sidebar.
Category loading stays in the category pane; result loading stays in the list.
Project icons and gallery images are loaded from the source. External project
links open in the browser. Only Modrinth is connected.

Opt-in real network verification:

```sh
python3 tools/resource_smoke.py build/core/core/libsolar_core.dylib
```

This downloaded Comforts 6.4.0+1.20.1 with its required dependency (two installed
JARs), MakeUp Ultra Fast 9.6, and Default Dark Mode 1.20-2023.7.0 into a temporary
directory. Archive/hash validation and repeat-install reuse passed for all three
types. The temporary directories are removed after testing. These are mod/pack
download tests; they contain no game files, do not create an account or ownership,
and do not establish that a real game launched successfully.

Native regression checks additionally cover null dependency IDs returned by
Modrinth, stale pack plans, explicit replacement consent, wrong game versions,
unsafe filenames and unresolved author-declared conflicts.

Current resource-market ARM64 Release: 22061724 bytes as an app; 8833205 bytes as a ZIP.


## Interface localization

Simplified Chinese, Traditional Chinese (Taiwan terminology), and English share a
central catalog in `app/lib/l10n/catalog.dart`. The default follows the OS, including
Hans/Hant script tags and TW/HK/MO region aliases. Manual selection is saved as
`language` in the existing native settings JSON; `system` persists the preference
rather than the currently resolved locale. Unsupported system languages fall back
to English. No settings file with an account or user path is bundled.

Additional Flutter checks cover catalog completeness and parameter preservation,
three-language home/market/detail layouts at 880×640, immediate switching without
losing search input, saved language restoration, system locale changes and retained
HTTP status/code/URL diagnostics. System file dialogs use the OS language. Website
project descriptions, names, filenames, user paths and diagnostic codes remain
original. The native OAuth completion page receives the effective UI locale.

```sh
python3 tools/language_smoke.py build/core/core/libsolar_core.dylib
```

This uses isolated settings directories and separate native processes to verify
all language preferences, persistence after restart, and invalid-value rejection.
It does not log in or write the user's settings. Existing core tests and C ABI
ownership gates still pass.

English category navigation expands with available width and wraps long labels
with content-sized rows. A regression test uses a long category label at the
minimum window size and verifies there is no ellipsis or overflow. Native Release
size with localization is about 22.7 MB (ZIP 9.1 MB).


## 2026-11-03 authentication and downloads

The project AppID was approved for the Minecraft Services API allow list in the
2026-11-02 review. Real browser PKCE sign-in, Xbox / XSTS exchange, Minecraft
Services authentication, Java Edition entitlement and player profile checks were
verified on this macOS ARM64 host on 2026-11-03. No account name, email, token or
administrative screenshot is published as test evidence. Actual game startup
has not yet been validated. API allow-list approval is not official endorsement.
A new CurseForge application was submitted on 2026-11-03 and is awaiting review.

The Downloads UI now calls the migrated Qt-free C++ installers. New checks cover
three-language minimum-window layouts, grouped game channels, passing nested
loader version data, install completion, progress / cancellation, HTTP 503
recovery, an empty game root, duplicate-instance rejection, cancellation before
metadata publication, and rejecting a Java package for a different OS. The Java
build list scrolls with filters at smaller window sizes rather than being squeezed
out by longer English text. Managed Java discovery and download-source settings
are persisted by the native core. New runtime validation allows up to 10 seconds
for a first JVM invocation; ordinary Java scans retain their shorter timeout.

Opt-in integration check (downloads into a temporary directory; no account or launch):

```sh
python3 tools/download_smoke.py build/native/core/libsolar_core.dylib --source auto
```

For manually resuming a failed network test, pass an isolated `--directory`.
This retains only test downloads, allowing verified files to be reused; remove
the directory after testing. No retry loop is added by the script or UI.

Real download validation on 2026-11-03 installed Zulu Java 17.0.21 (macOS
AArch64), verified repeat-install reuse and managed-runtime discovery, then
installed Minecraft 1.20.1 with 3,598 asset entries. Every asset size / SHA-1 and
the client SHA-1 (`0c3ec587af28e5a785c0b4a7b8a30f9a8f78f838`) was checked.
The installed version was discovered successfully and unauthenticated launch was
still rejected. Official-only resource downloads hit a real timeout; the
subsequent automatic-source run completed. Fabric / Quilt / Forge version-list
queries succeeded; NeoForge metadata timed out and remains unverified in this
run. This is a download / recognition check, not a real game-launch result.

Zulu Java 8 was also installed from the official distribution, probed as
`1.8.0_504-b01` on AArch64, discovered, and reused on repeat installation. This
validates matching legacy JVM version syntax against an `8.0.504+1` package build.
All Java 8 smoke-test files were removed automatically afterwards.

Downloads Release ARM64: 22,857,345 bytes as an app, 9,154,986 bytes as a ZIP. Ad hoc signature verification passed; Qt is absent from linked dependencies.


### Signed-in skin avatar

The verified profile now retains the ACTIVE official skin URL as public cosmetic
metadata. The URL is HTTPS and restricted to textures.minecraft.net; bearer tokens
are never passed to the avatar loader. Missing / malformed skin data and rejected
third-party URLs do not invalidate an otherwise verified account. Texture requests
have a ten-second overall timeout, no automatic retries, a one-MiB bound and PNG
size validation. At most eight small immutable texture files are cached in memory.
The UI uses a front orthographic head projection: the 8-unit face is centered
inside the 9-unit outer hair/hat layer. Both layers retain nearest-neighbor pixels
and square corners; the outer layer fits within the avatar bounds without clipping.
The official texture of the current real player was fetched separately
and checked with the production painter; no login or ownership was simulated.
Rendering checks cover the 8:9 face/hat geometry, unclipped square corners,
translucent outer pixels, legacy 64×32 skins and an undistorted head in a rectangular
viewport. Widget tests cover successful image replacement,
logout during an in-flight load, CDN failure, and unchanged entitlement gates.


### Java GA version catalogue

The download selector now requests `java.releases` from the native core. It uses
Adoptium's official `available_releases` list capped by
`most_recent_feature_release`; EA/tip version fields never create download choices.
The numeric 8–30 placeholder range was removed. Common versions are prioritized
only when present in the returned catalogue. Actual vendor/OS/architecture build
availability is still determined by the separate build query.

A catalogue failure shows the existing error/retry panel and does not invent
versions or retry automatically. Native checks cover GA filtering, sorting,
deduplication, malformed/empty catalogues and accepting a later GA release when
metadata changes. Widget checks cover data-driven choices and explicit retry.

## Management migration, 2026-11-03

New tests cover instance metadata, launch log paths, token-redacted log output, invalid Java pins, nested Markdown/HTML retention and sanitization, table alignment and compact account layout. Native: 144 + 75 checks. Flutter: 36 tests. Static analysis: no issues.

Implemented UI is distinct from acceptance testing: real account restoration in OS vaults, all loader/OptiFine update combinations, deep disk scans and Windows/Linux packaging remain unverified.

## Real macOS acceptance run — 2026-11-03

Tested the shipped Release library and app with an isolated temporary game directory. No synthetic identity was used for production launch.

| Scenario | Actual result |
| --- | --- |
| Microsoft sign-in | Real account, ownership and profile verified |
| Avatar | Real official 64x64 PNG decoded; sidebar displayed the skin. Fixed unrecoverable placeholder after CDN failure; manual retry and shared-cache notification added |
| Java download | Zulu Java 17.0.21 ARM64 installed, reinstallation reused existing files, runtime discovered |
| Game download | Minecraft 1.20.1, client SHA1 and all 3,598 asset hashes verified |
| Vanilla launch | Real authenticated process launched, LWJGL, sound and texture initialization completed |
| Fabric launch | Loader 0.16.10 with Comforts, Fabric API, Iris and Sodium launched; 69 total modules including embedded libraries loaded; OpenGL, sound and texture initialization completed |
| Independent settings | Actual JVM command used pinned Zulu 17, -Xmx2048M, --width 960 and --height 600; no tokens printed |
| Resource installation | Comforts plus required dependency, MakeUp shader and Default Dark Mode resource pack downloaded and reused on repeat |
| Wrong-loader import | Real Forge AppleSkin rejected in a Fabric instance, mod directory unchanged |
| Local inventory/log UI | Four top-level mods with embedded icons and versions displayed; real game logs read in the instance page |

Real tests found and fixed different-version Fabric nested libraries being incorrectly treated as duplicate root mods. Fabric Loader treats nested jars as candidates; two explicit root mods with the same ID remain rejected. Reference: https://docs.fabricmc.net/develop/loader/ .

Also fixed isolation settings accidentally replacing an installer-created instances/<id> directory with versions/<id>, and settings failing to display a pinned Java outside the global scan results.

The automation surface did not expose the Java game window for screenshots. Launch acceptance above is based on the actual running process, initialization logs and the user's observation of the opened window; it does not claim visual gameplay or shader rendering validation. Test JVMs were deliberately stopped with SIGTERM after checks (exit 143 is expected for this test stop).

Not covered: actual remembered-account restoration, Forge/NeoForge/OptiFine full install/runtime matrices, Windows/Linux, shader rendering in a world. Test files remain under /private/tmp/secl-live-acceptance and /private/tmp/secl-resource-acceptance, outside the repository.

Final UI acceptance also confirmed that the homepage shows the actual pinned Java 17 (not the global Java 21 selection), and that saved isolated directories retain their installer-created path. The test run left the installed assets in the isolated temporary directory for inspection (approximately 900 MiB).

## Local resource icon repaint regression — 2026-11-03

Local mod, shader and resource-pack icons retain decoded PNG buffers across parent
status polling. Repeated builds now reuse the same Flutter image-cache key;
changed icon content receives a new key. The regression test reproduces cache
key churn before the fix, verifies stable keys afterwards and checks malformed
metadata fallback. All 36 Flutter tests and static analysis passed. The updated
ARM64 Release was opened on the real four-mod inventory; ad hoc signature
verification passed.
