# Core API

C++ headers live under `core/include/solar/`. There is no Qt type in the public API.
Flutter uses `dart:ffi` with the UTF-8 JSON C ABI:

```c
unsigned int solar_abi_version(void); // 1
char *solar_command(const char *request_json);
void solar_free(char *response);
```

The caller frees every response. No credentials, raw authenticated command, or
unverified account identity are accepted or returned by the JSON API. Public
client configuration and display metadata are safe to return. The API is local
in-process functionality, not a remotely exposed service.

Normal shell actions: `poll`, `scan`, `detect_java`, `configure`, `login`, `cancel`,
`logout`, `clear_error`, `launch`. Launch uses the native authenticated identity;
providing invented account fields in a JSON request has no effect.

Backend operations use one asynchronous task at a time:

```json
{"action":"engine","request":{"op":"capabilities"}}
```

Poll until `busy` becomes false. The snapshot contains `result`, `progress`, and
structured `error` fields (`stage`, `code`, `message`, `http_status`, `url`).
Use `cancel` to request cancellation; retries are bounded, not an endless loop.
Cancellation cannot interrupt an OS Java probe until its short timeout returns.

Operation groups are defined in `core/src/engine.cpp`:

- `accounts.list`, `accounts.save` (metadata only), `platform`, `java.*`, `memory.*`, `instances.scan`, `settings.*`
- `resource.*`, `mods.*`, `packs.*`, `shaders.support`, `quilt.*`
- `game.*`, `loader.versions`, `optifine.*`

Install operations mutate the explicitly supplied target paths. The resource
market frontend serializes engine requests and shows an instance picker and
install plan before calling `mods.apply`, including `disable_files`, skipped
requirements, reuse and redownload choices. `mods.select` applies checkbox
choices; optional dependencies are not implicitly forced on. Replacements are
validated against the original hash and manifest snapshot before commit.

`resource.categories`, `resource.games` and `resource.projects` load selector
metadata independently. Failed requests show structured errors in the relevant
pane and are retried only by explicit user action. Pagination lives in the
shared footer. `packs.plan` validates the target game version and captures the
existing file hash; `packs.apply` downloads to staging, verifies the archive and
hash, rejects stale plans and requires confirmation before replacing different
same-name content. Identical installed packs are reused without downloading.

`resource.search`, `resource.categories`, `resource.project`, `resource.projects`
and `resource.versions` accept `source: "curseforge"`. The default is
`"modrinth"`. CurseForge versions requests can supply `offset` to receive
`files` and `next_offset`. Project and file IDs are specific to their source.
CurseForge files carry an official webpage link and are installable only when
the API supplies an allowed download URL and SHA-1. Downloads go directly to
the approved CDN hosts, with the host validated again after each redirect.

Source modes: `auto`, `official`, `bmclapi`, `auto-mirror-first`,
`auto-official-first`. Automatic mode prefers mirrors for Asia/Shanghai, official
URLs elsewhere. Only supported endpoints have mirror mappings. Each candidate
is attempted once; metadata and payload limits are bounded.

For smoke testing without credentials:

```sh
python3 tools/core_smoke.py build/core/core/libsolar_core.dylib
```

Use `.so` on Linux or the appropriate `.dll` path on Windows. The script sets an
isolated `SOLAR_DATA_DIR` and never fabricates a production session. An optional
`SOLAR_CORE_LIBRARY` environment variable can point the Dart bridge at a library
during development; ordinary application bundles load their packaged library.

`accounts.hpp` additionally exposes Microsoft metadata storage, OS vault adapters,
streaming redaction and XML log decoding to C++. Raw vault operations are not
exposed to Dart through the generic JSON API. Never store secrets in account JSON.

The product is now SECL. Existing `solar_*` ABI symbols and local data paths are
retained for compatibility; they are internal module identifiers, not the product name.


The UI `configure` action accepts `language`: `system` (default), `zh_Hans`,
`zh_Hant`, or `en`. It is persisted and returned in snapshots. Invalid values
return `settings / invalid_language`. `login` accepts an effective `locale` for
the browser completion page. Authentication state, error codes, HTTP status and
URLs remain language-independent; the Flutter view localizes known core messages.


