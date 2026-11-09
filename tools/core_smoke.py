#!/usr/bin/env python3
"""Exercise the real C ABI in an isolated config directory, with no credentials."""
import argparse, ctypes, json, os, pathlib, tempfile, time
parser=argparse.ArgumentParser(description=__doc__)
parser.add_argument('library',type=pathlib.Path)
parser.add_argument('--expect-embedded',choices=('yes','no'))
args=parser.parse_args()
# Prove the packaged core's Key setting independently of runtime overrides.
os.environ.pop('SECL_CURSEFORGE_API_KEY',None)
os.environ.pop('SOLAR_CORE_LIBRARY',None)
with tempfile.TemporaryDirectory(prefix='solar-ffi-') as temporary:
 os.environ['SOLAR_DATA_DIR']=temporary
 native=ctypes.CDLL(str(args.library.resolve()))
 native.solar_command.argtypes=[ctypes.c_char_p];native.solar_command.restype=ctypes.c_void_p
 native.solar_free.argtypes=[ctypes.c_void_p]
 def call(request):
  ptr=native.solar_command(json.dumps(request).encode())
  if not ptr: raise RuntimeError('Native allocation failed')
  try:return json.loads(ctypes.string_at(ptr))
  finally:native.solar_free(ptr)
 assert native.solar_abi_version()==1
 for source in ['official','bmclapi','auto']:
  state=call({'action':'configure','download_source':source})
  assert state['download_source']==source
  assert json.loads((pathlib.Path(temporary)/'settings.json').read_text())['download_source']==source
 invalid=call({'action':'configure','download_source':'unknown'})
 assert invalid['error']['code']=='invalid_source'
 assert call({'action':'clear_error'})['download_source']=='auto'
 assert pathlib.Path(call({'action':'poll'})['java_directory']).parent==pathlib.Path(temporary)
 assert call({'action':'poll'})['account'] is None
 denied=call({'action':'launch','version':'anything','java':'/invalid'})
 assert denied['error']['code']=='login_required'
 assert call({'action':'poll'})['client_id']=='7ed24288-81a6-4814-8271-25a1529d2e9d'
 call({'action':'configure','client_id':''})
 denied=call({'action':'login'});assert denied['error']['code']=='client_id_missing'
 call({'action':'configure','client_id':'7ed24288-81a6-4814-8271-25a1529d2e9d'})
 state=call({'action':'engine','request':{'op':'capabilities'}})
 deadline=time.monotonic()+5
 while state['busy'] and time.monotonic()<deadline:
  time.sleep(.02);state=call({'action':'poll'})
 assert not state['busy'] and 'mods.plan' in state['result']
 assert all('offline' not in x for x in state['result'])
 if args.expect_embedded is not None:
  assert 'build.info' in state['result']
  state=call({'action':'engine','request':{'op':'build.info'}})
  deadline=time.monotonic()+5
  while state['busy'] and time.monotonic()<deadline:
   time.sleep(.02);state=call({'action':'poll'})
  assert not state['busy'] and state['error'] is None
  assert state['result']['curseforge_key_embedded'] is (args.expect_embedded=='yes'), 'Wrong embedded CurseForge Key setting'
  print('PASS: packaged core CurseForge Key embedded:',args.expect_embedded)
 state=call({'action':'detect_java'})
 assert state['account'] is None and state['error'] is None
 print('PASS: C ABI, allocation ownership, login gates, async engine dispatch and local Java detection')
 print('Local Java runtimes:',len(state['javas']))
