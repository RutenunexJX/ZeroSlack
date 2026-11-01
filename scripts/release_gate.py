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

from test_policy import (PolicyError, applicable, digest, dump, external_tool_path, guard, inventory,
                         load_policy, read_json, require, safe_path, stamp)
from validation_process import infrastructure_error, run


PRODUCT_BINARIES = ['ZeroSlack.exe','zeroslack-cli.exe','libzeroslack_core.dll',
                    'libzeroslack_semantic.dll','libzeroslack_documents.dll','ElaWidgetTools.dll','WebView2Loader.dll']
FORMAL_FLAGS = {'BUILD_TESTING':'ON','CMAKE_BUILD_TYPE':'Release','ZEROSLACK_ENABLE_ELA':'ON',
                'ZEROSLACK_SELF_CONTAINED_FIXTURES':'ON','ZEROSLACK_TEST_REAL_NATIVE_COMPONENTS':'ON'}
FORMAL_DEPENDENCIES = ['SuiteApp_DIR','ZEROSLACK_XIPS_COMPONENT_DIR','ZEROSLACK_XIPS_ASSET_ADAPTER_DIR',
                       'ZEROSLACK_TICKX_COMPONENT_DIR','ZEROSLACK_DOCUMENT_EDITORS_DIR','ZEROSLACK_WEBVIEW2_SDK_DIR']


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


def verify_required_result(rule, result_path, build, policy_hash):
    result = verify_managed_result(rule, result_path, read_json(Path(build)/'managed-test-config.json'), policy_hash)
    require(result['exitCode']==0 and not result['timeout'], f"{rule['name']}: required process failed ({result['exitCode']})")
    require(result['binaries'], f"{rule['name']}: binary identity missing")
    require(all(digest(name)==value for name,value in result['binaries'].items()), f"{rule['name']}: required binary hash mismatch")
    executable = Path(build)/(rule['target']+'.exe')
    require(str(executable) in result['binaries'] or executable.as_posix() in result['binaries'], f"{rule['name']}: tested program does not match mapped target")
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
        matrix=test_set(config,work/'required',required=True)
        receipt['required']={'status':matrix['status'],'proof':str(work/'required/result.json'),'sha256':digest(work/'required/result.json')}
        require(matrix['status']=='passed','Required layout matrix failed; package writes remain prohibited')
        verify_sources(manifest,policy)
        verify_dependencies(dependency_manifest,policy)
        require(all(digest(build/name)==value for name,value in receipt['binaries'].items()),'Product binary changed during preparation')
        receipt['status']='passed'
        receipt['completedUtc']=stamp()
        dump(receipt_path,receipt)
        return receipt
    except BaseException as error:
        receipt.update(status='failed',error=str(error),completedUtc=stamp())
        dump(receipt_path,receipt)
        raise


def verify_receipt(path, original_root=None, formal=False):
    receipt=read_json(path)
    require(receipt.get('schema')=='zeroslack.release-preparation/v1' and receipt.get('status')=='passed','Missing/failed complete preparation evidence')
    require(receipt.get('newBuildDirectoryVerified') is True,'No new build directory evidence')
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
    verify_sources(read_json(receipt['sourceManifest']),policy)
    require(digest(receipt['dependencyManifest'])==receipt['dependencyManifestSha256'],'Dependency manifest changed')
    verify_dependencies(read_json(receipt['dependencyManifest']),policy)
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
    return receipt


def verify_package(package):
    package=Path(package)
    binding=read_json(package/'release-gate.json')
    info=read_json(package/'build-info.json')
    require(info.get('channel')=='formal' and info.get('backend')=='ela','Preview/non-Ela package is not a formal suite input')
    require(binding.get('schema')=='zeroslack.package-gate/v1','Invalid package binding')
    require(digest(binding['preparation'])==binding['preparationSha256'],'Package preparation reference changed')
    receipt=verify_receipt(binding['preparation'],formal=True)
    require(binding['sourceManifestSha256']==receipt['sourceManifestSha256'],'Package candidate identity mismatch')
    for name,value in binding['files'].items():
        require(digest(safe_path(package,name,load_policy(Path(receipt['sourceRoot'])/'test_sv/test-execution-policy.json')))==value,f'Package content mismatch: {name}')
    require(all(binding['files'].get(name)==value for name,value in receipt['binaries'].items()),'Packaged product binary does not match prepared candidate')
    policy=load_policy(Path(receipt['sourceRoot'])/'test_sv/test-execution-policy.json')
    actual={name for name,p in tree_files(package,policy) if p.name not in {'release-gate.json','SHA256SUMS.txt'}}
    require(actual==set(binding['files']),'Package file inventory changed')
    return receipt


def seal_package(package, preparation):
    receipt=verify_receipt(preparation,formal=True)
    package=Path(package)
    policy=load_policy(Path(receipt['sourceRoot'])/'test_sv/test-execution-policy.json')
    files={name:digest(p) for name,p in tree_files(package,policy) if p.name not in {'release-gate.json','SHA256SUMS.txt'}}
    require(all(files.get(name)==value for name,value in receipt['binaries'].items()),'Package has different prepared product binaries')
    dump(package/'release-gate.json',{'schema':'zeroslack.package-gate/v1','preparation':str(Path(preparation).absolute()),
                                    'preparationSha256':digest(preparation),'sourceManifestSha256':receipt['sourceManifestSha256'],'files':files})


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    sub=parser.add_subparsers(dest='action',required=True)
    p=sub.add_parser('prepare');p.add_argument('--plan',required=True);p.add_argument('--work',required=True);p.add_argument('--formal',action='store_true')
    p=sub.add_parser('verify');p.add_argument('--evidence',required=True);p.add_argument('--source');p.add_argument('--formal',action='store_true')
    p=sub.add_parser('verify-package');p.add_argument('--package',required=True)
    p=sub.add_parser('seal-package');p.add_argument('--package',required=True);p.add_argument('--evidence',required=True)
    p=sub.add_parser('test');p.add_argument('--config',required=True);p.add_argument('--output',required=True);p.add_argument('--names');p.add_argument('--required',action='store_true');p.add_argument('--caller-path')
    args=parser.parse_args()
    try:
        if args.action=='prepare':
            prepare(args.plan,args.work,args.formal)
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