The UI `configure` action also accepts persisted `download_source`: `auto`,
`official`, or `bmclapi`. Snapshots expose `java_directory` for managed runtimes.
Discovery includes this directory and official runtimes under the game root.
`game.install` returns `root`, `id`, `directory`, `java` and `metadata`; the UI
rescans the returned root after successful installation. A new install cannot
replace an existing instance unless `replace_existing` is explicitly requested.
An explicitly fixed install-time Java choice is saved in that instance's settings.
The home Java selector can update the selected instance via `configure.version`.


Verified account snapshots also contain optional `skin_url`, taken from the
ACTIVE official Minecraft profile skin. This is a public texture URL, never a
credential. The Flutter avatar fetches only canonical HTTPS texture URLs and
renders the 8x8 face plus hat overlay. Skin errors do not change account ownership.


`java.releases` returns an array of published GA major versions, newest first,
from the official Adoptium release catalogue. It filters out EA/tip versions.
This is a version selector catalogue, not a guarantee of a particular vendor or
architecture package; call `java.builds` with the selected filters for those.

## World management

All `worlds.*` requests take `instance` with `game_directory`. `worlds.list`
returns folder, display name, version, last_played (milliseconds), mode, hardcore,
size and an optional icon_base64 / metadata problem. `worlds.open` opens saves
or the game folder when saves does not exist. `worlds.backups` lists this
instance's SECL backup manifests.

- `worlds.import`: path (folder or ZIP), name (new folder name).
- `worlds.export`: folder, path (new ZIP filename, never overwrites).
- `worlds.copy`: folder, name, optional target_instance.
- `worlds.backup`: folder; writes ZIP and a manifest under backups/secl.
- `worlds.restore`: archive from the backup list, name; always creates a new copy.
- `worlds.remove`: folder; uses the OS Trash, never permanent deletion.

Mutations, exports and backups are rejected for the game's active directory,
using trusted native process state, and for a locked session.lock. File work is
streamed, cancellable and published from a staging directory only on success.
File changes, symlinks, special files, traversal and duplicate ZIP paths abort.
Current limits: 64 GiB / 100,000 directory entries for folder operations; classic
ZIP only (below 4 GiB and 65,535 files). ZIP64 is explicitly rejected, with folder
copy offered instead. This does not determine mod compatibility between worlds.


## Installed resource updates

`updates.check` takes `instance`, `kind` (`mod`, `shader`, `resourcepack`) and
`channel` (`release` default, `beta` includes releases, `alpha` includes all).
It hashes top-level local files and uses Modrinth's batch hash-identity and
hash-update APIs. Mod queries use the instance loader (Quilt also permits Fabric
candidates); packs do not inherit the mod-loader filter. The response contains
`target`, `kind`, `channel`, a resource `snapshot`, and `items` with individual
`status` / `message`, optional structured `error`, and a `candidate` for updates.
Unknown, duplicate, wrong-environment and failed lookups are distinct from
`current`. Publication dates prevent silently downgrading an installed newer
beta when checking releases. Folder packs and embedded jars are not separate
update targets. Network requests are cancellable and attempted once; an outage
stops remaining chunks and explicitly marks them failed instead of flooding the
service or claiming they are current.

`updates.plan` takes that `report` and `selected` local filenames (1–128).
It rejects stale inventories, combines selected mods into one recursive graph,
reuses compatible installed project-only dependencies, and restricts new
required dependencies to the selected channel. The existing install dialog
reviews dependencies and declared conflicts. Pack batches use the same dialog.
`mods.select` still applies dependency checkboxes to a mod update plan.
`updates.apply` consumes the reviewed plan and returns installed filenames and
warnings. It stages and validates payloads before replacement, retains disabled
mod state, rejects changed inventories, and rolls back live-file renames on
failure. Successful updates remove superseded copies. The in-app running
instance is blocked from updates. Runtime behavior and unreported conflicts
still require actual game testing; an update candidate is not a compatibility
guarantee. Existing resource-pack activation order is managed in the game.

The normal UI uses SECL's own approved public Client ID by default and no longer
exposes application registration or redirect configuration. The Client ID is
public application identity, not a secret; ordinary users only sign in.

## Installed mod detection

`instance.inventory` accepts optional `fresh: true`. Mod rows include `detection`
with three independent fields: `local` (`recognized`, `unknown`, `invalid`),
`source` (`matched`, `unmatched`, `not_checked`, `failed`), and `compatibility`
(`conflict`, `needs_review`, `no_known_conflict`, `disabled`). `issues` contain
severity, a stable diagnostic code, message and related filenames. Legacy
`errors` / `warnings` remain. Checks use the complete enabled mod set, including
nested components; issues are then attributed to affected top-level files.
Missing Java information produces a warning, not a fabricated Java 0 mismatch.
A source match never certifies loader or runtime compatibility.

