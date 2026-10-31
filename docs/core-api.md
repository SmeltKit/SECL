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
