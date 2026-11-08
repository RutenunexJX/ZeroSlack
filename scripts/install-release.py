"""Install one verified ZeroSlack package, preserving extras and recoverable backups.

Parameterized from the verified 0.33.5 installer. This command never pushes Git,
changes a version, removes a backup, or authorizes an installation by itself.
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

from release_gate import absolute, tree_files, verify_checksums, verify_package
from test_policy import PolicyError, digest, dump, load_policy, require, safe_path, stamp


def bounded(path,root):
    path=Path(path).absolute(); root=Path(root).absolute()
    require(path.is_relative_to(root),f'Path escapes installation owner: {path}')
    current=Path(path.anchor)
    for part in path.parts[1:]:
        current/=part
        require(not current.is_symlink() and not current.is_junction(),f'Path redirection: {current}')
    require(path.resolve().is_relative_to(root.resolve()),f'Resolved path escapes owner: {path}')
    return path


def inventory(root,policy):
    bounded(root,root)
    return {name:digest(path) for name,path in tree_files(root,policy)}


def assert_idle(target):
    require(os.name=='nt','Process ownership checking currently requires Windows')
    kernel=ctypes.WinDLL('kernel32',use_last_error=True)
    class Entry(ctypes.Structure):
        _fields_=[('size',W.DWORD),('usage',W.DWORD),('pid',W.DWORD),('heap',ctypes.c_size_t),
                  ('module',W.DWORD),('threads',W.DWORD),('parent',W.DWORD),('priority',W.LONG),
                  ('flags',W.DWORD),('name',W.WCHAR*260)]
    kernel.CreateToolhelp32Snapshot.argtypes=[W.DWORD,W.DWORD];kernel.CreateToolhelp32Snapshot.restype=W.HANDLE
    kernel.Process32FirstW.argtypes=[W.HANDLE,ctypes.POINTER(Entry)]
    kernel.Process32NextW.argtypes=[W.HANDLE,ctypes.POINTER(Entry)]
    kernel.OpenProcess.argtypes=[W.DWORD,W.BOOL,W.DWORD];kernel.OpenProcess.restype=W.HANDLE
    kernel.QueryFullProcessImageNameW.argtypes=[W.HANDLE,W.DWORD,W.LPWSTR,ctypes.POINTER(W.DWORD)]
    kernel.CloseHandle.argtypes=[W.HANDLE]
    snapshot=kernel.CreateToolhelp32Snapshot(2,0)
    require(snapshot!=W.HANDLE(-1).value,'Process inventory unavailable')
    entry=Entry();entry.size=ctypes.sizeof(entry);prefix=str(target.resolve()).casefold()+'\\'
    try:
        more=kernel.Process32FirstW(snapshot,ctypes.byref(entry))
        while more:
            process=kernel.OpenProcess(0x1000,False,entry.pid)
            path=None
            if process:
                try:
                    size=W.DWORD(32768);buffer=ctypes.create_unicode_buffer(size.value)
                    if kernel.QueryFullProcessImageNameW(process,0,buffer,ctypes.byref(size)):path=buffer.value
                finally:kernel.CloseHandle(process)
            require(not (path and path.casefold().startswith(prefix)),f'Package in use by PID {entry.pid}: {path}')
            require(path or entry.name.casefold() not in {'zeroslack.exe','zeroslack-cli.exe'},
                    f'Cannot establish ownership of running {entry.name}; close normally before replacement')
            more=kernel.Process32NextW(snapshot,ctypes.byref(entry))
    finally:kernel.CloseHandle(snapshot)


def verify_smoke(path,package):
    from test_policy import read_json
    result=read_json(path)
    require(result.get('status')=='passed' and result.get('guiVerified') is True,'Complete staged GUI/CLI/component smoke evidence required')
    require(absolute(result['package'])==absolute(package)
            and digest(Path(package)/'SHA256SUMS.txt')==result['packageChecksumsSha256'],'Smoke belongs to another package')
    require(result.get('logs') and all(digest(p)==h for p,h in result['logs'].items()),'Smoke logs missing/changed')


def install(package,suite,backup,work,smoke,post_verify=None,checkpoint=None):
    from test_policy import read_json
    package=absolute(package);suite=absolute(suite);backup=absolute(backup);work=absolute(work)
    target=suite/'Apps/ZeroSlack-win64';ready=work/'ready';failed=work/'failed-package'
    receipt=verify_package(package);policy=load_policy(Path(receipt['sourceRoot'])/'test_sv/test-execution-policy.json')
    verify_smoke(smoke,package)
    for path in [package,suite,target,backup,work]:bounded(path,path)
    for path in [backup,work]:
        require(not path.exists(),f'Installation output must be new: {path}')
        require(not path.is_relative_to(suite) and not suite.is_relative_to(path), 'Backup/work must be outside the suite')
        require(not path.is_relative_to(package) and not package.is_relative_to(path),'Output overlaps candidate')
    require(not backup.is_relative_to(work) and not work.is_relative_to(backup),'Backup and work must be disjoint')
    assert_idle(target)
    old=inventory(target,policy); owned=verify_checksums_owned(target,policy)
    expected=inventory(package,policy);extras={n:h for n,h in old.items() if n not in owned and n!='SHA256SUMS.txt'}
    require(not set(extras)&set(expected),'Candidate would overwrite an unknown/user file')
    require(not any(n.startswith(('.zeroslack/','profile/','user-data/')) for n in expected),'Package claims user data')
    oldinfo=read_json(target/'build-info.json');info=read_json(package/'build-info.json')
    def version(v):return tuple(int(x) for x in v.split('.'))
    require(version(info['nativeComponents']['xips']['version'])>=version(oldinfo['nativeComponents']['xips']['version']),
            'Native component downgrade refused')
    require(info['assetAdapter']['coreVersion']==oldinfo['assetAdapter']['coreVersion'],'Pinned independent adapter changed')
    require(version(info['suiteSdkVersion'])>=version(oldinfo['suiteSdkVersion']),'SDK downgrade refused')
    indexes={name:(suite/name).read_bytes() for name in ['suite-manifest.json','SHA256SUMS.txt']}
    manifest=json.loads(indexes['suite-manifest.json'].decode('utf-8-sig'))
    components=[x for x in manifest['components'] if x['id']=='zeroslack'];require(len(components)==1,'Ambiguous suite ownership')
    require(components[0]['version']==oldinfo['version'],'Suite index does not match old package')
    updated=json.loads(json.dumps(manifest));item=next(x for x in updated['components'] if x['id']=='zeroslack')
    for key,value in info.items():
        if key in item or key in {'preparationEvidenceSha256','suiteGuiProvider','suiteCliClient'}:item[key]=value
    updated['generatedUtc']=stamp()
    work.mkdir(parents=True);backup.mkdir(parents=True)
    for name,value in indexes.items():(backup/name).write_bytes(value)
    shutil.copytree(package,ready)
    for name in extras:
        source=bounded(target/name,target);destination=bounded(ready/name,ready)
        destination.parent.mkdir(parents=True,exist_ok=True);shutil.copy2(source,destination)
    require(inventory(ready,policy)=={**expected,**extras},'Staging copy mismatch')
    assert_idle(target)
    require(inventory(target,policy)==old,'Concurrent installed-package change')
    require(all((suite/n).read_bytes()==v for n,v in indexes.items()),'Concurrent suite-index change')
    result={'status':'running','package':str(package),'target':str(target),'backup':str(backup),
            'preservedExtras':extras,'startedUtc':stamp()}
    moved=False;installed=False
    current_indexes=dict(indexes)
    def replace_index(name,content):
        require((suite/name).read_bytes()==current_indexes[name],'Concurrent suite-index change: '+name)
        temporary=work/(name+'.next');temporary.write_bytes(content)
        os.replace(temporary,suite/name)
        current_indexes[name]=content
    def mark(phase):
        if checkpoint:checkpoint(phase)  # Dependency injection for isolated failure tests only.
    try:
        bounded(target,suite);bounded(backup/'ZeroSlack-win64',backup)
        target.rename(backup/'ZeroSlack-win64');moved=True
        require(inventory(backup/'ZeroSlack-win64',policy)==old,'Backup verification failed')
        mark('after_backup')
        bounded(ready,work);bounded(target,suite);ready.rename(target);installed=True
        require(inventory(target,policy)=={**expected,**extras},'Installed package mismatch')
        replace_index('suite-manifest.json',(json.dumps(updated,ensure_ascii=False,indent=2)+'\n').encode('utf8'))
        lines=[]
        for line in indexes['SHA256SUMS.txt'].decode('utf-8-sig').splitlines():
            if not line.strip():continue
            _,name=line.split('  ',1);name=name.replace('\\','/')
            if name!='suite-manifest.json' and not name.startswith('Apps/ZeroSlack-win64/'):lines.append(line)
        lines += [h+'  Apps/ZeroSlack-win64/'+n for n,h in sorted(expected.items())]
        lines.append(digest(suite/'suite-manifest.json')+'  suite-manifest.json')
        replace_index('SHA256SUMS.txt',('\n'.join(lines)+'\n').encode('utf8'))
        mark('after_replace')
        if post_verify:
            post_verify(target,work/'post-smoke')
        else:
            command=[sys.executable,'-B',str(Path(__file__).with_name('release_smoke.py')),
                     '--package',str(target),'--output',str(work/'post-smoke'),'--installed-extras']
            process=subprocess.run(command,capture_output=True,creationflags=subprocess.CREATE_NO_WINDOW,timeout=180)
            (work/'post-smoke.log').write_bytes(process.stdout+process.stderr)
            require(process.returncode==0,'Installed package smoke failed')
        require(inventory(target,policy)=={**expected,**extras},'Installed content changed during verification')
        require(inventory(backup/'ZeroSlack-win64',policy)==old,'Backup changed')
        result.update(status='verified',backupVerified=True)
    except BaseException as error:
        result.update(status='rollback_pending',error=str(error))
        try:
            assert_idle(target)
            require(all((suite/n).read_bytes()==v for n,v in current_indexes.items()),
                    'Concurrent suite-index change; preserve it and stop rollback')
            if installed:
                bounded(target,suite);bounded(failed,work);target.rename(failed)
            if moved:
                # Copy instead of consuming the verified, retained backup.
                shutil.copytree(bounded(backup/'ZeroSlack-win64',backup),bounded(target,suite))
            for name,value in indexes.items():(suite/name).write_bytes(value)
            require(inventory(target,policy)==old,'Restored package differs from backup')
            require(all((suite/n).read_bytes()==v for n,v in indexes.items()),'Index rollback mismatch')
            result['status']='rolled_back'
        except BaseException as rollback_error:
            result.update(status='rollback_failed',rollbackError=str(rollback_error))
        raise
    finally:
        result['completedUtc']=stamp();dump(work/'INSTALL.json',result)
    return result


def verify_checksums_owned(package,policy):
    """Validate only old package-owned files; do not reject or erase user extras."""
    owned={}
    for line in (Path(package)/'SHA256SUMS.txt').read_text(encoding='utf-8-sig').splitlines():
        value,name=line.split('  ',1)
        require(name not in owned,'Duplicate owned package path')
        require(digest(safe_path(package,name,policy))==value,'Modified managed file requires review: '+name)
        owned[name]=value
    require(owned and 'build-info.json' in owned and 'ZeroSlack.exe' in owned,'Old package ownership unavailable')
    return owned


if __name__=='__main__':
    parser=argparse.ArgumentParser(description=__doc__)
    for name in ['package','suite','backup','work','smoke']:parser.add_argument('--'+name,required=True)
    args=parser.parse_args()
    try:
        print(json.dumps(install(**vars(args)),ensure_ascii=False))
    except (PolicyError,OSError,ValueError,KeyError,subprocess.TimeoutExpired) as error:
        print('INSTALLATION BLOCKED: '+str(error),file=sys.stderr);sys.exit(2)