`mods.enrich` returns the same inventory shape. It batches SHA-512 lookups against
Modrinth and stores verified, hash-bound Modrinth identities in `.solar-mods.json`.
With CurseForge access configured, it also batches file fingerprints and
confirms each exact match using SHA-1. Rows report the verified providers in
`sources`. CurseForge metadata is used in the current response and is not
persisted. When both enabled files have verified CurseForge identities, an
author-declared incompatibility is an error. An unverified required dependency
is reported as a warning. Provider failures appear in `source_errors` and
retain local results and any successful lookup from the other provider.
A changed source index or file set aborts publication.

The bounded process-memory scan cache separates content metadata from filenames,
enabled state and source records. Browsing validates file identity, size, write
time and change time before reuse; unreliable platform stamps fall back to a
fresh read. Fresh refreshes and all mutation / update / launch validation read and
hash bytes again. File or manifest changes cannot inherit stale website identity.
Malformed source records retain readable local rows for browsing, but block
mutations until repaired. The cache is not a persisted compatibility database.

### Loader metadata and a consistent embedded selection

`mods.inspect` keeps legacy `ids`, `provided_versions`, `requirements`, `breaks`
and `conflicts`, and adds `own_ids` / `own_provided_versions`, `dependency_clauses`,
`metadata_warnings`, environment / load policy, language-loader metadata, and
JarJar artifact records where available. A clause has `kind` and an `expression`:
`{op: atom, id, range, dialect, optional?, unless?}` or `{op: all|any, children}`.
Quilt version groups use `any` / `all`; aliases retain their own versions and
qualified Maven group. Rich clauses are authoritative when present; legacy-only
records remain supported and incomplete embedded metadata produces a warning.

The checker encodes all active roots, provider exclusivity, selected-parent
availability, mandatory dependencies, negative declarations and JarJar artifact
range requirements into one bounded satisfiability problem. Dependencies from
unselected candidates do not participate. It returns a diagnostic `selection`
and `selection_status` (`satisfiable`, `conflict`, `unknown`); this is not an
instruction to rewrite JARs or an exact reproduction of loader preference order.
Node, provider, expression, clause, search-depth and elapsed-time limits return
warnings. Unsupported comparisons remain unknown. Runtime-provided Quilt group
aliases are separate from user mods; an unverified Fabric shim or loader-bundled
library version remains uncertain. JarJar classes, custom loader plugins and
unmodeled language-loader details still need the actual loader.

Install plans additionally retain `marketplace_existing`, `marketplace_graph`,
`graph_conflicts`, `graph_warnings`, and `local_graph_files`. Existing file identities are verified
against website file hashes. Conflicts are checked in both directions across
selected roots, transitive candidates and existing enabled components. Disabling,
replacement, optional deselection, and disabled-state preservation are reflected
in both native selection and the local dialog preview. Required reachability is
computed from all roots, independent of discovery order. Before applying, website
declarations are fetched again and the graph is rebuilt using fresh local hashes;
caller-edited cached declarations cannot authorize an unresolved conflict.
Nested identities are enumerated from recursive component records, preserving
multiple candidates with the same mod ID and the hashes of grandchildren.
Website-only exclusions involving a possible nested candidate are explicit,
nonblocking `graph_warnings`; they do not prove that every nested version is
loaded. Warning labels use the website version of that candidate. Local metadata
constraints still participate in the joint selection. Ordinary direct-file
exclusions remain enforced. Warnings appear in the review and apply result.
Lookups use bounded batches and fail explicitly without endless retries.

Independent impossible root requirements are all reported, even when the joint
solver's first proof involves another mod. If the full graph is unsatisfiable,
other enabled rows are marked `dependency_graph_unresolved` / `needs_review`,
rather than claiming a complete conflict-free joint check.

