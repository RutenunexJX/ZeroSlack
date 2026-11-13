"""Prepare and verify one immutable candidate; this module never creates a package."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import shutil
import subprocess
import sys
import time

from test_policy import (PolicyError, applicable, digest, dump, external_tool_path, guard, inventory,
                         load_policy, read_json, require, safe_path, stamp)
from validation_process import infrastructure_error, run


PRODUCT_BINARIES = ['ZeroSlack.exe','zeroslack-cli.exe','libzeroslack_core.dll',
                    'libzeroslack_semantic.dll','libzeroslack_documents.dll','ElaWidgetTools.dll','WebView2Loader.dll']
FORMAL_FLAGS = {'BUILD_TESTING':'ON','CMAKE_BUILD_TYPE':'Release','ZEROSLACK_ENABLE_ELA':'ON',
                'ZEROSLACK_SELF_CONTAINED_FIXTURES':'ON','ZEROSLACK_TEST_REAL_NATIVE_COMPONENTS':'ON'}
FORMAL_DEPENDENCIES = ['SuiteApp_DIR','ZEROSLACK_XIPS_COMPONENT_DIR','ZEROSLACK_XIPS_ASSET_ADAPTER_DIR',
                       'ZEROSLACK_TICKX_COMPONENT_DIR','ZEROSLACK_DOCUMENT_EDITORS_DIR','ZEROSLACK_WEBVIEW2_SDK_DIR']
REPOSITORY = Path(__file__).resolve().parent.parent
# These are external delivery inputs, never C++/resource-generation exemptions.
EXTERNAL_FILES = {'README.md','ARCHITECTURE.md','用户手册.md','packaging/ZeroSlack-PACKAGE-README.txt',
                  'docs/cli-automation.md','docs/release-process.md',
                  'scripts/release_gate.py','scripts/ReleasePreparation.ps1','scripts/prepare-release.ps1',
                  'scripts/package-release.ps1','scripts/package-ela.ps1','scripts/install-release.py',
                  'scripts/release_smoke.py','scripts/test_release_reuse.py','scripts/test_policy_contracts.py'}


def absolute(value):
    path = Path(value)
    return (path if path.is_absolute() else REPOSITORY/path).resolve()


def tree_files(root, policy):
    root=Path(root)
    require(not root.is_symlink() and not root.is_junction(),f'Path redirection unsupported: {root}')
    pending=['']
    while pending:
        relative=pending.pop()
        directory=root/relative
        for name in sorted(os.listdir(directory)):
            if name in {'.git','__pycache__'}:
                continue
            child=(Path(relative)/name).as_posix()
            path=safe_path(root,child,policy)
            if path.is_dir():
                pending.append(child)
            elif path.is_file():
                yield child,path


def dependency_identity(plan, policy):
    defines=plan['defines']
    roots={key:Path(defines[key]) for key in FORMAL_DEPENDENCIES}
    roots['SuiteApp_DIR']=roots['SuiteApp_DIR'].parents[2]
    roots['fmt']=Path(defines['FETCHCONTENT_SOURCE_DIR_FMT'])
    result={}
    for key,root in roots.items():
        require(not Path(plan['sourceRoot']).is_relative_to(root),f'Dependency root would traverse the source workspace: {root}')
        result[key]={'root':str(root),'files':{name:digest(path) for name,path in tree_files(root,policy)}}
        require(result[key]['files'],f'Empty dependency input: {key}')
    qt=Path(defines['CMAKE_PREFIX_PATH'])
    runtime={}
    for name,path in tree_files(qt/'bin',policy):
        if path.suffix.lower()=='.dll' or path.name=='windeployqt.exe':
            runtime['bin/'+name]=digest(path)
    for name,path in tree_files(qt/'plugins',policy):
        runtime['plugins/'+name]=digest(path)
    result['qtRuntime']={'root':str(qt),'files':runtime}
    compiler=Path(defines['CMAKE_CXX_COMPILER']).parent.parent
    compiler_runtime={}
    for directory in ['bin','licenses']:
        for name,path in tree_files(compiler/directory,policy):
            if directory=='licenses' or path.suffix.lower()=='.dll':
                compiler_runtime[directory+'/'+name]=digest(path)
    result['compilerRuntime']={'root':str(compiler),'files':compiler_runtime}
    return result


def verify_dependencies(dependencies, policy):
    for key,entry in dependencies.items():
        for name,value in entry['files'].items():
            require(digest(safe_path(entry['root'],name,policy))==value,f'Dependency changed: {key}/{name}')


def cache_values(path):
    values = {}
    for line in Path(path).read_text(encoding='utf8', errors='replace').splitlines():
        match = re.match(r'^([^/#][^:]*):[^=]+=(.*)$', line)
        if match:
            values[match[1]] = match[2]
    return values


def snapshot(plan, target, policy):
    original = Path(plan['sourceRoot'])
    previous = read_json(plan['sourceManifest'])
    require(digest(plan['sourceManifest'])==plan['sourceManifestSha256'], 'Input source manifest identity mismatch')
    owned = set(plan['ownedFiles']) | set(plan.get('newFiles', []))
    names = set(previous['files']) | set(plan.get('newFiles', []))
    require(owned <= names, 'Owned source missing from input manifest')
    sources = {}
    for name in sorted(names):
        origin = original if name in owned else Path(previous['root'])
        source = safe_path(origin, name, policy, ('deny_access','hash_only'))
        require(not name.startswith('resources/templates/__pycache__/'), 'Bytecode cache cannot be snapshotted')
        value = digest(source)
        if name not in owned:
            require(value==previous['files'][name], f'Pinned external source changed: {name}')
        destination = safe_path(target, name, policy, ('deny_access','hash_only'))
        destination.parent.mkdir(parents=True, exist_ok=True)
        shutil.copyfile(source, destination)
        content=source.read_bytes()
        blob=hashlib.sha1(b'blob '+str(len(content)).encode()+b'\0'+content).hexdigest()
        sources[name] = {'sha256':value,'origin':str(source),'gitBlobSha1':blob,'owned':name in owned}
    return {'root':str(target),'originalRoot':str(original),'files':sources,
            'inputManifestSha256':digest(plan['sourceManifest'])}


def verify_sources(manifest, policy, origins=True):
    for name, entry in manifest['files'].items():
        require(digest(safe_path(manifest['root'], name, policy, ('deny_access','hash_only')))==entry['sha256'], f'Snapshot source changed: {name}')
        if origins:
            # Validate the relative name before opening its recorded origin.
            origin = Path(entry['origin'])
            require(origin.as_posix().endswith('/'+name), f'Invalid source origin: {name}')
            require(digest(safe_path(origin.parents[len(Path(name).parts)-1], name, policy, ('deny_access','hash_only')))==entry['sha256'], f'Current source changed: {name}')


def first_diagnostic(result):
    lines = Path(result['log']).read_text(encoding='utf8', errors='replace').splitlines()
    failed = [x for x in lines if x.startswith('FAILED:')]
    errors = [x for x in lines if re.search(r'(^CMake Error|fatal error:|error:|undefined reference|cannot find|not recognized)', x, re.I)]
    return {'failedMarkers':sum(x.count('FAILED:') for x in lines),
            'targetOrObject':failed[0][len('FAILED:'):].strip() if failed else None,
            'firstDiagnostic':errors[0] if errors else (lines[-1] if lines else 'No diagnostic output; see start/timeout state'),
            'logSha256':digest(result['log'])}


def verify_managed_result(rule, result_path, config, policy_hash):
    result = read_json(result_path)
    name = rule['name']
    spec_path = Path(config['build'])/'Testing/ManagedCommands'/(name+'.json')
    require(result.get('schema')=='zeroslack.managed-test-result/v2' and result.get('evidenceComplete') is True
            and result.get('evidencePending') is False and result.get('infrastructureErrors')==[],
            f'{name}: missing/incomplete managed evidence or infrastructure error')
    require(result['name']==name and result['policySha256']==policy_hash
            and result['specSha256']==digest(spec_path), f'{name}: managed result identity mismatch')
    require(result['sourceTree']==config['source'] and result['buildDirectory']==config['build']
            and result['invocationId']==Path(result_path).parent.name, f'{name}: invocation identity mismatch')
    if result['classification']=='restricted':
        require(rule['execution']=='restricted' and result['reason']==rule['reason']
                and result['exitCode']==2 and not result['processStarted'], f'{name}: invalid restricted evidence')
        return result
    require(result['processStarted'] is True and result['processCompleted'] is True
            and type(result['exitCode']) is int, f'{name}: no completed process result')
    classification = 'timeout' if result['timeout'] else 'passed' if result['exitCode']==0 else 'failed'
    require(result['classification']==classification, f'{name}: process classification mismatch')
    require(result['command']==read_json(spec_path)['command'], f'{name}: tested command mismatch')
    require(result['binaries'] and all(digest(path)==value for path,value in result['binaries'].items()),
            f'{name}: missing/changed tested binaries')
    require(Path(result['log'])==Path(result_path).parent/'output.log'
            and digest(result['log'])==result['logSha256'], f'{name}: missing/changed complete log')
    require(all(digest(row['record'])==row['sha256'] for row in result['settingsInitializations']),
            f'{name}: settings initialization evidence changed')
    return result


def verify_required_target(rule, config, binaries):
    targets = [row for row in read_json(config['targets'])
               if row.get('name') == rule['target'] and row.get('type') == 'EXECUTABLE']
    require(len(targets) == 1, f"{rule['name']}: mapped executable target is missing or ambiguous")
    executable = Path(targets[0]['artifact']).resolve()
    require(executable.is_file(), f"{rule['name']}: mapped executable artifact is missing")
    identities = {Path(path).resolve(): value for path, value in binaries.items()}
    require(executable in identities and digest(executable) == identities[executable],
            f"{rule['name']}: tested program does not match mapped target")
    return executable


def verify_required_result(rule, result_path, build, policy_hash):
    config = read_json(Path(build)/'managed-test-config.json')
    result = verify_managed_result(rule, result_path, config, policy_hash)
    require(result['exitCode']==0 and not result['timeout'], f"{rule['name']}: required process failed ({result['exitCode']})")
    require(result['binaries'], f"{rule['name']}: binary identity missing")
    require(all(digest(name)==value for name,value in result['binaries'].items()), f"{rule['name']}: required binary hash mismatch")
    verify_required_target(rule, config, result['binaries'])
    require(digest(result['log'])==result['logSha256'], f"{rule['name']}: required log hash mismatch")
    qt_root = Path(result['qtGuiDirectory'])
    logs = list(qt_root.glob('**/test.log')) if qt_root.exists() else []
    require(len(logs)<=1, f"{rule['name']}: ambiguous Qt invocation")
    log = logs[0] if logs else Path(result['log'])
    text = log.read_text(encoding='utf8', errors='replace')
    cases = re.findall(r'^PASS\s+: \w+::([^\r\n]+)', text, re.M)
    cases = [x.strip() for x in cases]
    require(cases and len(cases)==len(set(cases)) and set(cases)==set(rule['coverage']['cases']), f"{rule['name']}: zero/missing/unexpected layout cases")
    require(not re.search(r'^FAIL!|^SKIP|^QFATAL', text, re.M), f"{rule['name']}: failed/skipped/aborted layout cases")
    totals = re.findall(r'Totals:\s+(\d+) passed, (\d+) failed, (\d+) skipped', text)
    require(len(totals)==1 and list(map(int,totals[0]))==[len(cases),0,0], f"{rule['name']}: invalid Qt totals")
    measurements = Path(result['artifactDirectory'])/rule['coverage']['measurementFile']
    require(measurements.is_file(), f"{rule['name']}: missing actual DPR measurements")
    rows = [json.loads(line) for line in measurements.read_text(encoding='utf8').splitlines() if line.strip()]
    require(rows and all(x.get('actualDpr')==rule['coverage']['dpr'] and x.get('actualUiBackend')=='ela' for x in rows), f"{rule['name']}: actual DPR/backend mismatch")
    require(all(not row.get('failures') for row in rows), f"{rule['name']}: measured layout failure")
    return {'name':rule['name'],'dpr':rule['coverage']['dpr'],'cases':cases,'actualDpr':sorted(set(x['actualDpr'] for x in rows)),
            'measurements':str(measurements),'measurementsSha256':digest(measurements),'qtLog':str(log),
            'qtLogSha256':digest(log),'result':str(result_path),'resultSha256':digest(result_path)}


def add_required_coverage(rule, execution, paths, config, policy_hash):
    # A real failed/timed-out process already blocks preparation. Coverage
    # acceptance applies to passed processes; it must not relabel their failures.
    if not rule.get('required') or execution['classification']!='passed':
        return
    try:
        require(len(paths)==1, f"{rule['name']}: required result missing or ambiguous")
        execution['coverage']=verify_required_result(rule,paths[0],config['build'],policy_hash)
    except (PolicyError, OSError, ValueError, KeyError) as error:
        execution['coverageError']=str(error)
        infrastructure_error(execution, 'required_evidence', error)
        execution['classification']='infrastructure_error'


def test_set(config_path, output, selection=None, required=False, caller_path=None):
    config = read_json(config_path)
    proof = guard(config_path, Path(output)/'policy.json')
    policy = load_policy(config['policy'])
    active = [x for x in policy['tests'] if applicable(x,config['configuration'])]
    if required:
        selected = [x for x in active if x.get('required')]
    else:
        require(selection is None or len(selection)>0, 'An empty selection is not a test run')
        selected = [x for x in active if x['execution']!='restricted' and
                    ((selection is None and x['execution']=='run') or
                     (selection is not None and (x['name'] in selection or x['name']==policy['guard'])))]
        if selection is not None:
            require(set(selection) <= {x['name'] for x in selected}, 'Selection contains unknown/restricted test')
    require(selected, 'No applicable tests selected')
    out = Path(output)
    out.mkdir(parents=True,exist_ok=True)
    planned = {'run':[x['name'] for x in selected], 'excluded':[x['name'] for x in active if x['execution']=='excluded' and x not in selected],
               'explicitDefaultExclusions':[x['name'] for x in selected if x['execution']=='excluded'],
               'restricted':[x['name'] for x in active if x['execution']=='restricted'],'notApplicable':proof['notApplicable']}
    dump(out/'selection.json',planned)
    print(json.dumps(planned),flush=True)
    results = []
    for rule in selected:
        case = out/rule['name']
        case.mkdir(exist_ok=False)
        env = os.environ.copy()
        if caller_path is not None:
            env['PATH']=caller_path
        env.update(PYTHONUTF8='1', QT_FONT_DPI='96', QT_QPA_FONTDIR='C:/Windows/Fonts',
                   ZEROSLACK_MANAGED_TEST_LOG_ROOT=str(case/'managed'))
        for key in ['APPDATA','LOCALAPPDATA','USERPROFILE','TEMP','TMP']:
            path = case/key
            path.mkdir()
            env[key]=str(path)
        command = [config['ctest'],'--test-dir',config['build'],'--output-on-failure','-j','1',
                   '-R','^'+re.escape(rule['name'])+'$','--output-junit',str(case/'ctest.xml')]
        execution = run(command, case, env, case/'ctest.log', rule['timeout']+30)
        paths = list((case/'managed'/rule['name']).glob('*/result.json'))
        execution.update(name=rule['name'],sourceTree=config['source'],buildDirectory=config['build'],parallel=1,
                         callerQtBinInPath=config['qtBin'].casefold() in env.get('PATH','').replace('\\','/').casefold(),
                         callerMingwBinInPath=config['compilerBin'].casefold() in env.get('PATH','').replace('\\','/').casefold(),
                         managedResult=str(paths[0]) if len(paths)==1 else None)
        ctest_timeout = '***Timeout' in (case/'ctest.log').read_text(encoding='utf8',errors='replace')
        inner = None
        try:
            require(len(paths)==1, f"{rule['name']}: managed result missing or ambiguous")
            inner = verify_managed_result(rule, paths[0], config, digest(config['policy']))
        except Exception as error:
            execution['evidenceError']=str(error)
            infrastructure_error(execution, 'managed_evidence', error)
        if execution['timeout'] or ctest_timeout or (inner and inner['timeout']):
            execution['classification']='timeout'
            execution['timeoutCause']=inner.get('timeoutCause') if inner else 'undetermined; CTest/process timeout requires diagnosis, not an ordinary assertion failure'
        elif execution['infrastructureErrors'] or not execution['processCompleted']:
            execution['classification']='infrastructure_error'
        elif (execution['exitCode']==0)!=(inner['exitCode']==0):
            infrastructure_error(execution, 'ctest_result', ValueError('CTest and completed managed process disagree'))
            execution['classification']='infrastructure_error'
        else:
            execution['classification']=inner['classification']
        execution['processClassification']=inner['classification'] if inner else None
        execution['processExitCode']=inner['exitCode'] if inner else None
        add_required_coverage(rule, execution, paths, config, digest(config['policy']))
        dump(case/'result.json',execution)
        results.append(execution)
        print(json.dumps({k:execution[k] for k in ['name','exitCode','classification','seconds']}),flush=True)
    status = ('passed' if all(x['classification']=='passed' for x in results) else
              'blocked' if any(x['classification']=='infrastructure_error' for x in results) else 'failed')
    report={'status':status,'required':required,'policySha256':digest(config['policy']),'results':results,
            'counts':{k:sum(x['classification']==k for x in results) for k in ['passed','failed','timeout','restricted','infrastructure_error']},
            'fullCtestRun':False,'sourceTree':config['source'],'buildDirectory':config['build']}
    dump(out/'result.json',report)
    return report


def prepare(plan_path, work, formal=False):
    plan=read_json(plan_path)
    require(plan.get('schema')=='zeroslack.prepare-plan/v1','Invalid preparation plan')
    policy=load_policy(Path(plan['sourceRoot'])/'test_sv/test-execution-policy.json')
    require(len(policy.get('requiredMatrices',[]))==2, 'Formal candidate requires compact and narrow layout contracts')
    work=Path(work).absolute()
    require(not work.exists(),f'Preparation requires a new directory: {work}')
    defines=plan['defines']
    require(all(defines.get(k)==v for k,v in FORMAL_FLAGS.items()),'Preparation requires full formal Ela configuration and BUILD_TESTING=ON')
    require(all(defines.get(k) for k in FORMAL_DEPENDENCIES),'Formal SDK/native dependency configuration missing')
    require(defines.get('GIT_EXECUTABLE'), 'Preparation requires an explicit Git executable')
    git=external_tool_path(defines['GIT_EXECUTABLE'],policy)
    require(git.is_file(),f'Preparation Git executable missing: {git}')
    require(digest(sys.executable)==digest(defines['Python_EXECUTABLE']),'Preparation interpreter differs from the configured interpreter')
    require(plan['generator']=='Ninja','Only the mechanically verified Ninja generator is supported')
    require(int(plan['parallel'])>0,'Invalid build parallelism')
    work.mkdir(parents=True)
    receipt={'schema':'zeroslack.release-preparation/v1','status':'running','startedUtc':stamp(),
             'channel':'formal-candidate' if formal else 'preview-candidate','releaseAuthorized':False,'allRegressionPassed':False,
             'plan':str(Path(plan_path).absolute()),'planSha256':digest(plan_path),'work':str(work),
             'compile':{'status':'not_run'},'policy':{'status':'not_run'},'required':{'status':'not_run'},
             'configuration':defines,'qtDirectory':str(Path(defines['CMAKE_PREFIX_PATH'])),
             'compilerDirectory':str(Path(defines['CMAKE_CXX_COMPILER']).parent.parent)}
    receipt_path=work/'preparation.json'
    dump(receipt_path,receipt)
    try:
        source=work/'source';build=work/'build'
        manifest=snapshot(plan,source,policy)
        dump(work/'source-manifest.json',manifest)
        receipt.update(sourceManifest=str(work/'source-manifest.json'),sourceManifestSha256=digest(work/'source-manifest.json'),
                       sourceRoot=str(source),originalRoot=plan['sourceRoot'],buildDirectory=str(build),
                       policySha256=digest(source/'test_sv/test-execution-policy.json'),newBuildDirectoryVerified=not build.exists())
        dependency_manifest=dependency_identity(plan,policy)
        dump(work/'dependency-manifest.json',dependency_manifest)
        receipt.update(dependencyManifest=str(work/'dependency-manifest.json'),dependencyManifestSha256=digest(work/'dependency-manifest.json'))
        original=plan['sourceRoot']
        receipt['sourceControl']={key:subprocess.check_output([str(git),'-C',original,*args]).decode('utf8',errors='replace').strip()
                                 for key,args in [('head',['rev-parse','HEAD']),('branch',['branch','--show-current'])]}
        tree=subprocess.check_output([str(git),'-C',original,'ls-tree','-r','-z','HEAD']).decode('utf8')
        tracked={entry.split('\t',1)[1]:entry.split('\t',1)[0].split()[2] for entry in tree.split('\0') if '\t' in entry}
        differences=[name for name,entry in manifest['files'].items() if entry['owned'] and tracked.get(name)!=entry['gitBlobSha1']]
        receipt['sourceControl'].update(candidateIncludesUncommittedFiles=bool(differences),changedOwnedFiles=differences)
        env=os.environ.copy()
        env['PATH']=';'.join([str(Path(defines['CMAKE_CXX_COMPILER']).parent),str(Path(defines['CMAKE_PREFIX_PATH'])/'bin'),
                              str(Path(plan['cmake']).parent),str(Path(defines['CMAKE_MAKE_PROGRAM']).parent),'C:/Windows/System32','C:/Windows'])
        env['PYTHONUTF8']='1'
        receipt['tools']={name:{'path':path,'sha256':digest(path),'version':subprocess.check_output([path,'--version'],env=env).decode('utf8',errors='replace').splitlines()[0]}
                          for name,path in [('cmake',plan['cmake']),('ctest',str(Path(plan['cmake']).parent/'ctest.exe')),
                                            ('compiler',defines['CMAKE_CXX_COMPILER']),('cCompiler',defines['CMAKE_C_COMPILER']),
                                            ('ninja',defines['CMAKE_MAKE_PROGRAM']),('python',sys.executable),('git',str(git))]}
        command=[plan['cmake'],'-S',str(source),'-B',str(build),'-G',plan['generator']]+['-D'+k+'='+str(v) for k,v in defines.items()]
        result=run(command,work,env,work/'configure.log',300,True)
        result.update(first_diagnostic(result));receipt['configure']=result
        require(result['exitCode']==0 and not result['timeout'],'Configure failed: '+result['firstDiagnostic'])
        cache=cache_values(build/'CMakeCache.txt')
        require(all(cache.get(k)==v for k,v in FORMAL_FLAGS.items()),'Actual cache does not match mandatory formal configuration')
        receipt['configurationEvidence']={name:digest(build/name) for name in
                                          ['CMakeCache.txt','managed-test-config.json','managed-test-targets.json']}
        suite=read_json(build/'generated/suiteapp-build.json')
        require(suite.get('enabled') and suite.get('guiProvider') and suite.get('cliClient'),'Formal SuiteApp capability missing')
        config=build/'managed-test-config.json'
        configured_git=read_json(config)['runtimeTools']['git']
        require(Path(configured_git['path'])==git and configured_git['sha256']==receipt['tools']['git']['sha256'],
                'Configured Git differs from the preparation tool')
        proof=guard(config,work/'policy-before-build.json')
        receipt['policy']={'status':'passed','proof':str(work/'policy-before-build.json'),'sha256':digest(work/'policy-before-build.json')}
        command=[plan['cmake'],'--build',str(build),'--parallel',str(plan['parallel'])]
        result=run(command,work,env,work/'build.log',7200,True)
        result.update(first_diagnostic(result))
        receipt['compile']={'status':'passed' if result['exitCode']==0 and not result['timeout'] and result['failedMarkers']==0 else 'failed',**result}
        dump(receipt_path,receipt)
        require(receipt['compile']['status']=='passed','Build failed: '+str(result['targetOrObject'])+'; '+result['firstDiagnostic'])
        proof=guard(config,work/'policy-after-build.json')
        receipt['policy']={'status':'passed','proof':str(work/'policy-after-build.json'),'sha256':digest(work/'policy-after-build.json')}
        receipt['binaries']={name:digest(build/name) for name in PRODUCT_BINARIES}
        for required_input in ['components/xips/xips-browser.dll','components/xips-asset-adapter/xips-asset-adapter.exe','resources/document-editors/manifest.json']:
            require((build/required_input).is_file(),f'Missing prepared native/runtime capability: {required_input}')
        receipt['packageInputs']={name:digest(path) for name,path in tree_files(build,policy)
                                 if path.suffix.lower() in {'.exe','.dll'} or name.startswith(('components/','resources/document-editors/'))}
        receipt['buildIdentity']=build_identity(receipt,policy)
        receipt['buildIdentityOrigin']={'phase':'after_compile_before_required_tests',
                                       'compileLogSha256':receipt['compile']['logSha256'],'capturedUtc':stamp()}
        dump(receipt_path,receipt)
        matrix=test_set(config,work/'required',required=True)
        receipt['required']={'status':matrix['status'],'proof':str(work/'required/result.json'),'sha256':digest(work/'required/result.json')}
        require(matrix['status']=='passed','Required layout matrix failed; package writes remain prohibited')
        verify_sources(manifest,policy)
        verify_dependencies(dependency_manifest,policy)
        require(all(digest(build/name)==value for name,value in receipt['binaries'].items()),'Product binary changed during preparation')
        verify_build_identity(receipt,policy)
        receipt['status']='passed'
        receipt['completedUtc']=stamp()
        dump(receipt_path,receipt)
        return receipt
    except BaseException as error:
        receipt.update(status='failed',error=str(error),completedUtc=stamp())
        dump(receipt_path,receipt)
        raise


def prepared_version(receipt):
    version=(Path(receipt['sourceRoot'])/'VERSION').read_text(encoding='utf8').strip()
    header=(Path(receipt['buildDirectory'])/'generated/version.h').read_text(encoding='utf8')
    match=re.search(r'#define\s+APP_VERSION\s+"([^"]+)"',header)
    require(match and match[1]==version,'Generated/application version mismatch; incremental version build required')
    return version


def ninja_command_hash(command):
    # Ninja 1.12.1 build_log.cc: MurmurHash64A, log format v6.
    data=command.encode('utf8'); mask=(1<<64)-1; multiplier=0xc6a4a7935bd1e995
    value=(0xDECAFBADDECAFBAD ^ (len(data)*multiplier)) & mask
    stop=len(data)//8*8
    for offset in range(0,stop,8):
        part=int.from_bytes(data[offset:offset+8],'little')*multiplier & mask
        part^=part>>47; part=part*multiplier & mask
        value=(value^part)*multiplier & mask
    if stop<len(data):
        value=(value^int.from_bytes(data[stop:],'little'))*multiplier & mask
    value^=value>>47; value=value*multiplier & mask; value^=value>>47
    return format(value,'x')


BUILD_IDENTITY_SCOPE = 'all-build-files-with-managed-commands/v1'


def build_file_identity(build,policy):
    """Freeze build files without an extension allowlist; exclude test artifacts."""
    build=Path(build); files={}; pending=[build]
    commands=safe_path(build,'Testing/ManagedCommands',policy)
    if commands.is_dir():pending.append(commands)
    while pending:
        directory=pending.pop()
        for entry in os.scandir(directory):
            path=Path(entry.path); name=path.relative_to(build).as_posix()
            # CTest and managed tests may create cyclic fixture trees here.
            # ManagedCommands is configuration, so it is included separately.
            if name=='Testing' or entry.name in {'__pycache__','.git'}:
                continue
            safe_path(build,name,policy)
            if entry.is_dir(follow_symlinks=False):
                pending.append(path)
            else:
                require(entry.is_file(follow_symlinks=False),f'Non-regular build input: {name}')
                files[name]=digest(path)
    return files


def build_identity(receipt,policy):
    """Sample current metadata. This alone is NOT evidence of prior build inputs."""
    build=Path(receipt['buildDirectory']); ninja=receipt['tools']['ninja']['path']
    log=(build/'.ninja_log').read_text(encoding='utf8').splitlines()
    require(log and log[0]=='# ninja log v6','Unknown/missing Ninja command evidence; complete preparation required')
    logged={parts[3]:parts[4] for line in log[1:] if len(parts:=line.split('\t'))==5}
    def compdb(expand=False):
        command=[ninja,'-C',str(build),'-t','compdb']+(['-x'] if expand else [])
        process=subprocess.run(command,capture_output=True,encoding='utf8',timeout=30)
        require(process.returncode==0,'Cannot inspect Ninja commands')
        return json.loads(process.stdout)
    raw=compdb(); expanded=compdb(True)
    require(len(raw)==len(expanded),'Ninja command inspection changed during verification')
    checked={}
    for row,full in zip(raw,expanded):
        name=row['output']; command=row['command']
        if not command or name not in logged:
            continue
        require(name==full['output'],'Ninja command order changed')
        if command!=full['command']:
            matches=list(re.finditer(r'@(?:"[^"]+"|[^\s]+)',command))
            require(len(matches)==1,f'Unknown response-file command: {name}')
            token=matches[0]; prefix=command[:token.start()]; suffix=command[token.end():]
            require(full['command'].startswith(prefix) and full['command'].endswith(suffix),f'Unknown response expansion: {name}')
            content=full['command'][len(prefix):len(full['command'])-len(suffix) if suffix else None]
            command+=';rspfile='+content
        require(ninja_command_hash(command)==logged[name],f'Build command changed since execution: {name}')
        checked[name]=logged[name]
    require(all(name in checked for name in PRODUCT_BINARIES if name!='WebView2Loader.dll'),
            'Product link commands are not covered by the completed build log')
    prepared_version(receipt)
    return {'scope':BUILD_IDENTITY_SCOPE,'files':build_file_identity(build,policy),'executedCommands':checked}


def require_original_build_identity(receipt):
    origin=receipt.get('buildIdentityOrigin',{})
    require(receipt.get('buildIdentity',{}).get('scope')==BUILD_IDENTITY_SCOPE
            and origin.get('phase')=='after_compile_before_required_tests'
            and origin.get('compileLogSha256')==receipt.get('compile',{}).get('logSha256')
            and origin.get('capturedUtc'),
            'Original generated-input identity missing: an import-time sample cannot prove the old compilation. '
            'Minimum evidence: the complete build-time file/command fingerprint bound to its successful compile log, '
            'captured before acceptance tests, plus unchanged-file verification after those tests. '
            'If that original evidence does not exist, prepare an isolated candidate; do not backfill the historical receipt.')


def verify_build_identity(receipt,policy):
    require_original_build_identity(receipt)
    require(build_identity(receipt,policy)==receipt['buildIdentity'],
            'Generated inputs/build commands changed from their original build-time fingerprint')


def source_delta(receipt,current_root):
    policy=load_policy(Path(receipt['sourceRoot'])/'test_sv/test-execution-policy.json')
    manifest=read_json(receipt['sourceManifest']); root=absolute(current_root)
    names=set(manifest['files'])
    # Index inspection does not open protected fixtures. Inspect untracked names
    # only inside product directories that contain no restricted resources.
    git=receipt['tools']['git']['path']
    tracked=subprocess.check_output([git,'-C',str(root),'ls-files','-z']).decode('utf8').split('\0')
    from test_policy import blocked
    names.update(n for n in tracked if n and not blocked(policy,n,('deny_access','hash_only'))
                 and not n.startswith('scripts/__pycache__/'))
    extras=subprocess.check_output([git,'-C',str(root),'ls-files','--others','--exclude-standard','-z',
                                    '--','src','include','cmake','components']).decode('utf8').split('\0')
    names.update(n for n in extras if n)
    names.update(n for n in EXTERNAL_FILES if safe_path(root,n,policy).is_file())
    delta={}; current={}
    for name in sorted(names):
        file=safe_path(root,name,policy,('deny_access','hash_only'))
        actual=digest(file) if file.is_file() else None
        expected=manifest['files'].get(name,{}).get('sha256')
        current[name]=actual
        if actual!=expected:
            kind='external_metadata' if name in EXTERNAL_FILES else 'compiled_version' if name=='VERSION' else 'build_or_function_input'
            delta[name]={'before':expected,'after':actual,'kind':kind}
    return delta,current


def accept_source_delta(delta):
    versions=[n for n,v in delta.items() if v['kind']=='compiled_version']
    changed=[n for n,v in delta.items() if v['kind']=='build_or_function_input']
    require(not versions,'VERSION is compiled into the program: direct reuse refused; incrementally configure/build affected targets and verify their versions, then import matching evidence (old runs retain their original binary identity)')
    require(not changed,'Build/function inputs changed; supplement affected checks or run complete preparation: '+', '.join(changed[:12]))
    require(all(v['after'] for v in delta.values()),'Removed external delivery input; review/update its packaging contract first')


def verify_checksums(package,policy,allow_extras=False):
    actual={name:digest(path) for name,path in tree_files(package,policy) if name!='SHA256SUMS.txt'}
    recorded={}
    for line in (Path(package)/'SHA256SUMS.txt').read_text(encoding='utf-8-sig').splitlines():
        value,name=line.split('  ',1)
        safe_path(package,name,policy)
        require(name not in recorded,'Duplicate package checksum: '+name)
        recorded[name]=value
    require((recorded==actual) if not allow_extras else bool(recorded) and all(actual.get(n)==h for n,h in recorded.items()),
            'Package checksums/inventory mismatch')
    return actual


def write_checksums(package,policy):
    values={name:digest(path) for name,path in tree_files(package,policy) if name!='SHA256SUMS.txt'}
    (Path(package)/'SHA256SUMS.txt').write_text(''.join(value+'  '+name+'\n' for name,value in sorted(values.items())),encoding='utf8')


def verify_package_sources(path,receipt):
    policy=load_policy(Path(receipt['sourceRoot'])/'test_sv/test-execution-policy.json')
    plan=read_json(external_tool_path(path,policy))
    require(plan.get('schema')=='zeroslack.package-sources/v1','Invalid first-package source plan')
    require(set(plan.get('sources',{}))=={'native','nativeLicenses','adapter','documents'},
            'First packaging needs explicit component, adapter, resource and component-license sources')
    for name,entry in plan['sources'].items():
        root=external_tool_path(entry['root'],policy)
        proof=entry['provenance']; proof_file=external_tool_path(proof['file'],policy)
        require(digest(proof_file)==proof['sha256'],'Package source provenance changed: '+name)
        recorded=read_json(proof_file)
        for key in proof['keys']:recorded=recorded[key]
        require(isinstance(recorded,dict),'Provenance must identify a file mapping: '+name)
        expected={n[len(proof.get('prefix','')):]:h for n,h in recorded.items() if n.startswith(proof.get('prefix',''))}
        expected={n:(h['sha256'] if isinstance(h,dict) else h) for n,h in expected.items()}
        require(entry.get('files') and entry['files']==expected,'Package source not fully covered by original provenance: '+name)
        require({n:digest(p) for n,p in tree_files(root,policy)}==expected,'Package source inventory/content changed: '+name)
    approval=plan['componentBaseline']
    baseline_file=external_tool_path(approval['file'],policy)
    require(digest(baseline_file)==approval['sha256'],'Approved component baseline changed')
    baseline=read_json(baseline_file);native=read_json(Path(plan['sources']['native']['root'])/'build-info.json')
    adapter=read_json(Path(plan['sources']['adapter']['root'])/'build-info.json')
    version=lambda value:tuple(int(x) for x in value.split('.'))
    require(native.get('channel')=='formal' and not native.get('dirty') and not native.get('sourceDirty'),
            'Native component must be a clean approved release')
    require(native==baseline['native'] and adapter==baseline['adapter'],
            'Native/adapter sources differ from their explicitly approved baseline (including version)')
    suite=read_json(Path(receipt['buildDirectory'])/'generated/suiteapp-build.json')
    require(version(suite['sdkVersion'])>=version(baseline['native']['suiteSdk']),'Suite SDK downgrade refused')
    require(native['nativeSurfaceAbi']==1 and native['qt']=='6.10.2' and adapter['protocolVersion']==1
            and adapter['qt']=='6.10.2' and adapter['buildType']=='Release','Incompatible native/independent adapter contracts')
    documents=plan['sources']['documents']
    dependencies=read_json(receipt['dependencyManifest'])
    require(documents['files']==dependencies['ZEROSLACK_DOCUMENT_EDITORS_DIR']['files'],
            'Document resources differ from the accepted build dependency')
    require(plan.get('distribution') in {'standalone','appsuite'},'Package distribution must be explicit')
    return plan


def verify_assembled_payload(package,receipt):
    report=receipt.get('reuse',{})
    if not report.get('packageSources'):return
    plan=verify_package_sources(report['packageSources']['path'],receipt)
    destinations={'native':'components/xips','nativeLicenses':'licenses/components/xips',
                  'adapter':'components/xips-asset-adapter','documents':'resources/document-editors'}
    policy=load_policy(Path(receipt['sourceRoot'])/'test_sv/test-execution-policy.json')
    for role,prefix in destinations.items():
        expected=plan['sources'][role]['files']
        actual={name:digest(p) for name,p in tree_files(Path(package)/prefix,policy)}
        require(actual==expected,'Assembled component/resources/licenses differ from their source: '+role)
    # Every deployed Qt/MinGW runtime file must come from the accepted toolchain.
    dependencies=read_json(receipt['dependencyManifest'])
    approved={}
    for key in ['qtRuntime','compilerRuntime']:
        for name,value in dependencies[key]['files'].items():
            if name.startswith(('bin/','plugins/')):approved[name.split('/',1)[1]]=value
    owned=read_json(Path(package)/'build-info.json')
    native=read_json(Path(plan['sources']['native']['root'])/'build-info.json')
    adapter=read_json(Path(plan['sources']['adapter']['root'])/'build-info.json')
    require(owned['nativeComponents']['xips']['version']==native['version']
            and owned['assetAdapter']['coreVersion']==adapter['coreVersion'],'Assembled component metadata mismatch')
    for name,p in tree_files(package,policy):
        if p.suffix.casefold()=='.dll' and not name.startswith('components/') and name not in receipt['binaries']:
            require(approved.get(name)==digest(p),'Unapproved packaged runtime/plugin: '+name)


def reuse(evidence,source,output,accepted_package=None,package_sources=None):
    evidence=absolute(evidence); output=absolute(output); source=absolute(source)
    require(not output.exists(),'Reuse report must be new; historical evidence is read-only')
    require(read_json(evidence).get('schema')=='zeroslack.release-preparation/v1',
            'Import the original preparation, not a chain of reuse reports')
    started=time.monotonic()
    report={'schema':'zeroslack.release-reuse/v1','status':'checking','startedUtc':stamp(),
            'preparation':str(evidence),'preparationSha256':digest(evidence),'currentSource':str(source),
            'executed':{'cppBuild':False,'layoutMatrix':False,'fullRegression':False},'releaseAuthorized':False}
    try:
        receipt=verify_receipt(evidence,formal=True,origins=False)
        require(source==Path(receipt['originalRoot']).resolve(),'Reuse source must identify the prepared repository')
        delta,current=source_delta(receipt,source)
        report.update(sourceDifferences=delta,currentFiles=current)
        accept_source_delta(delta)
        policy=load_policy(Path(receipt['sourceRoot'])/'test_sv/test-execution-policy.json')
        # Preserve the verified build-time identity, never establish a new
        # baseline by sampling whatever exists at import time.
        report['buildIdentity']=receipt['buildIdentity']
        require(not (accepted_package and package_sources),'Select existing-package reuse or first assembly, not both')
        if package_sources:
            path=absolute(package_sources);verify_package_sources(path,receipt)
            report['packageSources']={'path':str(path),'sha256':digest(path)}
            report['stagingMode']='assemble_from_build'
        if accepted_package:
            package=absolute(accepted_package)
            prior=verify_package(package,origins=False)
            require(prior['sourceManifestSha256']==receipt['sourceManifestSha256']
                    and prior['binaries']==receipt['binaries'],'Accepted package belongs to a different candidate')
            report['acceptedPackage']={'path':str(package),'gateSha256':digest(package/'release-gate.json'),
                                       'checksumsSha256':digest(package/'SHA256SUMS.txt')}
            report['stagingMode']='copy_existing_package'
        report.update(status='passed',sourceManifestSha256=receipt['sourceManifestSha256'],
                      requiredProof=receipt['required'],binaryIdentity=receipt['binaries'],
                      originalSourceControl=receipt['sourceControl'],
                      disposition='reuse_with_external_updates' if delta else 'direct_reuse')
    except BaseException as error:
        report.update(status='blocked',reason=str(error),disposition='supplement_or_prepare')
        raise
    finally:
        report.update(completedUtc=stamp(),seconds=round(time.monotonic()-started,3))
        dump(output,report)
    return report


def verify_reuse(path,original_root=None,formal=False):
    report=read_json(path)
    require(report.get('status')=='passed','Missing/failed reuse verification')
    if original_root:
        require(absolute(original_root)==absolute(report['currentSource']),'Reuse source mismatch')
    require(digest(report['preparation'])==report['preparationSha256'],'Original preparation changed')
    require(read_json(report['preparation']).get('schema')=='zeroslack.release-preparation/v1','Nested reuse evidence unsupported')
    receipt=verify_receipt(report['preparation'],formal=formal,origins=False)
    delta,current=source_delta(receipt,report['currentSource']); accept_source_delta(delta)
    require(delta==report['sourceDifferences'] and current==report['currentFiles'],'Current source changed after reuse verification')
    policy=load_policy(Path(receipt['sourceRoot'])/'test_sv/test-execution-policy.json')
    require(build_identity(receipt,policy)==report['buildIdentity'],'Generated inputs/build identity changed after reuse verification')
    if report.get('acceptedPackage'):
        item=report['acceptedPackage']; package=Path(item['path'])
        require(digest(package/'release-gate.json')==item['gateSha256']
                and digest(package/'SHA256SUMS.txt')==item['checksumsSha256'],'Accepted package identity changed')
        binding=read_json(package/'release-gate.json')
        # A same-candidate package may already be bound to a prior reuse report.
        # Its frozen gate/checksums and original candidate content are the link.
        require(binding['sourceManifestSha256']==receipt['sourceManifestSha256']
                and all(binding['files'].get(name)==value for name,value in receipt['binaries'].items()),
                'Accepted package/preparation mismatch')
        verify_checksums(package,policy)
    if report.get('packageSources'):
        entry=report['packageSources']
        require(digest(entry['path'])==entry['sha256'],'First-package source plan changed')
        verify_package_sources(entry['path'],receipt)
    return {**receipt,'reuse':report,'reuseEvidence':str(path)}


def stage_reused_package(evidence,output,allow_dirty=False):
    evidence=absolute(evidence); output=absolute(output)
    receipt=verify_receipt(evidence,formal=True)
    report=receipt.get('reuse',{}); item=report.get('acceptedPackage')
    if not item:
        require(report.get('packageSources'),'First packaging needs --package-sources with approved component/runtime/resource/license provenance')
        command=['pwsh.exe','-NoProfile','-NonInteractive','-File',str(REPOSITORY/'scripts/package-release.ps1'),
                 '-PreparationEvidence',str(evidence),'-OutputDirectory',str(output),'-Formal']
        if allow_dirty:command.append('-AllowDirty')
        result=subprocess.run(command,creationflags=subprocess.CREATE_NO_WINDOW if os.name=='nt' else 0)
        require(result.returncode==0,'First package assembly failed; retain its output and diagnostic')
        return {'status':'passed','package':str(output),'stagingMode':'assemble_from_build','cppBuildExecuted':False,'layoutMatrixExecuted':False}
    root=Path(report['currentSource']); policy=load_policy(Path(receipt['sourceRoot'])/'test_sv/test-execution-policy.json')
    require(not output.exists(),'Package staging directory must be new')
    external_tool_path(str(output),policy)
    package=Path(item['path'])
    require(not output.is_relative_to(package) and not package.is_relative_to(output),'Staging overlaps accepted package')
    git=receipt['tools']['git']['path']
    status=subprocess.check_output([git,'-C',str(root),'status','--porcelain','--untracked-files=no','--ignore-submodules=all']).decode('utf8')
    dirty=bool(status) or any(v['before'] is None for v in report['sourceDifferences'].values())
    require(not dirty or allow_dirty,'Formal staging has uncommitted source; explicitly use -AllowDirty for an isolated candidate')
    shutil.copytree(package,output)
    info=read_json(output/'build-info.json')
    for name,target in [('用户手册.md','用户手册.md'),('packaging/ZeroSlack-PACKAGE-README.txt','README.txt'),('docs/cli-automation.md','docs/cli-automation.md')]:
        shutil.copyfile(root/name,output/target)
    revision=subprocess.check_output([git,'-C',str(root),'rev-parse','HEAD']).decode().strip()
    tags=subprocess.check_output([git,'-C',str(root),'tag','--points-at',revision,'--list','v'+info['version']]).decode().splitlines()
    info.update(revision=revision,dirty=dirty,releaseTag=None if dirty or 'v'+info['version'] not in tags else 'v'+info['version'],
                preparationEvidenceSha256=digest(evidence),packagedAtUtc=stamp(),
                reusedBinarySource={'preparation':report['preparation'],'sha256':report['preparationSha256'],
                                    'sourceManifestSha256':receipt['sourceManifestSha256']})
    dump(output/'build-info.json',info)
    seal_package(output,evidence)
    write_checksums(output,policy)
    verify_package(output)
    return {'status':'passed','package':str(output),'cppBuildExecuted':False,'layoutMatrixExecuted':False}


def verify_receipt(path, original_root=None, formal=False, origins=True, sealed_payload_only=False):
    path=absolute(path)
    receipt=read_json(path)
    if receipt.get('schema')=='zeroslack.release-reuse/v1':
        return verify_reuse(path,original_root,formal)
    require(receipt.get('schema')=='zeroslack.release-preparation/v1' and receipt.get('status')=='passed','Missing/failed complete preparation evidence')
    if not sealed_payload_only:
        require_original_build_identity(receipt)
    if formal:
        require(receipt.get('channel')=='formal-candidate','Preview preparation cannot authorize a formal candidate')
    if original_root:
        require(Path(receipt['originalRoot']).absolute()==Path(original_root).absolute(),'Preparation candidate/source root mismatch')
    require(digest(receipt['plan'])==receipt['planSha256'],'Preparation plan changed')
    plan=read_json(receipt['plan'])
    require(receipt['configuration']==plan['defines'] and all(receipt['configuration'].get(k)==v for k,v in FORMAL_FLAGS.items()),
            'Receipt configuration differs from the full formal plan')
    require(digest(receipt['sourceManifest'])==receipt['sourceManifestSha256'],'Source manifest changed')
    policy_path=Path(receipt['sourceRoot'])/'test_sv/test-execution-policy.json'
    policy=load_policy(policy_path)
    require(digest(policy_path)==receipt['policySha256'],'Policy changed since preparation')
    verify_sources(read_json(receipt['sourceManifest']),policy,origins=origins)
    require(digest(receipt['dependencyManifest'])==receipt['dependencyManifestSha256'],'Dependency manifest changed')
    verify_dependencies(read_json(receipt['dependencyManifest']),policy)
    require(dependency_identity(plan,policy)==read_json(receipt['dependencyManifest']),
            'Dependency inventory changed (including added files)')
    for name,tool in receipt['tools'].items():
        require(digest(tool['path'])==tool['sha256'],f'Preparation tool changed: {name}')
    require(digest(sys.executable)==receipt['tools']['python']['sha256'],'Evidence verifier interpreter differs from preparation')
    build=Path(receipt['buildDirectory'])
    require(set(receipt.get('configurationEvidence',{}))=={'CMakeCache.txt','managed-test-config.json','managed-test-targets.json'},
            'Configuration identity missing')
    require(all(digest(build/name)==value for name,value in receipt['configurationEvidence'].items()),'Prepared configuration changed')
    config=read_json(build/'managed-test-config.json')
    require(Path(config['source'])==Path(receipt['sourceRoot']) and Path(config['build'])==build,
            'Prepared source/build configuration identity mismatch')
    git=receipt['tools'].get('git',{})
    configured_git=config.get('runtimeTools',{}).get('git',{})
    require(git.get('path') and Path(git['path'])==Path(plan['defines'].get('GIT_EXECUTABLE',''))
            and Path(git['path'])==Path(configured_git.get('path','')) and git['sha256']==configured_git.get('sha256'),
            'Prepared Git identity/configuration mismatch')
    current_guard=guard(build/'managed-test-config.json')
    for name in ['compile','policy','required']:
        require(receipt.get(name,{}).get('status')=='passed',f'Incomplete preparation stage: {name}')
    configured=receipt['configure']
    require(configured['exitCode']==0 and not configured['timeout']
            and digest(configured['log'])==configured['logSha256'],'Configure result/log mismatch')
    result=receipt['compile']
    require(result['exitCode']==0 and not result['timeout'] and first_diagnostic(result)['failedMarkers']==0
            and digest(result['log'])==result['logSha256'],'Compile result/log mismatch')
    command=result['command']
    require(command[1:3]==['--build',receipt['buildDirectory']] and command[3:4]==['--parallel'] and len(command)==5,'Default ALL build command missing')
    for name in ['policy','required']:
        require(digest(receipt[name]['proof'])==receipt[name]['sha256'],f'{name} proof changed')
    policy_proof=read_json(receipt['policy']['proof'])
    require(policy_proof.get('status')=='passed' and all(policy_proof.get(key)==current_guard[key]
            for key in ['policySha256','registeredTests','compiledTestTargets','reachableTestTargets','defaultGraphSha256','mappings']),
            'Actual policy/default graph differs from preparation')
    require(all(digest(Path(receipt['buildDirectory'])/name)==value for name,value in receipt['binaries'].items()),'Product binary identity mismatch')
    require(set(receipt['binaries'])==set(PRODUCT_BINARIES),'Incomplete product binary identity')
    require(receipt.get('packageInputs') and all(digest(safe_path(receipt['buildDirectory'],name,policy))==value for name,value in receipt['packageInputs'].items()),'Prepared runtime/component inputs changed')
    matrix=read_json(receipt['required']['proof'])
    require(matrix.get('status')=='passed' and matrix.get('required') is True and matrix.get('policySha256')==receipt['policySha256'],
            'Required proof status/policy mismatch')
    expected={x['name']:x for x in policy['tests'] if x.get('required')}
    actual={x['name']:x for x in matrix['results']}
    require(set(actual)==set(expected) and len(actual)==len(matrix['results']),'Required matrix missing/duplicate tests/DPR')
    for name,rule in expected.items():
        require(actual[name].get('classification')=='passed' and actual[name].get('exitCode')==0 and not actual[name].get('timeout'),
                f'{name}: required outer process failed')
        coverage=actual[name]['coverage']
        require(digest(coverage['result'])==coverage['resultSha256'] and digest(coverage['measurements'])==coverage['measurementsSha256']
                and digest(coverage['qtLog'])==coverage['qtLogSha256'],f'{name}: layout evidence modified')
        verify_required_result(rule,coverage['result'],receipt['buildDirectory'],receipt['policySha256'])
    if not sealed_payload_only:
        verify_build_identity(receipt,policy)
    return receipt


def verify_package(package, origins=True, allow_extras=False):
    package=absolute(package)
    binding=read_json(package/'release-gate.json')
    info=read_json(package/'build-info.json')
    require(info.get('channel')=='formal' and info.get('backend')=='ela','Preview/non-Ela package is not a formal suite input')
    require(binding.get('schema')=='zeroslack.package-gate/v1','Invalid package binding')
    require(digest(binding['preparation'])==binding['preparationSha256'],'Package preparation reference changed')
    # Existing sealed payload verification is not a new candidate import.
    # reuse()/prepare/first staging still require original build-time evidence.
    receipt=verify_receipt(binding['preparation'],formal=True,origins=origins,sealed_payload_only=True)
    require(binding['sourceManifestSha256']==receipt['sourceManifestSha256'],'Package candidate identity mismatch')
    for name,value in binding['files'].items():
        require(digest(safe_path(package,name,load_policy(Path(receipt['sourceRoot'])/'test_sv/test-execution-policy.json')))==value,f'Package content mismatch: {name}')
    require(all(binding['files'].get(name)==value for name,value in receipt['binaries'].items()),'Packaged product binary does not match prepared candidate')
    policy=load_policy(Path(receipt['sourceRoot'])/'test_sv/test-execution-policy.json')
    actual={name for name,p in tree_files(package,policy) if p.name not in {'release-gate.json','SHA256SUMS.txt'}}
    require(actual==set(binding['files']) if not allow_extras else actual>=set(binding['files']),'Package file inventory changed')
    version=prepared_version(receipt)
    require(info.get('version')==version,'Package/program version mismatch')
    require(info.get('preparationEvidenceSha256')==binding['preparationSha256'],'Package metadata/evidence mismatch')
    verify_checksums(package,policy,allow_extras)
    return receipt


def seal_package(package, preparation):
    receipt=verify_receipt(preparation,formal=True)
    package=absolute(package)
    policy=load_policy(Path(receipt['sourceRoot'])/'test_sv/test-execution-policy.json')
    files={name:digest(p) for name,p in tree_files(package,policy) if p.name not in {'release-gate.json','SHA256SUMS.txt'}}
    require(all(files.get(name)==value for name,value in receipt['binaries'].items()),'Package has different prepared product binaries')
    info=read_json(package/'build-info.json')
    require(info.get('version')==prepared_version(receipt),'Package/program version mismatch')
    require(info.get('preparationEvidenceSha256')==digest(preparation),'Package metadata/evidence mismatch')
    verify_assembled_payload(package,receipt)
    dump(package/'release-gate.json',{'schema':'zeroslack.package-gate/v1','preparation':str(Path(preparation).absolute()),
                                    'preparationSha256':digest(preparation),'sourceManifestSha256':receipt['sourceManifestSha256'],'files':files})


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    sub=parser.add_subparsers(dest='action',required=True)
    p=sub.add_parser('prepare');p.add_argument('--plan',required=True);p.add_argument('--work',required=True);p.add_argument('--formal',action='store_true')
    p=sub.add_parser('reuse');p.add_argument('--evidence',required=True);p.add_argument('--source',required=True);p.add_argument('--output',required=True);p.add_argument('--accepted-package');p.add_argument('--package-sources')
    p=sub.add_parser('stage-reused-package');p.add_argument('--evidence',required=True);p.add_argument('--package',required=True);p.add_argument('--allow-dirty',action='store_true')
    p=sub.add_parser('verify');p.add_argument('--evidence',required=True);p.add_argument('--source');p.add_argument('--formal',action='store_true')
    p=sub.add_parser('verify-package');p.add_argument('--package',required=True)
    p=sub.add_parser('seal-package');p.add_argument('--package',required=True);p.add_argument('--evidence',required=True)
    p=sub.add_parser('test');p.add_argument('--config',required=True);p.add_argument('--output',required=True);p.add_argument('--names');p.add_argument('--required',action='store_true');p.add_argument('--caller-path')
    args=parser.parse_args()
    for name in ['plan','work','evidence','source','output','accepted_package','package_sources','package','config','names']:
        if getattr(args,name,None):
            setattr(args,name,str(absolute(getattr(args,name))))
    try:
        if args.action=='prepare':
            prepare(args.plan,args.work,args.formal)
        elif args.action=='reuse':
            reuse(args.evidence,args.source,args.output,args.accepted_package,args.package_sources)
        elif args.action=='stage-reused-package':
            stage_reused_package(args.evidence,args.package,args.allow_dirty)
        elif args.action=='verify':
            verify_receipt(args.evidence,args.source,args.formal)
        elif args.action=='verify-package':
            verify_package(args.package)
        elif args.action=='seal-package':
            seal_package(args.package,args.evidence)
        else:
            result=test_set(args.config,args.output,read_json(args.names) if args.names else None,args.required,args.caller_path)
            return 0 if result['status']=='passed' else 1
        return 0
    except (PolicyError,OSError,ValueError,KeyError,subprocess.TimeoutExpired) as error:
        print(f'RELEASE PREPARATION BLOCKED: {error}',file=sys.stderr)
        return 2


if __name__=='__main__':
    sys.exit(main())
