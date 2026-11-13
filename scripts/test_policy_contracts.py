"""Real CTest contracts for Unicode output, process ownership and evidence IO."""
from datetime import date, timedelta
import json
import os
from pathlib import Path
import re
import subprocess
import sys
import uuid
import xml.etree.ElementTree as ET

from test_policy import PolicyError, digest, dump, read_json, require, stamp
from validation_process import run


PAYLOAD = ('Unicode: 中文 λ 😀 \ufffd\n'+'complete-output-中文\n'*8192+'END-完整输出\n').encode('utf8')+b'bad-utf8:\xff\n'
CHILD = r'''
import os,re,subprocess,sys,time
from pathlib import Path
mode=sys.argv[1]
out=Path(os.environ['ZEROSLACK_TEST_ARTIFACT_DIR']).parent
if mode=='forbidden':
    Path(sys.argv[2]).write_text('MUST NOT EXECUTE',encoding='utf8')
if mode=='write_error':
    (out/'result.json').mkdir()
if mode in ('bad_metadata','bad_metadata_failure'):
    (out/'profile/settings/bootstrap-invalid.json').write_bytes(b'{invalid-json')
payload=('Unicode: 中文 λ 😀 \ufffd\n'+'complete-output-中文\n'*8192+'END-完整输出\n').encode('utf8')+b'bad-utf8:\xff\n'
sys.stdout.buffer.write(payload)
sys.stdout.buffer.flush()
if mode=='read_error':
    log=out/'output.log'
    if os.name=='nt':
        data=subprocess.check_output(['C:/Windows/System32/whoami.exe','/user','/fo','csv','/nh'])
        sid=re.search(rb'S-\d(?:-\d+)+',data).group().decode('ascii')
        subprocess.run(['C:/Windows/System32/icacls.exe',str(log),'/deny','*'+sid+':(RD)'],stdout=subprocess.DEVNULL,stderr=subprocess.PIPE,check=True)
        (out/'restore-read-sid.txt').write_text(sid,encoding='ascii')
    else:
        log.rename(out/'original-output.log')
        log.mkdir()
if mode=='timeout':
    time.sleep(30)
sys.exit(7 if mode in ('failure','bad_metadata_failure') else 0)
'''

DRIVER = r'''
import json,os,subprocess,sys
from pathlib import Path
mode,receipt_path,scripts,*command=sys.argv[1:]
receipt=Path(receipt_path)
if mode=='log_open_error':
    sys.path.insert(0,scripts)
    from validation_process import run
    target=receipt.parent/'not-a-log-file'
    target.mkdir()
    result=run(command,receipt.parent,os.environ.copy(),target,10)
    receipt.write_text(json.dumps(result),encoding='utf8')
    assert result['exitCode']==125 and not result['processStarted'] and not result['processCompleted']
    assert result['infrastructureErrors']
    sys.exit(0)
child=subprocess.Popen(command,stdout=subprocess.PIPE,stderr=subprocess.PIPE)
if mode=='broken_stdout':
    child.stdout.close()
    error=child.stderr.read()
    code=child.wait()
    sys.stderr.buffer.write(error)
elif mode=='finalize_error':
    first=child.stdout.readline()
    assert first.startswith(b'Managed evidence: '),first
    out=Path(first.decode('utf8').strip().split(': ',1)[1])
    assert out.absolute().is_relative_to(receipt.parent.absolute())
    (out/'result.json').rename(out/'pending-result.json')
    (out/'result.json').mkdir()
    rest,error=child.communicate()
    code=child.returncode
    sys.stdout.buffer.write(first+rest)
    sys.stderr.buffer.write(error)
else:
    raise AssertionError(mode)
receipt.write_text(json.dumps({'wrapperExitCode':code,'stderr':error.decode('utf8',errors='replace')}),encoding='utf8')
sys.exit(code)
'''


def clean_environment(root):
    env = os.environ.copy()
    removed = ['PYTHONUTF8','PYTHONIOENCODING','PYTHONLEGACYWINDOWSSTDIO','PYTHONLEGACYWINDOWSFSENCODING',
               'PYTHONCOERCECLOCALE','LANG','LC_ALL','LC_CTYPE','PYTHONHOME','PYTHONPATH']
    for key in removed:
        env.pop(key, None)
    env['PATH'] = 'C:/Windows/System32' if os.name=='nt' else '/usr/bin:/bin'
    env['PYTHONDONTWRITEBYTECODE'] = '1'
    env['ZEROSLACK_MANAGED_TEST_LOG_ROOT'] = str(root/'m')
    return env, removed


