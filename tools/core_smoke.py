#!/usr/bin/env python3
"""Exercise the real C ABI in an isolated config directory, with no credentials."""
import ctypes, json, os, pathlib, sys, tempfile, time
with tempfile.TemporaryDirectory(prefix='solar-ffi-') as temporary:
 os.environ['SOLAR_DATA_DIR']=temporary
 native=ctypes.CDLL(str(pathlib.Path(sys.argv[1]).resolve()))
 native.solar_command.argtypes=[ctypes.c_char_p];native.solar_command.restype=ctypes.c_void_p
 native.solar_free.argtypes=[ctypes.c_void_p]
 def call(request):
  ptr=native.solar_command(json.dumps(request).encode())
  if not ptr: raise RuntimeError('Native allocation failed')
  try:return json.loads(ctypes.string_at(ptr))
  finally:native.solar_free(ptr)
 assert native.solar_abi_version()==1
 assert call({'action':'poll'})['account'] is None
 denied=call({'action':'launch','version':'anything','java':'/invalid'})
 assert denied['error']['code']=='login_required'
 denied=call({'action':'login'});assert denied['error']['code']=='client_id_missing'
 state=call({'action':'engine','request':{'op':'capabilities'}})
 deadline=time.monotonic()+5
 while state['busy'] and time.monotonic()<deadline:
  time.sleep(.02);state=call({'action':'poll'})
 assert not state['busy'] and 'mods.plan' in state['result']
 assert all('offline' not in x for x in state['result'])
 state=call({'action':'detect_java'})
 assert state['account'] is None and state['error'] is None
 print('PASS: C ABI, allocation ownership, login gates, async engine dispatch and local Java detection')
 print('Local Java runtimes:',len(state['javas']))
