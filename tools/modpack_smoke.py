#!/usr/bin/env python3
# Copyright (C) 2026 ppm203. SPDX-License-Identifier: GPL-3.0-only
"""Opt-in real Modrinth pack installation through the C ABI; never launches Minecraft.

All writes use a fresh temporary directory. --cache-root reads existing assets
and libraries, linking them only into the temporary test tree. Source bytes and
permissions are checked again before exit. Invoking this script permits network
downloads of the pinned pack, Minecraft, its loader, and Java if needed.
"""
import argparse
import ctypes
import hashlib
import json
import os
from pathlib import Path
import stat
import subprocess
import tempfile
import time
from urllib.parse import urlsplit
import zipfile


VERSION = "dN5QXu6n"
PROJECT = "BYN9yKrV"
ARCHIVE_SHA512 = (
    "8321906b06b3b8b675f45cc3da09e05dbba6613c336c958bc4eff76840b23f677"
    "96dbae742a7ea8effe8a782b276678f9581559c6fe6cacc34edcf7b910d8015"
)
ARCHIVE_SIZE = 5593
USER_AGENT = "SECL-modpack-smoke/0.1 (opt-in acceptance test)"


def require(condition, message):
    if not condition:
        raise RuntimeError(message)


def digest(path, algorithm="sha512"):
    result = hashlib.new(algorithm)
    with path.open("rb") as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b""):
            result.update(chunk)
    return result.hexdigest()


def curl_file(url, target, limit=1024 * 1024):
    subprocess.run(
        ["curl", "--fail", "--silent", "--show-error", "--location",
         "--proto", "=https", "--proto-redir", "=https", "--max-redirs", "3",
         "--connect-timeout", "15", "--max-time", "60", "--max-filesize", str(limit),
         "--user-agent", USER_AGENT, "--output", str(target), url],
        check=True, timeout=70,
    )
    require(target.stat().st_size <= limit, "Network response exceeded size limit")


def snapshot(path):
    details = path.stat()
    return (details.st_size, details.st_mtime_ns, stat.S_IMODE(details.st_mode),
            digest(path, "sha256"))


def link_file(source, target, temporary):
    require(target.is_relative_to(temporary), "Seed destination escaped the test directory")
    require(source.is_file() and not source.is_symlink(), "Seed source is not a regular file")
    require(not target.exists() and not target.is_symlink(), "Seed destination already exists")
    target.parent.mkdir(parents=True, exist_ok=True)
    os.link(source, target)


def seed_tree(source, target, temporary, observed=None):
    """Never chmod, truncate, replace, or delete a source file or source directory."""
    if not source.exists():
        return 0
    require(source.is_dir() and not source.is_symlink(), "Seed directory must not be a symlink")
    count = 0
    for folder, directories, files in os.walk(source, followlinks=False):
        folder = Path(folder)
        directories[:] = [name for name in directories if not (folder / name).is_symlink()]
        for name in files:
            path = folder / name
            if path.is_symlink() or not path.is_file() or ".part-" in name or ".tmp-" in name:
                continue
            if observed is not None:
                observed[path] = snapshot(path)
            link_file(path, target / path.relative_to(source), temporary)
            count += 1
    return count


class Native:
    def __init__(self, library, source, timeout):
        self.library = ctypes.CDLL(str(library))
        self.library.solar_command.argtypes = [ctypes.c_char_p]
        self.library.solar_command.restype = ctypes.c_void_p
        self.library.solar_free.argtypes = [ctypes.c_void_p]
        self.library.solar_free.restype = None
        self.source, self.timeout = source, timeout

    def call(self, request):
        pointer = self.library.solar_command(json.dumps(request).encode("utf-8"))
        require(bool(pointer), "C ABI returned a null response")
        try:
            return json.loads(ctypes.string_at(pointer))
        finally:
            self.library.solar_free(pointer)

    def run(self, operation, expected_error=None, **request):
        print(operation, flush=True)
        state = self.call({"action": "engine", "request": {
            "op": operation, "source": self.source, **request}})
        deadline, next_progress = time.monotonic() + self.timeout, time.monotonic() + 20
        while state.get("busy"):
            if time.monotonic() >= deadline:
                self.call({"action": "cancel"})
                raise RuntimeError("Bounded acceptance timeout: " + operation)
            if time.monotonic() >= next_progress:
                print("progress:", state.get("progress"), flush=True)
                next_progress = time.monotonic() + 20
            time.sleep(0.1)
            state = self.call({"action": "poll"})
        error = state.get("error")
        if expected_error is not None:
            require(error and error.get("code") == expected_error,
                    "Expected %s, received %s" % (expected_error, state))
            return error
        require(not error, json.dumps(error, ensure_ascii=False))
        return state.get("result")


def prepared_root(root, archive_hash):
    return root / ".solar-mrpack" / archive_hash / "game-root"