def cmake_quote(value):
    return '[==['+str(value).replace('\\','/')+']==]'


def restore_log_access(root):
    for path in (root/'m').glob('*/*/restore-read-sid.txt'):
        sid = path.read_text(encoding='ascii')
        subprocess.run(['C:/Windows/System32/icacls.exe', str(path.parent/'output.log'), '/remove:d', '*'+sid],
                       stdout=subprocess.DEVNULL, stderr=subprocess.PIPE, check=True)
    for path in (root/'m').glob('*/*/original-output.log'):
        (path.parent/'output.log').rmdir()
        path.rename(path.parent/'output.log')


def verify_contracts(config_path):
    config = read_json(config_path)
    root = Path(config['build'])/'Testing/PolicyContracts'/uuid.uuid4().hex[:12]
    source, build = root/'s', root/'b'
    source.mkdir(parents=True)
    build.mkdir()
    scripts = Path(__file__).absolute().parent
    (source/'child.py').write_text(CHILD, encoding='utf8')
    (source/'driver.py').write_text(DRIVER, encoding='utf8')
    env, removed = clean_environment(root)
    proof = {'status':'running','startedUtc':stamp(),'root':str(root),'checks':[], 'removedEncodingVariables':removed}

    def check(condition, name):
        proof['checks'].append({'name':name, 'passed':bool(condition)})
        require(condition, 'Policy contract failed: '+name+'; evidence: '+str(root))

    try:
        from test_runtime_path_contracts import verify_runtime_path
        verify_runtime_path(config, root, env, check)
        from test_frozen_regex_contracts import verify_frozen_regex
        verify_frozen_regex(config, root, env, check)
        from test_qt_gui_runner_contracts import verify_qt_gui_runner
        verify_qt_gui_runner(config, root, env, check)
        from test_release_reuse import verify_release_reuse
        verify_release_reuse(root, check)
        verify_required_target_mapping(root, check)
        return _verify(config, root, source, build, scripts, env, proof, check)
    except Exception as error:
        proof.update(status='failed', error=str(error))
        raise
    finally:
        restore_log_access(root)
        proof['completedUtc'] = stamp()
        dump(root/'result.json', proof)


def verify_required_target_mapping(root, check):
    from release_gate import verify_required_target
    work = root/'required-target'; work.mkdir()
    artifact = work/'runtime'/'layout.exe'; artifact.parent.mkdir()
    artifact.write_bytes(b'isolated target identity fixture')
    other = work/'layout.exe'; other.write_bytes(b'different root artifact')
    targets = work/'targets.json'
    config = {'targets':str(targets)}
    rule = {'name':'required-layout', 'target':'layout'}
    row = {'name':'layout', 'type':'EXECUTABLE', 'artifact':str(artifact)}
    identity = {str(artifact):digest(artifact)}
    dump(targets, [row])
    check(verify_required_target(rule, config, identity) == artifact.resolve(),
          'required layout accepts the unique configured artifact in a runtime subdirectory')
    for label, entries, binaries in [
        ('missing target', [], identity),
        ('duplicate target', [row,row], identity),
        ('wrong tested artifact', [row], {str(other):digest(other)}),
        ('changed tested artifact', [row], {str(artifact):'0'*64}),
        ('missing artifact', [{**row,'artifact':str(work/'absent.exe')}], identity),
    ]:
        dump(targets, entries)
        try:
            verify_required_target(rule, config, binaries)
        except PolicyError:
            check(True, 'required layout rejects '+label)
        else:
            check(False, 'required layout rejects '+label)


