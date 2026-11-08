"""Verify the actual package in an isolated profile and DLL search environment.

--gui waits for the isolated window to be closed normally by the operator or
Computer Use. CLI/native checks run automatically; no user process is terminated.
"""
import argparse
import ctypes
from ctypes import wintypes as W
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import time
import xml.etree.ElementTree as ET

from release_gate import absolute, verify_package
from test_policy import digest, dump, require, stamp


def module_paths(pid):
    kernel=ctypes.WinDLL('kernel32',use_last_error=True);psapi=ctypes.WinDLL('psapi',use_last_error=True)
    kernel.OpenProcess.argtypes=[W.DWORD,W.BOOL,W.DWORD];kernel.OpenProcess.restype=W.HANDLE
    kernel.CloseHandle.argtypes=[W.HANDLE]
    psapi.EnumProcessModulesEx.argtypes=[W.HANDLE,ctypes.POINTER(W.HMODULE),W.DWORD,ctypes.POINTER(W.DWORD),W.DWORD]
    psapi.GetModuleFileNameExW.argtypes=[W.HANDLE,W.HMODULE,W.LPWSTR,W.DWORD]
    handle=kernel.OpenProcess(0x410,False,pid);require(handle,'Cannot inspect isolated GUI modules')
    try:
        modules=(W.HMODULE*2048)();needed=W.DWORD()
        require(psapi.EnumProcessModulesEx(handle,modules,ctypes.sizeof(modules),ctypes.byref(needed),3),'Cannot enumerate GUI modules')
        result=[]
        for module in modules[:needed.value//ctypes.sizeof(W.HMODULE)]:
            buffer=ctypes.create_unicode_buffer(32768)
            if psapi.GetModuleFileNameExW(handle,module,buffer,len(buffer)):result.append(buffer.value)
        return result
    finally:kernel.CloseHandle(handle)


def smoke(package,output,gui=False,installed_extras=False):
    package=absolute(package);output=absolute(output)
    require(not output.exists(),'Smoke output must be new')
    receipt=verify_package(package,allow_extras=installed_extras)
    output.mkdir(parents=True);profile=output/'profile';profile.mkdir();workspace=output/'workspace';workspace.mkdir()
    sv=workspace/'release_tb.sv';sv.write_text('module release_tb;\nlogic clk;\ninitial begin\nclk=0;\nend\nendmodule\n',encoding='utf8')
    env={k:v for k,v in os.environ.items() if not k.startswith(('QT_','ZEROSLACK_','XIPS_','SIMDOCK_'))}
    env.update(PATH=str(package)+';C:/Windows/System32;C:/Windows',QT_PLUGIN_PATH=str(package),QT_QPA_PLATFORM='offscreen',
               ZEROSLACK_PROFILE_ROOT=str(profile),ZEROSLACK_EDITING_TIME_STORAGE_PATH=str(profile/'time.json'),
               ZEROSLACK_SIMULATION_CACHE_ROOT=str(profile/'simulation'),ZEROSLACK_SIMDOCK_LEGACY_SETTINGS_PATH=str(profile/'legacy.ini'),
               XIPS_LIBRARY=str(profile/'xips'),XIPS_ASSET_ADAPTER_STATE=str(profile/'adapter'))
    for key in ['APPDATA','LOCALAPPDATA','USERPROFILE','TEMP','TMP']:
        path=profile/key;path.mkdir();env[key]=str(path)
    result={'status':'running','package':str(package),'startedUtc':stamp(),'guiVerified':False,
            'packageChecksumsSha256':digest(package/'SHA256SUMS.txt'),'records':[],
            'environment':{'PATH':env['PATH'],'pluginPath':env['QT_PLUGIN_PATH']}}
    running=None
    def command(name,args,runenv=None,timeout=90):
        started=time.monotonic()
        process=subprocess.run(list(map(str,args)),cwd=output,env=runenv or env,capture_output=True,
                               timeout=timeout,creationflags=subprocess.CREATE_NO_WINDOW)
        (output/(name+'.stdout.log')).write_bytes(process.stdout);(output/(name+'.stderr.log')).write_bytes(process.stderr)
        result['records'].append({'name':name,'command':list(map(str,args)),'exitCode':process.returncode,'seconds':time.monotonic()-started})
        require(process.returncode==0,f'Package smoke failed: {name} ({process.returncode})')
        return process.stdout.decode('utf-8-sig')
    cli=package/'zeroslack-cli.exe'
    def api(name,request,session=None):
        request_path=output/(name+'.request.json');dump(request_path,request)
        value=json.loads(command(name,[cli,'api',request_path]+(['--session',session] if session else ['--profile',profile])))
        require(value['ok'],f'Package API failed: {name}: {value}')
        return value['data']
    try:
        info=json.loads((package/'build-info.json').read_text(encoding='utf-8-sig'));version=info['version']
        require(command('gui-version',[package/'ZeroSlack.exe','--version','--isolated-profile',profile/'version']).strip()=='ZeroSlack '+version,'GUI version mismatch')
        caps=json.loads(command('capabilities',[cli,'capabilities']))
        require(caps['ok'] and caps['data']['appVersion']==version,'CLI version mismatch')
        api('template-examples',{'command':'templates.examples'})
        command('format-write',[cli,'format',sv,'--write','--format','json'])
        command('format-check',[cli,'format',sv,'--check','--format','json'])
        api('document-inspect',{'command':'document.inspect','workspace':str(workspace),'file':str(sv)})
        # The existing integration executable resolves native components using
        # applicationDirPath. Copy only the exact verified test and QtTest DLL.
        test=Path(receipt['buildDirectory'])/'native_component_integration_test.exe'
        require(digest(test)==receipt['packageInputs'][test.name],'Native test binary changed')
        temporary={package/test.name:test,package/'Qt6Test.dll':Path(receipt['qtDirectory'])/'bin/Qt6Test.dll'}
        copied={}
        try:
            for target,source in temporary.items():
                require(not target.exists(),'Temporary smoke harness collides with package')
                shutil.copy2(source,target);copied[target]=digest(target)
            native_env=env.copy();native_env.update(PATH='C:/Windows/System32;C:/Windows',
                ZEROSLACK_MANAGED_SETTINGS_ROOT=str(output),XIPS_BROWSER_LIBRARY=str(package/'components/xips/xips-browser.dll'))
            command('native-components',[package/test.name,'-o',str(output/'native.xml')+',xml','-o',str(output/'native.log')+',txt'],native_env)
            incidents=list(ET.parse(output/'native.xml').getroot().iter('Incident'))
            require(incidents and all(x.get('type')=='pass' for x in incidents),'Native component cases failed/skipped')
        finally:
            for target,value in copied.items():
                require(target.resolve().parent==package.resolve() and digest(target)==value,'Temporary harness changed')
                target.unlink()
        if gui:
            gui_env=env.copy();gui_env['QT_QPA_PLATFORM']='windows'
            with (output/'gui.stdout.log').open('wb') as stdout,(output/'gui.stderr.log').open('wb') as stderr:
                running=subprocess.Popen([str(package/'ZeroSlack.exe'),'--isolated-profile',str(profile/'gui'),str(sv)],
                    cwd=output,env=gui_env,stdout=stdout,stderr=stderr)
                session=None;deadline=time.monotonic()+40
                while time.monotonic()<deadline:
                    require(running.poll() is None,'Isolated GUI exited before registration')
                    sessions=json.loads(command('sessions',[cli,'sessions']))['data']['sessions']
                    matches=[x for x in sessions if int(x['processId'])==running.pid]
                    if matches:session=matches[0]['sessionId'];break
                    time.sleep(.25)
                require(session,'Isolated GUI registration timeout')
                state=api('gui-describe',{'command':'session.describe'},session)
                require(state['kind']=='gui' and any(Path(x['file'])==sv and not x['dirty'] for x in state['documents']),'Actual GUI did not open the clean fixture')
                api('gui-inspect',{'command':'document.inspect','workspace':str(workspace),'file':str(sv)},session)
                modules=module_paths(running.pid);dump(output/'gui-modules.json',modules)
                for name in ['libzeroslack_core.dll','libzeroslack_semantic.dll','libzeroslack_documents.dll','Qt6Core.dll','Qt6Widgets.dll','ElaWidgetTools.dll']:
                    paths=[Path(p) for p in modules if Path(p).name.casefold()==name.casefold()]
                    require(len(paths)==1 and paths[0].parent==package,'GUI loaded a development DLL: '+name)
                dump(output/'GUI-READY.json',{'pid':running.pid,'session':session,'package':str(package),'status':'awaiting_normal_close'})
                print('GUI_READY: close only the isolated package window normally; evidence '+str(output/'GUI-READY.json'),flush=True)
                running.wait(timeout=300)
                require(running.returncode==0,'GUI did not close normally')
                result['guiVerified']=True
        verify_package(package,allow_extras=installed_extras)
        result['status']='passed'
    except BaseException as error:
        result.update(status='failed',error=str(error))
        if running and running.poll() is None:
            result['remainingIsolatedGuiPid']=running.pid
        raise
    finally:
        result.update(completedUtc=stamp(),logs={str(p):digest(p) for p in output.iterdir() if p.is_file() and p.suffix in {'.log','.xml','.json'}})
        dump(output/'RESULT.json',result)
    return result


if __name__=='__main__':
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--package',required=True);parser.add_argument('--output',required=True)
    parser.add_argument('--gui',action='store_true');parser.add_argument('--installed-extras',action='store_true')
    try:
        args=parser.parse_args();print(json.dumps(smoke(**vars(args)),ensure_ascii=False))
    except Exception as error:
        print('PACKAGE SMOKE FAILED: '+str(error),file=sys.stderr);sys.exit(2)
