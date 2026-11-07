# Test record and limits

Updated: 2026-11-05. Host: Apple Silicon macOS, Xcode 27.2.
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

Fifty-five Flutter tests cover disabled launch without ownership, structured error
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
The CurseForge application submitted on 2026-11-03 was subsequently approved.

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

## World management acceptance — 2026-11-04

The first world-management UI uses the shared management table, checkboxes,
image caching and dialogs. All 43 Flutter tests and static analysis pass,
including compact Simplified Chinese / Traditional Chinese / English layouts,
loading inside the list, error retry, restore-as-copy and cross-instance warnings.
The existing 144 core / 75 engine checks remain passing; the new native world
suite adds 41 checks. These use generated temporary fixtures, not real player saves.

World tests cover compressed and uncompressed NBT, Java modified-UTF8 emoji,
metadata failure with raw export retained, nested folders and ZIPs, independent
ZIP decoding, CRC errors, traversal, duplicate paths, symlinks, collisions,
cancellation cleanup, source changes, native active-game state and an external
Java-style byte-range lock. Source files remain unchanged.

Run the local-only integration check with:

```sh
python3 tools/worlds_smoke.py build/native/core/libsolar_core.dylib
```

The real FFI acceptance exercised list, ZIP export and Python zipfile validation,
import of a Python-created nested ZIP, copying, backup, restore and moving a
disposable generated fixture to the macOS Trash. No network, sign-in or real
player world is needed. Temporary directories are cleaned automatically; the
Trash test intentionally leaves a small recoverable test copy in the OS Trash.

Limits: restore never overwrites an existing world; no ZIP64, automatic backups,
data-pack management or in-game world loading was tested or added in this pass.
Windows/Linux runtime and their OS lock/Trash behavior remain unverified.

The final macOS UI check also created a backup and restored it to a separate copy through the actual shipped interface. UI-only fixture worlds and their backup records were removed afterwards.


## Installed updates and component-query fixes (2026-11-04)

- Native update tests cover hash association, primary artifacts, ambiguous files,
  release/beta/alpha filtering, no automatic downgrades, wrong loader/game/project,
  per-item unknown/error status, bounded partial network failures and HTTP codes,
  selected roots sharing a graph, channel conflicts, stale plans, and rollback.
- All 55 Flutter tests pass. New cases exercise selection and explicit review,
  cancellation without mutation, channel changes, unknown/error checkboxes,
  release defaults, disabled-state labels, three-language compact layouts,
  in-list loading, clearing stale errors, and reuse of the install dialog.
- Real network acceptance via `tools/updates_smoke.py` uses disposable instances:
  AppleSkin 2.5.1+mc1.20 → 2.5.2+mc1.20.1 (kept disabled), Complementary
  Reimagined r5.9.2 → r5.9.3, and Fresh Animations 1.11.1 → 1.11.2.
  Each new payload hash was verified, superseded files removed, and repeat checks
  returned current. User instances and accounts were not modified by this test.
- Shader/resource-pack selection still happens in the game. If an update changes
  its filename, the result explicitly asks the user to select the new file again.
- OptiFine query previously treated `{builds, source, warnings}` as a bare array,
  causing `invalid_result` even on successful lookup. The UI now reads the envelope,
  keeps loading/errors inside its own card, clears errors on retry, shows fallback
  source information, and resets the selected build when changing channels.
  Tests include valid, malformed and HTTP-failed responses.
- Ordinary settings no longer expose a Microsoft registration/redirect editor;
  fresh data defaults to SECL's approved public Client ID. Authentication and
  ownership gates remain enforced; no sign-in is faked by these tests.
- Runtime compatibility between arbitrary mods and Windows/Linux behavior remain
  outside this acceptance. No automatic update or scheduled checker is enabled.

- Final macOS Release was opened through the native UI. The Acceptance test
  instance's update query showed Sodium 0.5.13 as a newer candidate, while three
  other items had no newer release. This was a query only; no live-instance update
  was applied. The actual OptiFine query displayed HD_U_I5 / Forge 47.0.35 and
  HD_U_I6 / Forge 47.2.18 from BMCLAPI, with the component cards still visible.
- Final four native CTest suites pass, static analysis reports no issues, and
  the packaged app passes ad-hoc signature verification. The app remains open.

## 2026-11-04 combined installed-mod detection

- Five native CTest suites pass. The new inventory suite has 86 assertions for
  cached and fresh reads, same-size replacement with a restored modification
  time, enable/disable renames, multi-loader metadata, source-index changes,
  duplicate / missing / nested dependency attribution, unknown Java, malformed
  source records and corrupt archives beside healthy files.
