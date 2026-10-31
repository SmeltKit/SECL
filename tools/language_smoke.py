#!/usr/bin/env python3
# Copyright (C) 2026 ppm203. SPDX-License-Identifier: GPL-3.0-only
"""Exercise language preferences across native-core process restarts, in isolation."""
import ctypes, json, os, pathlib, subprocess, sys, tempfile

def load(path):
    lib = ctypes.CDLL(str(path))
    lib.solar_command.argtypes = [ctypes.c_char_p]
    lib.solar_command.restype = ctypes.c_void_p
    lib.solar_free.argtypes = [ctypes.c_void_p]
    def call(request):
        ptr = lib.solar_command(json.dumps(request).encode())
        if not ptr:
            raise RuntimeError('Native allocation failed')
        try:
            return json.loads(ctypes.string_at(ptr))
        finally:
            lib.solar_free(ptr)
    return call

if len(sys.argv) == 4 and sys.argv[2] == '--worker':
    call = load(pathlib.Path(sys.argv[1]).resolve())
    request = json.loads(sys.argv[3])
    snapshot = call(request)
    print(json.dumps({'language': snapshot['language'], 'error': snapshot['error'],
                      'account': snapshot['account']}))
else:
    library = pathlib.Path(sys.argv[1]).resolve()
    with tempfile.TemporaryDirectory(prefix='secl-language-') as folder:
        environment = {**os.environ, 'SOLAR_DATA_DIR': folder}
        def worker(request):
            return json.loads(subprocess.check_output(
                [sys.executable, __file__, str(library), '--worker', json.dumps(request)],
                env=environment))
        assert worker({'action': 'poll'})['language'] == 'system'
        for language in ['zh_Hans', 'zh_Hant', 'en', 'system']:
            snapshot = worker({'action': 'configure', 'language': language})
            assert snapshot['language'] == language and snapshot['account'] is None
            assert worker({'action': 'poll'})['language'] == language
            assert json.loads((pathlib.Path(folder) / 'settings.json').read_text())['language'] == language
        invalid = worker({'action': 'configure', 'language': 'invalid'})
        assert invalid['error']['code'] == 'invalid_language'
        assert worker({'action': 'poll'})['language'] == 'system'
    print('PASS: system default, all three languages, restart persistence, invalid preference rejection')