Reference semantics: [Fabric metadata](https://wiki.fabricmc.net/documentation:fabric_mod_json_spec),
[Fabric selection](https://github.com/FabricMC/fabric-loader/blob/master/src/main/java/net/fabricmc/loader/impl/discovery/ModSolver.java),
[Quilt metadata](https://github.com/QuiltMC/rfcs/blob/main/specification/0002-quilt.mod.json.md),
[Quilt reader](https://github.com/QuiltMC/quilt-loader/blob/develop/src/main/java/org/quiltmc/loader/impl/metadata/qmj/V1ModMetadataReader.java),
[NeoForge mod files](https://docs.neoforged.net/docs/gettingstarted/modfiles/), and
[Maven version ordering](https://maven.apache.org/pom.html#version-order-specification).


## Modpack import

`modpacks.inspect {path}` validates a local `.mrpack` ZIP and returns its archive
SHA-512, name/version/summary, Minecraft and primary loader requirements, normalized
file descriptors (`path`, SHA-1/SHA-512, size, URL alternatives, client requirement),
override layers and environment counts. Only format v1 / Minecraft is accepted;
unknown required dependency IDs and multiple primary loaders fail explicitly.

`modpacks.plan {path,root,id,name?,optional_files?}` reads the archive again,
requires an unused target `versions/id` and `instances/id`, and returns a review
plan. Client-required files are always included, optional files default off, and
server-only files are omitted. The download byte count covers pack files only;
game, Java and loader downloads are additional. Native callers may pass `java`
with an explicit local executable; otherwise the existing game installer selects
or installs an appropriate runtime.

`modpacks.apply {plan}` reconstructs the plan from the archive and verifies that
its hash is unchanged. Client files require both SHA-1 and SHA-512. URLs are
restricted to Modrinth's documented CDN/GitHub/GitLab hosts; supported GitHub
release-asset redirect hosts are also explicitly listed. Each redirect is checked
before connecting (at most three); at most three alternative file URLs are tried,
without automatic retry loops. Ordinary game downloads retain the configured
Official/BMCLAPI/Automatic policy.

`overrides/` is applied first and `client-overrides/` second. No server overrides,
path escapes, symlinks, device names, case/prefix ambiguities, duplicate JSON keys,
or overwrites of launcher-owned version/library/runtime/settings files are
allowed. The actual destination filesystem also detects aliases during staging.
Override data is CRC-checked before publication.

Verified pack downloads and unfinished game preparation are retained under hidden
`.solar-mrpack/<archive-hash>/`. Successful imports remove the duplicate prepared
game tree; subsequent imports reuse matching shared game caches. Retrying
revalidates and reuses complete files;
this is file-level reuse, not byte-range resumption of an incomplete download.
OS-held locks release on process exit. An owned game-directory orphan left before
metadata publication can be moved into retained recovery storage and reinstalled;
unrelated existing directories are never adopted or deleted. Existing shared
libraries/assets/runtime files are reused only when bytes match; differing files
are not overwritten. The final game directory and version directory are published
with atomic no-replace renames, version metadata last. Java and game paths are
rebased and Java is re-probed before publication. The result includes `committed`,
`id`, `root`, `directory`, `java`, `metadata`, `modpack`, `checks`, and `warnings`.
Late cancellation cannot conceal an already committed success.

Bounds: archive 256 MiB, 10,000 ZIP entries and 2 GiB expanded data, index 16 MiB,
10,000 listed files, 2 GiB per download / 16 GiB aggregate, and 256 MiB per client
override. ZIP64 and unsupported compression fail explicitly. Custom modpack
scripts are not executed.
The original archive is never edited, and authentication/ownership gates still
apply to launching the resulting game.

For a CurseForge `.zip`, `modpacks.inspect` reads the root `manifest.json` without
contacting the API. The result has `format: "curseforge"`; file selections use
`projectID/fileID` as the `path` key. `modpacks.plan` resolves exact manifest
entries for mods, resource packs and shaders, using the configured CurseForge
Key. Optional files remain off by default. The response lists items that need
attention in `blocked_files`, with their IDs and official links when available.
Callers may pass `supplement_directory` for files downloaded manually; matching
filenames must also match the official file size and SHA-1. Selected blocked
entries prevent installation until resolved or, for optional files, deselected.

Apply revalidates the archive and pinned file identities before staging. It
installs the selected manifest entries and applies the pack's overrides. It
checks author-declared conflicts and local JAR metadata before publishing the
isolated instance. CurseForge API metadata and its download URLs are not
written to disk.

Format references

- [Modrinth .mrpack specification](https://support.modrinth.com/en/articles/8802351-modrinth-modpack-format-mrpack)
- [CurseForge modpack ZIP format](https://support.curseforge.com/support/solutions/articles/9000198500-exporting-a-modpack-for-curseforge-project-submission)
