# Test record and limits

Date: 2026-10-31. Host: Apple Silicon macOS, Xcode 27.2.
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

The latest Release-native test run passes 110 authentication/core checks and 57
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

Sixteen Flutter tests cover disabled launch without ownership, structured error
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

- Real Microsoft login / Minecraft Services access using SECL's own approved ID.
- Real game startup; no installed game is currently available on this host.
- Complete online install/upgrade/rollback matrices for all Java vendors, game
  versions, Forge/NeoForge installers and OptiFine builds.
- System credential persistence with a real account; current UI is session-only.
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