- All 72 Flutter tests pass and static analysis reports no issues. Added coverage
  checks independent local/source/compatibility states, failed source enrichment
  without losing rows, manual retry, and English / Traditional Chinese diagnostic
  translation while preserving dependency identifiers.
- `python3 tools/inventory_smoke.py build/native/core/libsolar_core.dylib` is an
  opt-in network check. It downloads a real Fabric AppleSkin file and verifies its
  SHA-512 identity through Modrinth. A synthetic JAR remains source-unmatched while
  still reporting its missing dependency. An intentionally unreachable local
  proxy then verifies failure recovery: local rows and verified identities remain,
  with no source-index mutation. Changing bytes invalidates the old source match.
  All settings and files are isolated in a temporary directory and removed.
- The real C ABI smoke check also passes authentication gates and local Java
  detection. No game account, game installation or live-instance mod file is
  changed by these checks. Arbitrary runtime compatibility and the new cache's
  Windows/Linux behavior still require testing on those platforms.

- Final macOS ARM64 Release UI inspection: four installed mods (Comforts, Fabric
  API, Sodium and Iris) matched their Modrinth identities through the source
  action. Returning to the list retained those identities. Status phrases now
  wrap as whole labels rather than leaving a single Chinese character on a line.
  Shader support still detects the installed Iris/Sodium combination. The app is
  left open on the mod list. The final bundle passes strict ad-hoc signature
  validation; its ZIP is 10,491,544 bytes. This is not a notarized distribution.


## Dependency-selection and metadata completion (2026-11-05)

- All nine native CTest suites pass. New focused suites contain 183 joint
  constraint checks, 128 marketplace-graph checks, 61 metadata-parser checks,
  and 151 dialect-specific version checks. Constraint cases include 147
  exhaustive two-version comparisons against a brute-force oracle, mutually
  inconsistent consumers, versioned aliases, Quilt groups and conditional
  exceptions, inactive candidates, runtime aliases and unknown bundled libraries,
  language loaders, JarJar coordinates, independent missing dependencies, and
  bounded analysis. Other enabled rows cannot be labeled fully checked when the
  joint graph is unresolved.
- Metadata coverage includes multiline and escaped TOML, multiple mod sections,
  manifest versions, repeated requirements, client/server dependency sides,
  recursive nested archives and plain JarJar libraries. Unsupported declarations
  remain review warnings. Fabric, Quilt and Maven predicates retain separate
  semantics; malformed or unsupported comparisons do not become false mismatches.
- Marketplace tests cover transitive and reverse existing-file exclusions,
  shared mandatory reachability independent of traversal order, optional
  deselection, disable consent, disabled updates, hash-bound identities, stale
  and caller-edited plans, and fresh revalidation before applying. Recursive
  candidates with the same ID retain separate hashes. Website-only exclusions
  involving a possibly inactive nested candidate are visible nonblocking
  warnings, including both source and target directions; direct-file exclusions
  still block. The exact nested candidate's website version is shown.
- All 88 Flutter tests pass; full static analysis reports no issues. New
  graph-review tests cover checkbox changes, mandatory descendants, reverse
  conflicts, consent removal, managed OptiFine, and multiple nested-version
  warnings without duplicate widget keys. English and Traditional Chinese
  diagnostics preserve literal names, IDs, paths, and placeholder-like text.
- The three new metadata/graph/constraint suites also pass with AddressSanitizer
  and UndefinedBehaviorSanitizer enabled.
- Real-network `tools/updates_smoke.py` passes with disposable AppleSkin,
  Complementary Reimagined and Fresh Animations files, including preservation of
  a disabled mod. `tools/inventory_smoke.py` passes against the new C ABI: real
  SHA-512 association, multiple independent missing dependencies, unreachable
  proxy recovery, unchanged source index on failure, and invalidation after file
  changes. Account/ownership gates and local Java detection also pass the C ABI
  smoke. These fixtures do not alter user game files or accounts.
- The actual macOS Release interface was opened. Four installed mods retained
  icons, local/source recognition and known-conflict status. The update query
  and Sodium update review loaded successfully; the review was canceled, so no
  live-instance payload was changed.

Limits: this is static preflight, not execution of the loader resolver or a
promise of runtime compatibility. Custom Quilt plugins, exact JarJar classloading
policy, unverified language-loader internals, Mixin/API/rendering interactions,
and Windows/Linux runtime behavior still need real game acceptance. The native
core remains Qt-free. toml++ 3.4.0 is pinned with its MIT notice included in the
repository and the app's third-party license list.

