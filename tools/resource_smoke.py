#!/usr/bin/env python3
"""Opt-in real Modrinth downloads in isolated temporary directories; no game launch."""
import ctypes, json, os, pathlib, sys, tempfile, time

with tempfile.TemporaryDirectory(prefix='secl-resource-live-') as temporary:
    os.environ['SOLAR_DATA_DIR'] = temporary + '/settings'
    native = ctypes.CDLL(str(pathlib.Path(sys.argv[1]).resolve()))
    native.solar_command.argtypes = [ctypes.c_char_p]
    native.solar_command.restype = ctypes.c_void_p
    native.solar_free.argtypes = [ctypes.c_void_p]

    def call(request):
        pointer = native.solar_command(json.dumps(request).encode())
        try:
            return json.loads(ctypes.string_at(pointer))
        finally:
            native.solar_free(pointer)

    def run(op, **request):
        print(op, flush=True)
        state = call({'action': 'engine', 'request': {'op': op, **request}})
        deadline = time.monotonic() + 180
        while state.get('busy'):
            if time.monotonic() > deadline:
                call({'action': 'cancel'})
                raise RuntimeError('Bounded smoke timeout: ' + op)
            time.sleep(.05)
            state = call({'action': 'poll'})
        if state.get('error'):
            raise RuntimeError(json.dumps(state['error'], ensure_ascii=False))
        return state.get('result')

    instance = {'id': 'Isolated download verification', 'version': '1.20.1',
                'loader': 'fabric', 'loader_version': '0.16.10', 'java_major': 21,
                'game_directory': temporary + '/game'}
    report = []
    for kind, query in [('mod', 'Comforts'), ('shader', 'MakeUp'), ('resourcepack', 'Default Dark Mode')]:
        found = run('resource.search', type=kind, query=query, version='1.20.1', sort='downloads')
        assert found['hits'], kind
        project = found['hits'][0]
        run('resource.categories', type=kind)
        versions = run('resource.versions', project=project['project_id'], game='1.20.1',
                       loader='fabric' if kind == 'mod' else '')
        assert versions, (kind, project['title'])
        file = next((f for f in versions if f['release_type'] == 'release'), versions[0])
        if kind == 'mod':
            plan = run('mods.plan', instance=instance, file=file)
            selected = [f['project_id'] for f in plan['files'] if not f.get('optional')]
            selected += [r['project_id'] for r in plan['reuse'] if not r['file'].get('optional')]
            selected_plan = run('mods.select', plan=plan, selected=selected)
            run('mods.apply', plan=selected_plan)
            installed = run('mods.list', directory=instance['game_directory'], loader='fabric')
            assert installed
            reused = run('mods.plan', instance=instance, file=file)
            assert any(r['project_id'] == file['project_id'] for r in reused['reuse'])
        else:
            plan = run('packs.plan', instance=instance, file=file, kind=kind)
            run('packs.apply', plan=plan)
            installed = run('packs.list', directory=instance['game_directory'], kind=kind)
            assert any(p.get('sha512') == file['sha512'] for p in installed)
            reused = run('packs.plan', instance=instance, file=file, kind=kind)
            assert reused['reuse'] and run('packs.apply', plan=reused)['reused']
        report.append({'kind': kind, 'project': project['title'], 'file': file['filename'],
                       'installed_count': len(installed)})
    print(json.dumps({'passed': report}, ensure_ascii=False, indent=2), flush=True)
