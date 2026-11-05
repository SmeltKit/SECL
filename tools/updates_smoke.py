#!/usr/bin/env python3
# Copyright (C) 2026 ppm203. SPDX-License-Identifier: GPL-3.0-only
"""Opt-in network acceptance: real Modrinth files in disposable instances only."""
import ctypes, hashlib, json, os, pathlib, subprocess, sys, tempfile, time, urllib.parse

def fetch(url):
    return subprocess.check_output(['curl', '--fail', '--silent', '--show-error',
        '--location', '--connect-timeout', '10', '--max-time', '30',
        '--user-agent', 'SECL/0.1.0 (update acceptance)', url])

def run(library, root):
    os.environ['SOLAR_DATA_DIR'] = str(root/'settings')
    native = ctypes.CDLL(str(pathlib.Path(library).resolve()))
    native.solar_command.argtypes = [ctypes.c_char_p]
    native.solar_command.restype = ctypes.c_void_p
    native.solar_free.argtypes = [ctypes.c_void_p]
    def call(request):
        ptr = native.solar_command(json.dumps(request).encode())
        try: return json.loads(ctypes.string_at(ptr))
        finally: native.solar_free(ptr)
    def command(op, **extra):
        state = call({'action': 'engine', 'request': {'op': op, **extra}})
        deadline = time.monotonic()+180
        while state.get('busy'):
            if time.monotonic() > deadline:
                call({'action':'cancel'}); raise RuntimeError('Bounded update acceptance timeout')
            time.sleep(.05); state=call({'action':'poll'})
        if state.get('error'): raise RuntimeError(json.dumps(state['error'], ensure_ascii=False))
        return state['result']
    assert call({'action':'poll'})['client_id'] == '7ed24288-81a6-4814-8271-25a1529d2e9d'
    for kind, project, folder in [('mod','appleskin','mods'), ('shader','complementary-reimagined','shaderpacks'),
                                   ('resourcepack','fresh-animations','resourcepacks')]:
        query={'game_versions':json.dumps(['1.20.1'])}
        if kind == 'mod': query['loaders']=json.dumps(['fabric'])
        versions=json.loads(fetch('https://api.modrinth.com/v2/project/'+project+'/version?'+urllib.parse.urlencode(query)))
        releases=[v for v in versions if v['version_type']=='release']
        assert len(releases)>1, (project,'requires two published releases for acceptance')
        # Use a recent previous version, reducing unnecessary historical dependencies.
        old=releases[1]; payload=next((f for f in old['files'] if f.get('primary')), old['files'][0])
        data=fetch(payload['url']); assert hashlib.sha512(data).hexdigest()==payload['hashes']['sha512']
        game=root/kind; dest=game/folder; dest.mkdir(parents=True)
        name=payload['filename']+('.disabled' if kind=='mod' else '')
        (dest/name).write_bytes(data)
        if kind == 'mod':
            (dest/'.solar-mods.json').write_text(json.dumps({payload['filename']:{
                'sha512':payload['hashes']['sha512'],'projectId':old['project_id'],'versionId':old['id']}}))
        instance={'game_directory':str(game),'version':'1.20.1','loader':'fabric','loader_version':'0.16.10','java_major':17}
        report=command('updates.check',instance=instance,kind=kind,channel='release')
        row=next(r for r in report['items'] if r['filename']==name)
        assert row['status']=='update', row
        plan=command('updates.plan',report=report,selected=[name])
        if kind=='mod':
            assert plan['files'][0]['install_disabled'] is True
            plan=command('mods.select',plan=plan,selected=[f['project_id'] for f in plan['files'] if not f.get('optional')],redownload=[],enable=[])
        result=command('updates.apply',plan=plan)
        candidate=row['candidate']; newname=candidate['filename']+('.disabled' if kind=='mod' else '')
        assert (dest/newname).is_file() and newname in result['installed']
        assert hashlib.sha512((dest/newname).read_bytes()).hexdigest()==candidate['sha512']
        assert not (dest/name).exists() or name==newname
        if kind=='mod': assert not (dest/candidate['filename']).exists()
        again=command('updates.check',instance=instance,kind=kind,channel='release')
        assert next(r for r in again['items'] if r['filename']==newname)['status']=='current'
        print('PASS',kind,old['version_number'],'->',candidate['version'],'hash verified; old copy removed; repeat check current')
    print('PASS fresh-data default Client ID; all test files isolated and automatically removed')

if __name__ == '__main__':
    with tempfile.TemporaryDirectory(prefix='secl-updates-ffi-') as temp:
        run(sys.argv[1], pathlib.Path(temp))