def _verify(config, root, source, build, scripts, env, proof, check):
    modes = {
        'unicode_success':'success', 'unicode_failure':'failure', 'timeout':'timeout',
        'write_error':'write_error', 'bad_metadata':'bad_metadata', 'bad_metadata_failure':'bad_metadata_failure',
        'read_error':'read_error', 'broken_stdout':'success', 'broken_stdout_failure':'failure',
        'finalize_error':'success', 'malformed_spec':'success', 'unlisted':'success',
        'restricted':'forbidden', 'restricted_unwritable':'forbidden', 'restricted_unsafe':'forbidden',
        'log_open_error':'forbidden'}
    restricted = {'restricted','restricted_unwritable','restricted_unsafe'}
    dates = {'managedSince':date.today().isoformat(), 'reviewBy':(date.today()+timedelta(days=2)).isoformat(),
             'reviewOwner':'ZeroSlack policy contracts'}
    policy = {'schema':'zeroslack.test-execution-policy/v1', **dates,
              'scope':{'id':'isolated-policy-contracts','source':'scripts/test_policy_contracts.py'},
              'guard':'unicode_success', 'requiredMatrices':[], 'tests':[],
              'resources':[{'path':'private-fixture','operation':'deny_access','source':'isolated contract',
                            'reason':'No fixture access is needed to reject a command',**dates}]}
    commands = {}
    spec_root = build/'Testing/ManagedCommands'
    spec_root.mkdir(parents=True)
    cmake = ['cmake_minimum_required(VERSION 3.30)', 'project(PolicyContracts NONE)', 'enable_testing()']
    unwritable = root/'not-a-directory'
    unwritable.write_text('preserve this file', encoding='utf8')
    for name, mode in modes.items():
        rule = {'name':name, **dates, 'source':'child.py', 'when':{}, 'role':'script','script':'child.py',
                'execution':'restricted' if name in restricted else 'run',
                'reason':{'category':'user_restriction' if name in restricted else 'self_contained',
                          'detail':'原始限制原因：不得启动此命令。' if name in restricted else 'Isolated real CTest contract'},
                'timeout':0.3 if name=='timeout' else 10, 'timeoutBasis':'Small isolated process contract'}
        if name in restricted:
            rule['prerequisites']=[{'path':'private-fixture', 'kind':'directory'}]
        policy['tests'].append(rule)
        spec = {'name':'not_listed' if name=='unlisted' else name, 'source':str(source), 'build':str(build),
                'command':[sys.executable, str(source/'child.py'), mode, str(root/(name+'-launched.txt'))]}
        spec_path = spec_root/(name+'.json')
        dump(spec_path, spec)
        if name=='malformed_spec':
            spec_path.write_text('{malformed-json', encoding='utf8')
        command = [sys.executable, str(scripts/'test_policy.py'), 'invoke', '--policy',str(source/'policy.json'),
                   '--spec', str(spec_path)]
        driver = 'broken_stdout' if name.startswith('broken_stdout') else name if name in {'finalize_error','log_open_error'} else None
        if driver:
            target = spec['command'] if driver=='log_open_error' else command
            command = [sys.executable, str(source/'driver.py'),driver,str(root/(name+'-driver.json')),str(scripts),*target]
        commands[name] = command
        cmake.append('add_test(NAME '+name+' COMMAND '+' '.join(cmake_quote(arg) for arg in command)+')')
        cmake.append('set_tests_properties('+name+' PROPERTIES TIMEOUT 20)')
        if name in {'restricted_unwritable','restricted_unsafe'}:
            destination = unwritable if name=='restricted_unwritable' else root/'private-fixture/evidence'
            cmake.append('set_tests_properties('+name+' PROPERTIES ENVIRONMENT '+
                         cmake_quote('ZEROSLACK_MANAGED_TEST_LOG_ROOT='+str(destination))+')')
    (source/'CMakeLists.txt').write_text('\n'.join(cmake)+'\n', encoding='utf8')
    dump(source/'policy.json', policy)
    mini_config = {'source':str(source), 'build':str(build), 'policy':str(source/'policy.json'), 'configuration':{}}
    dump(build/'managed-test-config.json', mini_config)
    encoding = subprocess.run([sys.executable, '-B', '-c',
        'import sys,os,json; print(json.dumps(dict(stdout=sys.stdout.encoding,stderr=sys.stderr.encoding,utf8Mode=sys.flags.utf8_mode,pythonUtf8=os.environ.get("PYTHONUTF8"),pythonIoEncoding=os.environ.get("PYTHONIOENCODING"))))'],
        env=env, capture_output=True, timeout=10)
    check(encoding.returncode==0, 'encoding probe executes')
    proof['encodingProbe'] = json.loads(encoding.stdout)
    check(proof['encodingProbe']['pythonUtf8'] is None and proof['encodingProbe']['pythonIoEncoding'] is None,
          'no caller encoding overrides')
    proof['configure'] = run([str(Path(config['ctest']).with_name('cmake.exe' if os.name=='nt' else 'cmake')),
        '-S', str(source), '-B', str(build), '-G', 'Ninja', '-DCMAKE_MAKE_PROGRAM='+config['ninja']],
        root, env, root/'configure.log', 30)
    check(proof['configure']['exitCode']==0, 'real isolated CMake configuration')
    proof['ctest'] = run([config['ctest'],'--test-dir',str(build),'-V','-j','1',
        '--test-output-size-passed','1048576','--test-output-size-failed','1048576',
        '--output-junit',str(root/'ctest.xml')], root, env, root/'ctest.log', 90)
    restore_log_access(root)
    cases = {case.attrib['name']:case for case in ET.parse(root/'ctest.xml').getroot().iter('testcase')}
    expected_failures = {'unicode_failure','timeout','bad_metadata_failure','broken_stdout_failure',
                         'malformed_spec','unlisted',*restricted}
    failed = {name for name,case in cases.items() if case.find('failure') is not None}
    check(set(cases)==set(modes) and failed==expected_failures and proof['ctest']['exitCode']==8,
          'real CTest success/failure set matches completed process outcomes')
    check(all(case.find('skipped') is None for case in cases.values()), 'no CTest skip conversion')
    proof['expectedCtestFailures'] = sorted(expected_failures)
    return _check_results(root, source, build, cases, policy, mini_config, proof, check)