def check_install(native, root, identifier, installed, archive, index):
    game = root / "instances" / identifier
    version = root / "versions" / identifier
    require(installed["committed"] is True, "Import did not report a completed commit")
    require(Path(installed["directory"]) == game, "Returned game directory is not final")
    metadata = json.loads((version / (identifier + ".json")).read_text())
    settings = json.loads((version / ".solar-instance.json").read_text())
    require(metadata["id"] == identifier and metadata["_baseId"] == "1.20.1", "Wrong installed game")
    require(Path(metadata["_solarGameDirectory"]) == game and settings["isolated"] is True,
            "Import did not persist an isolated final directory")
    for value in (metadata, settings, installed["java"]):
        require(".solar-mrpack" not in json.dumps(value), "Published value retains a staging path")
    require(Path(installed["java"]["path"]).is_file(), "Final Java path is unavailable")
    client = version / (identifier + ".jar")
    require(digest(client, "sha1") == metadata["downloads"]["client"]["sha1"], "Client hash differs")
    with zipfile.ZipFile(archive) as pack:
        overrides = {}
        for prefix in ("overrides/", "client-overrides/"):
            for name in pack.namelist():
                if name.startswith(prefix) and not name.endswith("/"):
                    overrides[name[len(prefix):]] = pack.read(name)
        for relative, contents in overrides.items():
            require((game / relative).read_bytes() == contents, "Override differs: " + relative)
    for file in index["files"]:
        if file.get("env", {}).get("client", "required") == "unsupported":
            continue
        target = game / file["path"]
        require(target.is_file() and target.stat().st_size == file["fileSize"], "Pack file missing")
        for algorithm in ("sha1", "sha512"):
            require(digest(target, algorithm) == file["hashes"][algorithm], "Pack file hash differs")
    inventory = native.run("instances.scan", roots=[str(root)])
    instance = next(item for item in inventory["instances"] if item["id"] == identifier)
    require(instance["version"] == "1.20.1" and instance["loader"] == "fabric" and
            instance["loader_version"] == "0.15.9" and instance["isolated"] is True and
            Path(instance["game_directory"]) == game, "Scanned instance differs from the pack")
    require(not inventory["warnings"], "Instance scan produced warnings")
    return inventory


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("library", type=Path, help="Built solar_core shared library")
    parser.add_argument("--cache-root", type=Path, help="Read-only source of assets/ and libraries/ caches")
    parser.add_argument("--java", type=Path, help="Optional existing Java 17 executable")
    parser.add_argument("--source", default="official", choices=("official", "auto", "bmclapi"))
    parser.add_argument("--timeout", type=int, default=1200, help="Maximum seconds per native operation")
    args = parser.parse_args()
    library = args.library.resolve(strict=True)
    cache_source = args.cache_root.resolve(strict=True) if args.cache_root else None
    java = args.java.resolve(strict=True) if args.java else None
    require(args.timeout > 0, "Timeout must be positive")
    observed = {}
    with tempfile.TemporaryDirectory(prefix="secl-mrpack-live-") as directory:
        temporary = Path(directory).resolve()
        os.environ["SOLAR_DATA_DIR"] = str(temporary / "settings")
        api = temporary / "version.json"
        curl_file("https://api.modrinth.com/v2/version/" + VERSION, api)
        release = json.loads(api.read_text())
        require(release["id"] == VERSION and release["project_id"] == PROJECT, "Wrong API release")
        descriptor = next(file for file in release["files"] if file["primary"])
        require(descriptor["hashes"]["sha512"] == ARCHIVE_SHA512 and descriptor["size"] == ARCHIVE_SIZE,
                "Pinned public archive metadata changed")
        address = urlsplit(descriptor["url"])
        require(address.scheme == "https" and address.hostname == "cdn.modrinth.com" and
                address.username is None and address.password is None and address.port is None and
                address.path.startswith("/data/" + PROJECT + "/versions/" + VERSION + "/"),
                "Archive URL is not the pinned official CDN location")
        archive = temporary / "Adrenaline.mrpack"
        curl_file(descriptor["url"], archive)
        require(archive.stat().st_size == ARCHIVE_SIZE and digest(archive) == ARCHIVE_SHA512,
                "Pinned archive bytes did not verify")
        with zipfile.ZipFile(archive) as pack:
            index = json.loads(pack.read("modrinth.index.json"))
        require(len(index["files"]) == 14 and index["dependencies"] == {
            "fabric-loader": "0.15.9", "minecraft": "1.20.1"}, "Pinned pack contents changed")
        root = temporary / "game"
        prepared = prepared_root(root, ARCHIVE_SHA512)
        seeded = 0
        if cache_source:
            for name in ("assets", "libraries"):
                seeded += seed_tree(cache_source / name, prepared / name, temporary, observed)
        native = Native(library, args.source, args.timeout)
        try:
            if java:
                runtime = native.run("java.probe", path=str(java))
                require(runtime and runtime["major"] == 17, "--java must run Java 17")
            info = native.run("modpacks.inspect", path=str(archive))
            require(len(info["files"]) == 14 and info["override_count"] == 10 and
                    info["archive_sha512"] == ARCHIVE_SHA512, "Unexpected inspected pack")
            request = {"path": str(archive), "root": str(root), "id": "adrenaline-check"}
            if java:
                request["java"] = str(java)
            plan = native.run("modpacks.plan", **request)
            require(len(plan["files"]) == 14 and plan["override_count"] == 10,
                    "Real pack plan did not select all expected files")
            require(not native.run("instances.scan", roots=[str(root)])["instances"],
                    "Prepared cache was visible as an instance")
            installed = native.run("modpacks.apply", plan=plan)
            inventory = check_install(native, root, request["id"], installed, archive, index)
            require(len(inventory["instances"]) == 1, "Real pack did not publish exactly one instance")
            native.run("modpacks.plan", expected_error="instance_exists", **request)

        finally:
            for source, before in observed.items():
                require(snapshot(source) == before, "Source cache bytes or permissions changed: " + str(source))
        print(json.dumps({"pinned_version": VERSION, "archive_sha512": ARCHIVE_SHA512,
            "downloaded_files": 14, "common_overrides": 10,
            "client_overlay": "covered by offline integration fixtures; absent from this public pack",
            "duplicate_rejected": True, "seeded_files": seeded,
            "source_cache_unchanged": True, "game_launched": False,
            "root": str(root), "directory": installed["directory"], "java": installed["java"]["path"],
            "temporary_files": "removed on exit"}), flush=True)


if __name__ == "__main__":
    main()
