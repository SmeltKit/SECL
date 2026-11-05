#!/usr/bin/env python3
# Copyright (C) 2026 ppm203. SPDX-License-Identifier: GPL-3.0-only
"""Opt-in Modrinth identity + failure smoke check using disposable mod files."""
import ctypes
import hashlib
import io
import json
import os
import pathlib
import subprocess
import sys
import tempfile
import time
import zipfile


def fetch(url):
    return subprocess.check_output(['curl', '--fail', '--silent', '--show-error',
        '--location', '--connect-timeout', '10', '--max-time', '30',
        '--user-agent', 'SECL/0.1.0 (inventory acceptance)', url])


def run(library, root):
    os.environ['SOLAR_DATA_DIR'] = str(root / 'settings')
    native = ctypes.CDLL(str(pathlib.Path(library).resolve()))
    native.solar_command.argtypes = [ctypes.c_char_p]
    native.solar_command.restype = ctypes.c_void_p
    native.solar_free.argtypes = [ctypes.c_void_p]

    def command(op, **extra):
        def call(request):
            ptr = native.solar_command(json.dumps(request).encode())
            try:
                return json.loads(ctypes.string_at(ptr))
            finally:
                native.solar_free(ptr)
        state = call({'action': 'engine', 'request': {'op': op, **extra}})
        deadline = time.monotonic() + 90
        while state.get('busy'):
            if time.monotonic() > deadline:
                call({'action': 'cancel'})
                raise RuntimeError('Bounded inventory smoke timeout')
            time.sleep(.03)
            state = call({'action': 'poll'})
        if state.get('error'):
            raise RuntimeError(json.dumps(state['error'], ensure_ascii=False))
        return state['result']

    versions = json.loads(fetch('https://api.modrinth.com/v2/project/appleskin/version?'
        'game_versions=%5B%221.20.1%22%5D&loaders=%5B%22fabric%22%5D'))
    version = next(v for v in versions if v['version_type'] == 'release')
    payload = next((f for f in version['files'] if f.get('primary')), version['files'][0])
    data = fetch(payload['url'])
    assert hashlib.sha512(data).hexdigest() == payload['hashes']['sha512']
    game = root / 'instance'
    mods = game / 'mods'
    mods.mkdir(parents=True)
    (mods / payload['filename']).write_bytes(data)
    buffer = io.BytesIO()
    with zipfile.ZipFile(buffer, 'w') as archive:
        archive.writestr('fabric.mod.json', json.dumps({'schemaVersion': 1,
            'id': 'inventory_smoke', 'version': '1.0.0',
            'depends': {'missing_smoke_dependency': '>=1.0.0'}}))
    (mods / 'synthetic.jar').write_bytes(buffer.getvalue())
    instance = {'game_directory': str(game), 'version': '1.20.1',
                'loader': 'fabric', 'loader_version': '0.16.10', 'java_major': 17}

    def rows(report):
        return {r['filename']: r for r in report['items']}

    before = rows(command('instance.inventory', instance=instance, kind='mod'))
    assert before[payload['filename']]['detection']['source'] == 'not_checked'
    assert before['synthetic.jar']['detection']['compatibility'] == 'conflict'
    result = command('mods.enrich', instance=instance)
    assert 'source_error' not in result, result.get('source_error')
    enriched = rows(result)
    assert enriched[payload['filename']]['detection']['source'] == 'matched'
    assert enriched['synthetic.jar']['detection']['source'] == 'unmatched'
    assert enriched['synthetic.jar']['detection']['compatibility'] == 'conflict'
    assert enriched[payload['filename']]['project_id'] == version['project_id']
    print('PASS real SHA-512 source match, unknown source, independent local dependency issue')

    manifest = mods / '.solar-mods.json'
    original = manifest.read_bytes()
    keys = ['HTTPS_PROXY', 'https_proxy', 'ALL_PROXY', 'all_proxy', 'NO_PROXY', 'no_proxy']
    saved = {k: os.environ.get(k) for k in keys}
    try:
        for key in keys:
            os.environ.pop(key, None)
        os.environ['HTTPS_PROXY'] = 'http://127.0.0.1:1'
        failed = command('mods.enrich', instance=instance)
        assert failed.get('source_error'), failed
        failed_rows = rows(failed)
        assert len(failed_rows) == 2 and failed_rows['synthetic.jar']['known']
        assert failed_rows[payload['filename']]['detection']['source'] == 'matched'
        assert failed_rows['synthetic.jar']['detection']['source'] == 'failed'
        assert manifest.read_bytes() == original
    finally:
        for key, value in saved.items():
            os.environ.pop(key, None)
            if value is not None:
                os.environ[key] = value
    print('PASS unavailable network retains local rows and verified identities; index unchanged')
    cached = rows(command('instance.inventory', instance=instance, kind='mod'))
    assert cached[payload['filename']]['detection']['source'] == 'matched'
    with zipfile.ZipFile(buffer, 'a') as archive:
        archive.writestr('new.txt', 'changed content')
    (mods / payload['filename']).write_bytes(buffer.getvalue())
    changed = rows(command('instance.inventory', instance=instance, kind='mod', fresh=True))
    assert changed[payload['filename']]['detection']['source'] == 'not_checked'
    print('PASS changed file cannot inherit stale website identity; all files isolated and removed')


if __name__ == '__main__':
    with tempfile.TemporaryDirectory(prefix='secl-inventory-ffi-') as temporary:
        run(sys.argv[1], pathlib.Path(temporary))
