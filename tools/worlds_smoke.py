#!/usr/bin/env python3
# Copyright (C) 2026 ppm203. SPDX-License-Identifier: GPL-3.0-only
"""Local-only FFI world acceptance, using generated fixtures, never real saves."""
import ctypes, gzip, hashlib, json, os, pathlib, struct, sys, tempfile, time, zipfile

def nbt(name):
    def text(s):
        b=s.encode('utf-8'); return struct.pack('>H',len(b))+b
    def string(k,v): return b'\x08'+text(k)+text(v)
    def compound(k,v):return b'\x0a'+text(k)+v+b'\x00'
    data=string('LevelName',name)+b'\x04'+text('LastPlayed')+struct.pack('>q',1793700000000)
    data+=b'\x03'+text('GameType')+struct.pack('>i',0)
    data+=compound('Version',string('Name','1.20.1'))
    return gzip.compress(compound('',compound('Data',data)))

def run_test(library, root):
    root=pathlib.Path(root); os.environ['SOLAR_DATA_DIR']=str(root/'settings')
    native=ctypes.CDLL(str(pathlib.Path(library).resolve()))
    native.solar_command.argtypes=[ctypes.c_char_p]; native.solar_command.restype=ctypes.c_void_p
    native.solar_free.argtypes=[ctypes.c_void_p]
    def call(request):
        p=native.solar_command(json.dumps(request).encode())
        try:return json.loads(ctypes.string_at(p))
        finally:native.solar_free(p)
    def command(op,instance,**extra):
        s=call({'action':'engine','request':{'op':'worlds.'+op,'instance':instance,**extra}})
        end=time.monotonic()+30
        while s.get('busy'):
            if time.monotonic()>end:
                call({'action':'cancel'});raise RuntimeError('Bounded world smoke timeout')
            time.sleep(.01);s=call({'action':'poll'})
        if s.get('error'):raise RuntimeError(json.dumps(s['error'],ensure_ascii=False))
        return s['result']
    game=root/'game'; (game/'saves/source/region').mkdir(parents=True)
    level=nbt('存档功能测试 · Synthetic fixture')
    (game/'saves/source/level.dat').write_bytes(level)
    (game/'saves/source/region/r.0.0.mca').write_bytes(bytes(range(256))*4096)
    instance={'game_directory':str(game),'version':'1.20.1','loader':'fabric'}
    assert command('list',instance)[0]['name']=='存档功能测试 · Synthetic fixture'
    command('export',instance,folder='source',path=str(root/'export.zip'))
    with zipfile.ZipFile(root/'export.zip') as z:
        assert z.testzip() is None and z.read('level.dat')==level
    with zipfile.ZipFile(root/'external.zip','w',compression=zipfile.ZIP_DEFLATED) as z:
        z.writestr('wrapper/world/level.dat',level);z.writestr('wrapper/world/data/test.dat',b'hello')
    command('import',instance,path=str(root/'external.zip'),name='imported')
    assert (game/'saves/imported/data/test.dat').read_bytes()==b'hello'
    backup=command('backup',instance,folder='source')
    command('restore',instance,archive=backup['archive'],name='restored')
    command('copy',instance,folder='source',name='copy')
    assert (game/'saves/restored/level.dat').read_bytes()==level
    assert (game/'saves/copy/level.dat').read_bytes()==level
    # This is a disposable generated fixture. The production Trash path is used.
    command('remove',instance,folder='copy')
    assert not (game/'saves/copy').exists()
    assert (game/'saves/source/level.dat').read_bytes()==level
    assert not list(game.glob('.secl-world-stage-*'))
    print('PASS: real FFI list/export/independent ZIP validation/import/backup/restore/copy/Trash; original unchanged.')

if __name__=='__main__':
    with tempfile.TemporaryDirectory(prefix='secl-worlds-ffi-') as temp:
        run_test(sys.argv[1],temp)
