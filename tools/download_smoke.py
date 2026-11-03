#!/usr/bin/env python3
# Copyright (C) 2026 ppm203. SPDX-License-Identifier: GPL-3.0-only
"""Opt-in real Java + vanilla game installation, isolated from personal games/accounts."""
import argparse, contextlib, ctypes, hashlib, json, os, pathlib, sys, tempfile, time
p=argparse.ArgumentParser()
p.add_argument('library');p.add_argument('--game',default='1.20.1');p.add_argument('--source',default='auto')
p.add_argument('--java-only',action='store_true');p.add_argument('--major',type=int,default=17)
p.add_argument('--directory', help='Optional isolated test directory, retained to resume after network failure')
a=p.parse_args()
with (contextlib.nullcontext(a.directory) if a.directory else tempfile.TemporaryDirectory(prefix='secl-install-live-')) as temp:
    base=pathlib.Path(temp);os.environ['SOLAR_DATA_DIR']=str(base/'settings')
    lib=ctypes.CDLL(str(pathlib.Path(a.library).resolve()))
    lib.solar_command.argtypes=[ctypes.c_char_p];lib.solar_command.restype=ctypes.c_void_p
    lib.solar_free.argtypes=[ctypes.c_void_p]
    def call(request):
        pointer=lib.solar_command(json.dumps(request).encode())
        try:return json.loads(ctypes.string_at(pointer))
        finally:lib.solar_free(pointer)
    def run(op,timeout=180,**request):
        print(op,flush=True)
        state=call({'action':'engine','request':{'op':op,'source':a.source,**request}})
        deadline=time.monotonic()+timeout;last=time.monotonic()
        while state.get('busy'):
            if time.monotonic()>deadline:
                call({'action':'cancel'});raise RuntimeError('Bounded smoke timeout: '+op)
            if time.monotonic()-last>20:
                print('progress:',state.get('progress'),flush=True);last=time.monotonic()
            time.sleep(.1);state=call({'action':'poll'})
        if state.get('error'): raise RuntimeError(json.dumps(state['error'],ensure_ascii=False))
        return state.get('result')
    host=run('platform')
    manifest=run('game.versions')
    assert {'release','snapshot','special'} <= {v['channel'] for v in manifest['versions']}
    entry=next(v for v in manifest['versions'] if v['id']==a.game)
    # A small native JRE, with provider checksum and executable verification.
    builds=run('java.builds',major=a.major,vendor='zulu',os=host['os'],arch=host['arch'])
    assert builds
    runtime=run('java.install',timeout=300,build=builds[0],directory=str(base/'settings/java-managed'))
    assert runtime['major']==a.major and pathlib.Path(runtime['path']).is_file()
    assert run('java.install',build=builds[0],directory=str(base/'settings/java-managed'))['path']==runtime['path']
    detected=call({'action':'detect_java'})['javas']
    assert runtime['path'] in [j['path'] for j in detected]
    if a.java_only:
        print(json.dumps({'java':runtime,'repeat_install':'reused','discovery':'passed'},ensure_ascii=False),flush=True)
        sys.exit(0)
    for loader in ['fabric','quilt','forge','neoforge']:
        try:
            versions=run('loader.versions',loader=loader,game=a.game)
            assert all(v.get('loader',{}).get('version') for v in versions)
        except RuntimeError as e:
            print('Loader listing not verified:',loader,str(e),flush=True)
    root=base/'game'
    installed=run('game.install',timeout=1200,request={'root':str(root),'id':'download-check',
        'version_entry':entry,'loader':'vanilla','java':runtime['path']})
    metadata=installed['metadata'];assert installed['root']==str(root)
    client=root/'versions/download-check/download-check.jar'
    assert hashlib.sha1(client.read_bytes()).hexdigest()==metadata['downloads']['client']['sha1']
    index=json.loads((root/'assets/indexes'/f"{metadata['assetIndex']['id']}.json").read_text())
    for obj in index['objects'].values():
        h=obj['hash'];f=root/'assets/objects'/h[:2]/h
        assert f.stat().st_size==obj['size'] and hashlib.sha1(f.read_bytes()).hexdigest()==h
    snapshot=call({'action':'scan','root':str(root)})
    assert any(v['id']=='download-check' and v['available'] for v in snapshot['versions'])
    assert snapshot['account'] is None
    denied=call({'action':'launch','version':'download-check','java':runtime['path']})
    assert denied['error']['code']=='login_required'
    print(json.dumps({'java':{k:runtime[k] for k in ['version','vendor','arch']},'game':a.game,
        'asset_count':len(index['objects']),'client_sha1':metadata['downloads']['client']['sha1'],
        'ownership_gate':'preserved','temporary_files':'retained in --directory' if a.directory else 'removed on exit'},ensure_ascii=False),flush=True)