def _check_results(root, source, build, cases, policy, config, proof, check):
    from release_gate import add_required_coverage, verify_managed_result, verify_required_result

    rules = {rule['name']:rule for rule in policy['tests']}
    records, paths = {}, {}
    for name in cases:
        candidates = [path for path in (root/'m'/name).glob('*/result.json') if path.is_file()]
        if candidates:
            check(len(candidates)==1, name+': unique managed result')
            paths[name] = candidates[0]
            records[name] = read_json(candidates[0])
    policy_hash = digest(source/'policy.json')
    expected_echo = PAYLOAD.decode('utf8', errors='replace')
    for name in ('unicode_failure','timeout'):
        actual = dict(records[name])
        add_required_coverage({**rules[name], 'required':True}, actual, [paths[name]], config, policy_hash)
        check(actual['classification']==records[name]['classification'] and not actual['infrastructureErrors'],
              name+': required coverage acceptance retains the actual failure/timeout classification')
    for name, code in [('unicode_success',0),('unicode_failure',7),('timeout',124)]:
        result = records[name]
        check(result['exitCode']==code and result['processCompleted'] and result['evidenceComplete'],
              name+': completed child result preserved')
        check(result['classification']==('timeout' if code==124 else 'passed' if code==0 else 'failed'),
              name+': correct process classification')
        verified = verify_managed_result(rules[name], paths[name], config, policy_hash)
        check(verified['exitCode']==code, name+': complete evidence accepted by direct gate consumer')
        if name!='timeout':
            check(expected_echo in cases[name].findtext('system-out',''), name+': complete Unicode echo through real CTest')
            check(Path(result['log']).read_bytes()==PAYLOAD, name+': all original log bytes preserved')

    for name, code in [('bad_metadata',0),('bad_metadata_failure',7),('read_error',0),
                       ('broken_stdout',0),('broken_stdout_failure',7)]:
        result = records[name]
        check(result['exitCode']==code and result['classification']==('passed' if code==0 else 'failed'),
              name+': observation failure cannot change completed child outcome')
        check(not result['evidenceComplete'] and bool(result['infrastructureErrors']), name+': infrastructure errors recorded separately')
        output = cases[name].findtext('system-out','')
        check('TEST INFRASTRUCTURE ERROR' in output and 'TEST POLICY BLOCKED' not in output,
              name+': explicit infrastructure diagnostic instead of policy rejection')
        try:
            verify_required_result(rules[name], paths[name], build, policy_hash)
        except PolicyError as error:
            check('missing/incomplete managed evidence' in str(error), name+': required/preparation consumer rejects incomplete evidence')
        else:
            check(False, name+': incomplete evidence must block preparation')
        if name.startswith('broken_stdout'):
            check(read_json(root/(name+'-driver.json'))['wrapperExitCode']==code,
                  name+': actual pipe failure does not become interpreter exit 120')

    for name in ('write_error','finalize_error'):
        check(name not in records, name+': failed persistence is not presented as complete evidence')
        output = cases[name].findtext('system-out','')
        check('TEST INFRASTRUCTURE ERROR' in output and 'TEST POLICY BLOCKED' not in output,
              name+': actual filesystem error is self-identified')
        check(expected_echo in output, name+': persistence failure does not truncate log echo')
        if name=='write_error':
            check('Managed evidence unavailable:' in output, 'failed initial write is not claimed as persisted evidence')
        else:
            check(read_json(root/(name+'-driver.json'))['wrapperExitCode']==0, 'actual final write failure preserves wrapper exit 0')
            pending = list((root/'m'/name).glob('*/pending-result.json'))
            check(len(pending)==1 and read_json(pending[0])['evidencePending'] is True,
                  'interrupted evidence finalization leaves an incomplete receipt')
            try:
                verify_required_result(rules[name], pending[0], build, policy_hash)
            except PolicyError as error:
                check('missing/incomplete managed evidence' in str(error), 'pending receipt cannot authorize preparation')
            else:
                check(False, 'pending receipt rejected')

    restricted = records['restricted']
    check(restricted['classification']=='restricted' and restricted['exitCode']==2
          and not restricted['processStarted'] and restricted['evidenceComplete'], 'restricted record is distinct and remains nonzero')
    check(restricted['reason']['detail']==rules['restricted']['reason']['detail']
          and restricted['policySha256']==policy_hash and bool(restricted['timeUtc']),
          'restricted record preserves exact reason, policy identity and timestamp')
    check(verify_managed_result(rules['restricted'], paths['restricted'], config, policy_hash)['classification']=='restricted',
          'restricted evidence has its own validated classification')
    for name in ('restricted','restricted_unwritable','restricted_unsafe','log_open_error'):
        check(not (root/(name+'-launched.txt')).exists(), name+': command never launched')
    for name in ('restricted_unwritable','restricted_unsafe'):
        output = cases[name].findtext('system-out','')
        check('TEST POLICY BLOCKED' in output and 'TEST INFRASTRUCTURE ERROR' in output
              and 'Managed evidence:' not in output, name+': no false persistence claim when rejection evidence cannot be written')
    check(not (root/'private-fixture').exists() and (root/'not-a-directory').read_text(encoding='utf8')=='preserve this file',
          'protected output rejected before directory creation; existing file preserved')
    check('TEST INFRASTRUCTURE ERROR' in cases['malformed_spec'].findtext('system-out','')
          and 'TEST POLICY BLOCKED' not in cases['malformed_spec'].findtext('system-out',''), 'JSON parse failure is infrastructure')
    check('TEST POLICY BLOCKED' in cases['unlisted'].findtext('system-out','')
          and 'TEST INFRASTRUCTURE ERROR' not in cases['unlisted'].findtext('system-out',''), 'unlisted invocation is a policy rejection')
    opening = read_json(root/'log_open_error-driver.json')
    check(opening['exitCode']==125 and not opening['processStarted'] and bool(opening['infrastructureErrors']),
          'actual log open failure never invents a successful child result')

    # Consume real successful evidence, then remove its log temporarily. The
    # preparation verifier must fail closed even though the process really passed.
    good = records['unicode_success']
    log = Path(good['log'])
    moved = log.with_name('withheld-output.log')
    check(log.absolute().is_relative_to(root.absolute()), 'negative evidence probe stays in the isolated project')
    log.rename(moved)
    try:
        try:
            verify_managed_result(rules['unicode_success'], paths['unicode_success'], config, policy_hash)
        except (OSError, PolicyError):
            check(True, 'missing original log blocks acceptance of a real passed process')
        else:
            check(False, 'missing log must block acceptance')
    finally:
        moved.rename(log)
    try:
        verify_managed_result(rules['unicode_success'], paths['unicode_success'], config, '0'*64)
    except PolicyError as error:
        check('identity mismatch' in str(error), 'wrong policy identity blocks actual passed evidence')
    else:
        check(False, 'wrong policy identity must block acceptance')
    proof.update(status='passed', ctestRegistrations=len(cases), managedResults={name:str(path) for name,path in paths.items()},
                 policySha256=policy_hash, ctestLogSha256=digest(root/'ctest.log'), ctestXmlSha256=digest(root/'ctest.xml'))
    return {'status':'passed','checks':len(proof['checks']),'ctestRegistrations':len(cases),'evidence':str(root/'result.json')}