The final rebuilt macOS ARM64 Release was reopened after the last graph-warning
fix and verified on the four-mod instance list. Strict ad-hoc signature validation
passed; the ZIP is 10,683,325 bytes. This is not a notarized distribution.


## Modrinth modpack import (2026-11-05)

- Eleven C++ CTest suites and 103 Flutter tests pass; full Flutter analysis is
  clean. New parser fixtures contain 278 checks; new installer fixtures contain
  179 checks. Both new native suites also pass with AddressSanitizer and
  UndefinedBehaviorSanitizer.
- Parser fixtures cover ZIP structure/CRC/index limits, malformed UTF-8 and
  duplicate JSON keys, paths and portable device names, case/prefix collisions,
  reserved launcher files, dependency/game/loader requirements, client/server
  flags, optional files, URL allowlists and exact override layering.
- Offline installer tests run the real game installer through an injected
  native transport and a self-executable Java probe fixture. They exercise
  vanilla and Fabric, both file hashes and sizes, URL fallback/redirect bounds,
  edited and stale plans, existing targets, wrong shared caches, cancellation,
  retained verified files, client override ordering, corrupted override CRC,
  orphan ownership/recovery, stale legacy locks, and warm shared-cache reuse.
  Completed imports do not retain a duplicate prepared game/Java tree or alter
  existing shared-file permissions. Synthetic Java and game data are confined
  to tests and are never bundled with the launcher.
- UI tests cover both entry points, required/default-off optional files, editable
  plan errors, cancel without mutation, retry with the exact plan, cancellation
  after commit, three languages, narrow headers, virtualized long lists and the
  distinction between pack-file bytes and additional game/Java/loader downloads.
- Opt-in real acceptance via `tools/modpack_smoke.py` downloaded the pinned
  official Adrenaline release `dN5QXu6n` / project `BYN9yKrV`, verified its
  5,593-byte archive SHA-512, and imported all 14 files and 10 common overrides
  for Minecraft 1.20.1 / Fabric Loader 0.15.9 through the public C ABI. Final
  file SHA-1/SHA-512, client JAR, configuration bytes, isolated paths, settings,
  Java path and instance scan were checked. Duplicate target import was rejected.
  Existing test assets/libraries were seeded read-only; their hashes, sizes,
  modification times and permissions were unchanged afterwards. The temporary
  imported instance was removed on exit. This public pack has no client-specific
  overrides; that layering is covered by offline fixtures.
- No game was launched and no account was accessed during this acceptance.
  Windows/Linux filesystem behavior, Forge/NeoForge/Quilt pack execution and
  arbitrary pack runtime compatibility remain unverified. Custom install
  scripts are not supported by this importer. Cached complete
  files are reused after interruption; incomplete bytes are not range-resumed.

The final macOS ARM64 Release was opened through the native interface. The game
library import entry loaded the pinned archive and displayed its file list,
Minecraft/loader requirements, target directory, download-size scope and full
confirmation plan without overflow. This UI check stopped at confirmation;
complete installation was exercised separately through the C ABI above. Strict
ad-hoc signature verification passed; the ZIP is 10,794,752 bytes. It is not a
notarized distribution.


## 2026-11-07 submission validation

The current C++ Release core builds successfully and passes all eleven CTest
suites, including CurseForge/modpack metadata and installation checks. The current
Flutter sources pass 135 tests and static analysis reports no issues. Home
carousel input, responsive layouts, cover selection, market source controls and
modpack UI contracts are included. Native test fixtures remain isolated; these
checks do not establish compatibility for every real game/mod combination.

The ARM64 macOS Release bundle with the version cover artwork and vector terrain-S
branding is 33,599,088 bytes (33.6 MB); its ZIP is 17,380,159 bytes (17.4 MB).
Strict deep ad hoc signature verification passes. Build products, private notes,
credentials and validation logs are excluded from source control.


### Unified management interface follow-up

The library, instance/resource tables, all three markets, Java and downloads now
share a restrained landscape page heading and aligned controls. Appearance
settings use a full content page; the previous page remains mounted while hidden
and excluded from keyboard focus. New layout checks cover compact English/light/
dark headings, library management actions and settings focus/state restoration.
All 138 Flutter tests pass and static analysis reports no issues.

The updated ARM64 macOS Release builds, passes strict deep ad hoc signature
verification, and opens with the existing native core. Its app is 33,632,300 bytes;
the ZIP is 17,391,371 bytes. The library, markets, independent settings page,
Java runtime details and a live Java build query have been visually inspected in
the running application. Generated design references and
prompts are private development artifacts, excluded from the repository.
