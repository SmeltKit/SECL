#!/usr/bin/env python3
# Copyright (C) 2026 ppm203. SPDX-License-Identifier: GPL-3.0-only
"""Build the Qt-free C++ core and Flutter 3.19.6 desktop shell."""
import argparse, json, os, pathlib, platform, shutil, subprocess
root=pathlib.Path(__file__).resolve().parent.parent
p=argparse.ArgumentParser()
p.add_argument('--flutter',default=os.environ.get('FLUTTER','flutter'))
p.add_argument('--test',action='store_true')
p.add_argument('--debug',action='store_true',help='JIT desktop build; avoids old Intel-only AOT tools on Apple Silicon')
args=p.parse_args()
def run(*cmd,**kw):subprocess.run([str(x) for x in cmd],check=True,**kw)
version=json.loads(subprocess.check_output([args.flutter,'--version','--machine']))
if version['frameworkVersion']!='3.19.6':raise SystemExit('Use the pinned Flutter 3.19.6 SDK, not another SDK.')
native=root/'build/native'
config=['cmake','-S',root,'-B',native,'-DCMAKE_BUILD_TYPE=Release','-DBUILD_TESTING='+('ON' if args.test else 'OFF')]
if platform.system()=='Darwin':config+=['-DCMAKE_OSX_ARCHITECTURES='+platform.machine(),'-DCMAKE_OSX_DEPLOYMENT_TARGET=12.0']
run(*config);run('cmake','--build',native,'--config','Release','--parallel','3')
if args.test:run('ctest','--test-dir',native,'-C','Release','--output-on-failure')
run(args.flutter,'pub','get',cwd=root/'app')
if args.test:
 run(args.flutter,'analyze','--no-pub',cwd=root/'app');run(args.flutter,'test','--no-pub',cwd=root/'app')
system={'Darwin':'macos','Windows':'windows','Linux':'linux'}[platform.system()]
mode='Debug' if args.debug else 'Release'
run(args.flutter,'build',system,'--'+mode.lower(),'--no-pub',cwd=root/'app')
if system=='macos':
 bundle=root/'app/build/macos/Build/Products'/mode/'SECL.app'
 dest=bundle/'Contents/Frameworks/libsolar_core.dylib';shutil.copy2(native/'core/libsolar_core.dylib',dest)
 run('codesign','--force','--sign','-',dest);run('codesign','--force','--deep','--sign','-','--entitlements',root/'app/macos/Runner'/('DebugProfile.entitlements' if args.debug else 'Release.entitlements'),bundle)
 print('Built:',bundle)
