"""Single policy implementation used by ALL, CTest, and release preparation."""
import argparse
from datetime import date, datetime, timezone
import hashlib
import json
import os
from pathlib import Path, PurePosixPath
import re
import shutil
import subprocess
import sys
import uuid

from validation_process import infrastructure_error, run, silence_broken_stream, write_utf8


class PolicyError(RuntimeError):
    pass


def require(condition, message):
    if not condition:
        raise PolicyError(message)


def read_json(path):
    return json.loads(Path(path).read_text(encoding='utf-8-sig'))


def dump(path, value):
    path = Path(path)
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(value, ensure_ascii=False, indent=2)+'\n', encoding='utf8')


def digest(path):
    with Path(path).open('rb') as stream:
        return hashlib.file_digest(stream, 'sha256').hexdigest()


def stamp():
    return datetime.now(timezone.utc).isoformat()


def relative_name(name):
    normalized = name.replace('\\', '/')
    path = PurePosixPath(normalized)
    require(normalized and not path.is_absolute() and ':' not in normalized and '..' not in path.parts,
            f'Unsafe relative path: {name}')
    return normalized


def blocked(policy, name, operations=('deny_access',)):
    normalized = name.replace('\\', '/').strip('/').casefold()
    for rule in policy['resources']:
        candidate = rule['path'].casefold()
        if rule['operation'] in operations and (normalized == candidate or normalized.startswith(candidate+'/')):
            return rule
    return None


def safe_path(root, name, policy, operations=('deny_access',)):
    name = relative_name(name)
    require(not blocked(policy, name, operations), f'Resource restricted before probing: {name}')
    root = Path(root).absolute()
    current = root
    # The restriction check must precede all filesystem probes, including junction checks.
    for part in PurePosixPath(name).parts:
        current = current / part
        require(not current.is_symlink() and not current.is_junction(), f'Path redirection is unsupported: {current}')
    return current


def external_tool_path(value, policy):
    path = Path(value)
    require(path.is_absolute(), f'Runtime tool path must be absolute: {value}')
    normalized = path.as_posix().casefold().rstrip('/')
    for restriction in policy['resources']:
        if restriction['operation'] in {'deny_access', 'hash_only'}:
            needle = '/'+restriction['path'].casefold()
            require(needle not in normalized, f"Runtime tool references restricted {restriction['path']}")
    return safe_path(path.anchor, path.relative_to(path.anchor).as_posix(), policy, ('deny_access', 'hash_only'))


def runtime_tools(rule, config, command, policy):
    names = rule.get('runtimeTools', [])
    if not names:
        return {}
    # Required tools must be supplied by the command's own environment. An outer
    # CTest PATH cannot repair an inner `cmake -E env PATH=...` replacement.
    require(command[1:3]==['-E','env'], f"{rule['name']}: runtime tools require an explicit command environment")
    assignments = []
    for argument in command[3:]:
        if re.match(r'^[A-Za-z_][A-Za-z_0-9]*=', argument):
            assignments.append(argument)
        else:
            break
    paths = [value.split('=',1)[1].replace('\\;', ';') for value in assignments if value.upper().startswith('PATH=')]
    require(len(paths)==1, f"{rule['name']}: runtime tools require one explicit PATH")
    for directory in paths[0].split(os.pathsep):
        external_tool_path(directory, policy)
    result = {}
    for name in names:
        tool = config.get('runtimeTools', {}).get(name, {})
        require(tool.get('path') and tool.get('sha256'), f"{rule['name']}: missing configured runtime tool {name}")
        path = external_tool_path(tool['path'], policy)
        require(path.is_file(), f"{rule['name']}: missing runtime tool {name}: {path}")
        require(digest(path)==tool['sha256'], f"{rule['name']}: runtime tool changed: {name}")
        resolved = shutil.which(name, path=paths[0])
        require(resolved and Path(resolved).absolute()==path.absolute(),
                f"{rule['name']}: explicit PATH does not resolve the configured runtime tool {name}")
        result[name] = {'path':str(path), 'sha256':tool['sha256']}
    return result


def dates(item, label):
    try:
        start = date.fromisoformat(item['managedSince'])
        end = date.fromisoformat(item['reviewBy'])
    except (KeyError, ValueError, TypeError) as error:
        raise PolicyError(f'{label}: missing/invalid managedSince or reviewBy') from error
    require(start <= date.today() <= end and start <= end, f'{label}: future start or expired review ({start}, {end})')
    require(bool(item.get('reviewOwner')), f'{label}: reviewOwner missing')
    return start, end


def load_policy(path):
    policy = read_json(path)
    require(policy.get('schema') == 'zeroslack.test-execution-policy/v1', 'Unsupported policy schema')
    dates(policy, 'policy')
    require(policy.get('scope', {}).get('id') and policy['scope'].get('source'), 'Policy authorization scope missing')
    names = set()
    categories = {'self_contained', 'user_restriction', 'restricted_resource', 'external_tool', 'manual', 'duration', 'flaky', 'layout_contract', 'policy_guard'}
    for entry in policy['tests']:
        name = entry.get('name')
        require(name and name not in names, f'Duplicate/missing CTest name: {name}')
        names.add(name)
        start, end = dates(entry, name)
        require(entry.get('reason', {}).get('category') in categories and entry['reason'].get('detail'), f'{name}: invalid reason')
        require(entry.get('source') and isinstance(entry.get('when'), dict), f'{name}: source/configuration missing')
        require(entry.get('role') in {'compiled', 'script'}, f'{name}: invalid role')
        require(entry.get('target') if entry['role']=='compiled' else entry.get('script'), f'{name}: target/script mapping missing')
        require(entry.get('execution') in {'run', 'excluded', 'restricted'}, f'{name}: invalid execution rule')
        require(isinstance(entry.get('timeout'), (int, float)) and 0 < entry['timeout'] <= 1200, f'{name}: invalid timeout')
        require(entry.get('timeoutBasis'), f'{name}: timeout basis missing')
        tools = entry.get('runtimeTools', [])
        require(isinstance(tools,list) and all(isinstance(x,str) and x=='git' for x in tools)
                and len(tools)==len(set(tools)), f'{name}: invalid runtime tool dependency')
        if 'recoveryStorage' in entry:
            storage = entry['recoveryStorage']
            require(isinstance(storage,dict) and storage.get('owner')=='fixture'
                    and storage.get('source') and storage.get('reason') and entry['role']=='compiled',
                    f'{name}: fixture recovery storage needs an explicit owner and evidence')
        if entry.get('variant'):
            variant=entry['variant']
            require(variant.get('scope')==policy['scope']['id'] and isinstance(variant.get('arguments'),list)
                    and variant['arguments'] and all(isinstance(x,str) for x in variant['arguments']),
                    f'{name}: invalid or out-of-scope allowed variant')
        if entry.get('required'):
            require(entry['execution']=='run' and not entry.get('disabled'), f'{name}: required/exclusion conflict')
            require(entry.get('coverage', {}).get('cases') and entry['coverage'].get('dpr') in [1, 1.5, 2], f'{name}: empty/missing required coverage')
        if entry['reason']['category']=='flaky':
            proof = entry.get('flaky', {})
            require(isinstance(proof.get('runs'), int) and isinstance(proof.get('failures'), int)
                    and 0 < proof['failures'] < proof['runs'] and (end-start).days <= 30, f'{name}: invalid flaky counts/deadline')
            require(all(proof.get(k) for k in ['sourceSha256','binarySha256','configurationSha256','inputsSha256','isolation','evidence']), f'{name}: flaky identity/evidence missing')
            evidence=safe_path(Path(path).parent.parent, proof['evidence'], policy)
            require(evidence.is_file(), f'{name}: flaky evidence file missing')
            runs=read_json(evidence)
            require(isinstance(runs,list) and len(runs)==proof['runs'], f'{name}: flaky evidence run count mismatch')
            require(all(isinstance(run.get('exitCode'),int) and all(run.get(key)==proof[key]
                        for key in ['sourceSha256','binarySha256','configurationSha256','inputsSha256','isolation'])
                        for run in runs), f'{name}: flaky runs do not have the same identities/conditions')
            require(sum(run['exitCode']!=0 for run in runs)==proof['failures'], f'{name}: flaky evidence failure count mismatch')
            for run in runs:
                log=safe_path(Path(path).parent.parent,run.get('log',''),policy)
                require(log.is_file() and digest(log)==run.get('logSha256'), f'{name}: flaky execution log missing/changed')
    guard = next((x for x in policy['tests'] if x['name']==policy.get('guard')), None)
    require(guard and guard['execution']=='run' and not guard.get('disabled') and guard['role']=='script', 'Guard missing or excluded')
    required_names = {x['name'] for x in policy['tests'] if x.get('required')}
    matrix_names = set()
    for matrix in policy.get('requiredMatrices', []):
        entries = [x for x in policy['tests'] if x['name'] in matrix['tests']]
        require(len(entries)==3 and all(x.get('required') for x in entries)
                and {x['coverage']['dpr'] for x in entries}=={1,1.5,2}, f"{matrix['id']}: required DPR matrix incomplete")
        require(all(x['target']==matrix['target'] for x in entries), f"{matrix['id']}: required target mismatch")
        require(all(x['coverage']['cases']==entries[0]['coverage']['cases'] for x in entries), f"{matrix['id']}: mode/state coverage mismatch")
        require(not matrix_names.intersection(matrix['tests']), 'Duplicate required matrix registration')
        matrix_names.update(matrix['tests'])
    require(matrix_names==required_names, 'Required matrix mapping missing or contradictory')
    resources = set()
    for rule in policy['resources']:
        name = relative_name(rule['path'])
        require(name not in resources, f'Duplicate resource: {name}')
        resources.add(name)
        dates(rule, name)
        require(rule.get('operation') in {'deny_access','hash_only','preserve'} and rule.get('source') and rule.get('reason'), f'{name}: invalid resource rule')
    return policy


def applicable(entry, configuration):
    for key, value in entry['when'].items():
        require(key in configuration, f"{entry['name']}: unknown configuration condition {key}")
        if isinstance(value, dict):
            require(set(value)=={'nonEmpty'} and isinstance(value['nonEmpty'], bool), f"{entry['name']}: invalid condition")
            if bool(configuration[key]) != value['nonEmpty']:
                return False
        elif configuration[key] != value:
            return False
    return True


def inventory(config):
    result = subprocess.run([config['ctest'], '--test-dir', config['build'], '--show-only=json-v1'],
                            capture_output=True, text=True, encoding='utf8', timeout=30)
    require(result.returncode==0, f'CTest inventory failed: {result.stderr}')
    return json.loads(result.stdout)


def origin(test, inv):
    graph = inv['backtraceGraph']
    node = graph['nodes'][test['backtrace']]
    while graph['files'][node['file']].replace('\\','/').endswith('/TestExecution.cmake') and 'parent' in node:
        node=graph['nodes'][node['parent']]
    return f"{graph['files'][node['file']]}:{node.get('line', '?')}"


def properties(test):
    return {p['name']:p['value'] for p in test.get('properties', [])}


def executable_in(command, artifacts):
    matches = []
    for target, data in artifacts.items():
        for value in command:
            value = value.split('=', 1)[-1] if value.startswith('-D') and '=' in value else value
            if Path(value).as_posix().casefold()==Path(data['artifact']).as_posix().casefold():
                matches.append(target)
    return list(dict.fromkeys(matches))


def graph_identity(text):
    nodes = {match[1]: (match[2], match[3]) for match in re.finditer(
        r'^"([^"]+)" \[label="((?:\\.|[^"\\])*)"([^\r\n]*)\]$', text, re.M)}
    edges = [match.groups() for match in re.finditer(r'^"([^"]+)" -> "([^"]+)"([^\r\n]*)$', text, re.M)]
    require(nodes and edges, 'Default graph has no identifiable nodes/edges')
    require(all(start in nodes for start,_,_ in edges), 'Default graph edge has an unknown source')
    outgoing, incoming = {}, {}
    for start,end,_ in edges:
        outgoing.setdefault(start,[]).append(end)
        incoming.setdefault(end,[]).append(start)
    keys = {}
    for node,(label,attributes) in nodes.items():
        if 'shape=ellipse' in attributes:
            outputs = sorted(nodes[end][0] for end in outgoing.get(node,[]) if end in nodes)
            require(outputs, 'Default graph rule has no output')
            identity = ['rule',label,outputs]
        else:
            identity = ['file',label]
        keys[node] = json.dumps(identity,ensure_ascii=False,separators=(',',':'))
    require(len(set(keys.values()))==len(keys), 'Default graph has ambiguous node identities')
    # Ninja omits declarations for unused secondary outputs. Retain their
    # multiplicity and producing rule without hashing process-local addresses.
    for node in sorted({end for _,end,_ in edges} - set(nodes)):
        producers = incoming[node]
        require(len(producers)==1 and 'shape=ellipse' in nodes[producers[0]][1],
                'Default graph has an unidentifiable secondary output')
        keys[node] = json.dumps(['secondary-output',keys[producers[0]]],separators=(',',':'))
        nodes[node] = ('','undeclared-secondary-output')
    canonical = {'nodes':sorted((keys[node],attributes) for node,(_,attributes) in nodes.items()),
                 'edges':sorted((keys[start],keys[end],attributes) for start,end,attributes in edges)}
    return hashlib.sha256(json.dumps(canonical,ensure_ascii=False,sort_keys=True,separators=(',',':')).encode('utf8')).hexdigest()


def guard(config_path, output=None):
    config = read_json(config_path)
    policy = load_policy(config['policy'])
    require(config['generator']=='Ninja' and config['configuration'].get('CMAKE_BUILD_TYPE')=='Release', 'Unsupported generator/configuration; require Ninja/Release')
    require(config['configuration'].get('BUILD_TESTING')=='ON', 'BUILD_TESTING must be ON')
    inv = inventory(config)
    actual = {x['name']:x for x in inv['tests']}
    active = {x['name']:x for x in policy['tests'] if applicable(x, config['configuration'])}
    inactive = [x['name'] for x in policy['tests'] if x['name'] not in active]
    require(set(actual)==set(active), f'CTest/policy mismatch: unlisted={sorted(set(actual)-set(active))}; missing={sorted(set(active)-set(actual))}')
    targets = read_json(config['targets'])
    graph = subprocess.run([config['ninja'], '-C', config['build'], '-t', 'graph', 'all'], capture_output=True, text=True, encoding='utf8', timeout=30)
    require(graph.returncode==0, f'Default graph unavailable: {graph.stderr}')
    labels = set(re.findall(r'\[label="((?:\\.|[^"\\])*)"', graph.stdout))
    artifacts = {x['name']:x for x in targets if x['type']=='EXECUTABLE'}
    compiled_tests = [x for x in targets if x.get('testTarget')]
    for target in compiled_tests:
        relative = os.path.relpath(target['artifact'], config['build']).replace('\\','/')
        require(relative in labels or target['artifact'].replace('\\','/') in labels,
                f"Default ALL cannot reach {target['name']} ({relative})")
    require('test_execution_policy_check' in labels, 'Default ALL does not include policy guard')
    build_guard = subprocess.run([config['ninja'], '-C', config['build'], '-t', 'commands', 'test_execution_policy_check'],
                                 capture_output=True,text=True,encoding='utf8',timeout=30)
    require(build_guard.returncode==0, 'Cannot inspect default guard command')
    guard_script = (Path(config['source'])/'scripts/test_policy.py').as_posix()
    guard_config = (Path(config['build'])/'managed-test-config.json').as_posix()
    normalized_command=build_guard.stdout.replace('\\','/').replace('"','')
    require(f'{guard_script} guard --config {guard_config}' in normalized_command, 'Default guard command no longer invokes the shared policy implementation')
    mappings = []
    for name, rule in active.items():
        test = actual[name]
        where = origin(test, inv)
        props = properties(test)
        require(not props.get('DISABLED'), f'{name} ({where}): disabled registration')
        require(props.get('TIMEOUT') == rule['timeout'], f'{name} ({where}): missing/mismatched explicit TIMEOUT')
        require('--spec' in test.get('command',[]) and 'invoke' in test.get('command',[]), f'{name} ({where}): missing managed precondition wrapper')
        spec_path = test['command'][test['command'].index('--spec')+1]
        spec = read_json(spec_path)
        require(spec['name']==name and spec['source']==config['source'], f'{name} ({where}): command identity mismatch')
        command = spec['command']
        if name==policy['guard']:
            require(len(command)==6 and Path(command[1]).as_posix()==guard_script
                    and command[2:4]==['guard','--config'] and Path(command[4]).as_posix()==guard_config
                    and command[5]=='--contracts',
                    f'{name} ({where}): guard command/variant mismatch')
        if rule['role']=='compiled':
            require(rule['target'] in artifacts, f'{name} ({where}): unknown target {rule["target"]}')
            require(rule['target'] in executable_in(command, artifacts), f'{name} ({where}): executable/target mapping mismatch')
            if not rule.get('runtimeException'):
                environments = '\n'.join(command + props.get('ENVIRONMENT_MODIFICATION', []) + props.get('ENVIRONMENT', []))
                require(config['qtBin'] in environments.replace('\\','/') and config['compilerBin'] in environments.replace('\\','/'), f'{name} ({where}): registration missing Qt/MinGW PATH')
        else:
            require(any(rule['script'] in value.replace('\\','/') for value in command), f'{name} ({where}): script command mismatch')
        if rule.get('variant'):
            arguments = command[command.index(next(x for x in command if Path(x).name.casefold()==Path(artifacts[rule['target']]['artifact']).name.casefold()))+1:]
            expected = [v.replace('${SOURCE}',config['source']) for v in rule['variant']['arguments']]
            require(arguments==expected, f'{name} ({where}): allowed variant arguments mismatch')
            require(rule['variant'].get('scope')==policy['scope']['id'], f'{name}: variant authorization scope mismatch')
        try:
            tools = runtime_tools(rule, config, command, policy)
        except PolicyError as error:
            raise PolicyError(f'{where}: {error}') from error
        mappings.append({'name':name, 'target':rule.get('target'), 'script':rule.get('script'), 'origin':where,
                         'execution':rule['execution'], 'runtimeTools':tools})
    result = {'status':'passed','timeUtc':stamp(),'policySha256':digest(config['policy']),
              'registeredTests':len(actual),'compiledTestTargets':len(compiled_tests),'reachableTestTargets':len(compiled_tests),
              'scriptTests':sum(x['role']=='script' for x in active.values()),'notApplicable':inactive,
              'mappings':mappings,'inventory':inv,'defaultGraphSha256':graph_identity(graph.stdout),
              'defaultGraphIdentityFormat':'labeled-nodes-and-edges/v1',
              'rawGraphSha256':hashlib.sha256(graph.stdout.encode()).hexdigest()}
    if output:
        dump(output, result)
    return result


def evidence_directory(spec, rule, policy):
    name = relative_name(rule['name'])
    require('/' not in name, 'Evidence rule name must be a single path component')
    base = Path(os.environ.get('ZEROSLACK_MANAGED_TEST_LOG_ROOT', str(Path(spec['build'])/'Testing/Managed'))).absolute()
    normalized = base.as_posix().casefold().rstrip('/')
    # Reject protected output destinations lexically before even checking a
    # parent for links/junctions. Preserve/hash-only resources are not writable.
    for resource in policy['resources']:
        needle = '/'+resource['path'].casefold()
        require(needle not in normalized, f"Evidence destination references protected {resource['path']}")
    base = safe_path(base.anchor, base.relative_to(base.anchor).as_posix(), policy)
    out = safe_path(base, name+'/'+str(uuid.uuid4()), policy)
    out.mkdir(parents=True)
    return out


def publish_result(out, result, echo_log=True):
    # A pending receipt is deliberately fail-closed if the final write fails.
    # All post-process operations are observations; none own the process code.
    result.update(evidenceComplete=False, evidencePending=True)
    persisted = False
    try:
        dump(out/'result.json', result)
        persisted = True
    except Exception as error:
        infrastructure_error(result, 'evidence_write', error)
    try:
        label = 'Managed evidence' if persisted else 'Managed evidence unavailable'
        write_utf8(sys.stdout, f'{label}: {out}\n')
    except Exception as error:
        infrastructure_error(result, 'evidence_echo', error)
        silence_broken_stream(sys.stdout)
    if echo_log:
        try:
            text = (out/'output.log').read_text(encoding='utf8', errors='replace')
        except Exception as error:
            infrastructure_error(result, 'log_read', error)
        else:
            try:
                write_utf8(sys.stdout, text+'\n')
            except Exception as error:
                infrastructure_error(result, 'log_echo', error)
                silence_broken_stream(sys.stdout)
    result['evidencePending'] = False
    result['evidenceComplete'] = not result['infrastructureErrors'] and (
        result['processCompleted'] or result['classification']=='restricted')
    if persisted:
        try:
            dump(out/'result.json', result)
        except Exception as error:
            infrastructure_error(result, 'evidence_finalize', error)
    return result['exitCode']


def observe_qt_gui_result(result, directory):
    """Keep wrapper infrastructure failures distinct from Qt assertions."""
    try:
        contexts = list(Path(directory).glob('*/*/invocation.txt'))
        require(len(contexts) == 1, 'Expected one Qt invocation context; found '+str(len(contexts)))
        report = contexts[0].parent
        phase = (report/'phase.txt').read_text(encoding='utf8').strip()
        code_path = report/'exit-code.txt'
        code = code_path.read_text(encoding='utf8').strip() if code_path.is_file() else None
        result['qtInvocation'] = {'directory':str(report), 'phase':phase,
                                  'rawExitCode':code, 'context':str(contexts[0])}
        error_path = report/'infrastructure-error.txt'
        require(not error_path.is_file(), error_path.read_text(encoding='utf8') if error_path.is_file() else '')
        require(result['timeout'] or phase in {'complete','process_failed'},
                'Qt runner did not finalize evidence; last phase='+phase+'; '+str(report))
    except Exception as error:
        infrastructure_error(result, 'qt_gui_evidence', error)


def invoke(policy_path, spec_path):
    policy = load_policy(policy_path)
    spec = read_json(spec_path)
    rule = next((x for x in policy['tests'] if x['name']==spec['name']), None)
    require(rule, f"Unlisted test: {spec['name']}")
    if rule['execution']=='restricted':
        result = {'schema':'zeroslack.managed-test-result/v2', 'name':rule['name'],
                  'classification':'restricted', 'reason':rule['reason'], 'timeUtc':stamp(),
                  'policySha256':digest(policy_path), 'specSha256':digest(spec_path),
                  'exitCode':2, 'timeout':False, 'processStarted':False, 'processCompleted':False,
                  'infrastructureErrors':[]}
        try:
            write_utf8(sys.stderr, f"TEST POLICY BLOCKED: {rule['name']}: restricted before probing/launch: {rule['reason']['detail']}\n")
        except Exception as error:
            infrastructure_error(result, 'policy_echo', error)
            silence_broken_stream(sys.stderr)
        try:
            out = evidence_directory(spec, rule, policy)
        except Exception as error:
            infrastructure_error(result, 'restricted_evidence_directory', error)
            return 2
        result.update(invocationId=out.name, sourceTree=spec['source'], buildDirectory=spec['build'])
        return publish_result(out, result, echo_log=False)
    command = spec['command']
    for arg in command:
        normalized = arg.replace('\\','/').casefold()
        for restriction in policy['resources']:
            if restriction['operation']=='deny_access':
                needle='/'+restriction['path'].casefold()
                require(not (needle in normalized or normalized==restriction['path'].casefold()), f"{rule['name']}: command references restricted {restriction['path']}")
    config=read_json(Path(spec['build'])/'managed-test-config.json')
    require(spec['source']==config['source'] and Path(policy_path).absolute()==Path(config['policy']).absolute(), f"{rule['name']}: invocation source/policy identity mismatch")
    require(applicable(rule,config['configuration']), f"{rule['name']}: invocation is not applicable to this configuration")
    if rule['role']=='compiled':
        targets={x['name']:x for x in read_json(config['targets']) if x['type']=='EXECUTABLE'}
        require(rule['target'] in executable_in(command,targets), f"{rule['name']}: invocation target mismatch")
        if rule.get('variant'):
            executable=targets[rule['target']]['artifact']
            index=next((i for i,value in enumerate(command) if Path(value).as_posix()==Path(executable).as_posix()),None)
            expected=[value.replace('${SOURCE}',config['source']) for value in rule['variant']['arguments']]
            require(index is not None and command[index+1:]==expected, f"{rule['name']}: exact allowed variant required")
    else:
        require(any(rule['script'] in value.replace('\\','/') for value in command), f"{rule['name']}: invocation script mismatch")
    for prerequisite in rule.get('prerequisites', []):
        path = safe_path(spec['source'], prerequisite['path'], policy)
        require(path.is_file() if prerequisite['kind']=='file' else path.is_dir(), f"{rule['name']}: missing prerequisite: {path}")
    tools = runtime_tools(rule, config, command, policy)
    out = evidence_directory(spec, rule, policy)
    artifacts = out/'artifacts'
    artifacts.mkdir()
    profile = out/'profile'
    settings = profile/'settings'
    (settings/'system').mkdir(parents=True)
    env = os.environ.copy()
    env.update(ZEROSLACK_TEST_ARTIFACT_DIR=str(artifacts), ZEROSLACK_QT_TEST_LOG_ROOT=str(out/'QtGui'),
               ZEROSLACK_MANAGED_SETTINGS_ROOT=str(settings), ZEROSLACK_PROFILE_ROOT=str(profile),
               ZEROSLACK_SESSION_STORAGE_PATH=str(profile/'workspace-sessions.ini'),
               ZEROSLACK_EDITING_TIME_STORAGE_PATH=str(profile/'editing-time.json'),
               ZEROSLACK_SIMULATION_CACHE_ROOT=str(profile/'simulation'),
               ZEROSLACK_SIMDOCK_LEGACY_SETTINGS_PATH=str(profile/'simdock-legacy.ini'))
    if rule.get('recoveryStorage',{}).get('owner')=='fixture':
        env.pop('ZEROSLACK_PROFILE_ROOT',None)
    binaries = {x:digest(x) for x in command if x.lower().endswith('.exe') and Path(x).is_file()}
    binaries.update({tool['path']:tool['sha256'] for tool in tools.values()})
    for value in command:
        if value.startswith('-D') and '=' in value and value.lower().endswith('.exe'):
            path = value.split('=',1)[1]
            binaries[path] = digest(path)
    policy_hash, spec_hash, started = digest(policy_path), digest(spec_path), stamp()
    result = run(command, Path.cwd(), env, out/'output.log', rule['timeout'])
    result.update(schema='zeroslack.managed-test-result/v2', name=rule['name'], sourceTree=spec['source'], buildDirectory=spec['build'],
                  invocationId=out.name, policySha256=policy_hash, specSha256=spec_hash,
                  binaries=binaries, runtimeTools=tools, artifactDirectory=str(artifacts),
                  qtGuiDirectory=str(out/'QtGui'), startedEvidenceUtc=started, completedUtc=stamp(),
                  profileDirectory=str(profile), settingsDirectory=str(settings),
                  recoveryStorageOwner=rule.get('recoveryStorage',{}).get('owner','managed'))
    if not result['processCompleted']:
        result['classification']='infrastructure_error'
    elif result['timeout']:
        result['classification']='timeout'
        result['timeoutCause']='undetermined; allowed prerequisites passed; requires diagnosis, not an ordinary assertion failure'
    else:
        result['classification']='passed' if result['exitCode']==0 else 'failed'
    if any(Path(arg).as_posix() == (Path(spec['source'])/'cmake/run_qt_gui_test.cmake').as_posix() for arg in command):
        observe_qt_gui_result(result, out/'QtGui')
    try:
        result['logSha256'] = digest(out/'output.log')
        result['settingsInitializations'] = [{'record':str(path),'sha256':digest(path),**read_json(path)}
                                            for path in sorted(settings.glob('bootstrap-*.json'))]
        require(all(digest(path)==value for path,value in binaries.items()), f"{rule['name']}: binary changed during execution")
        require(digest(policy_path)==policy_hash and digest(spec_path)==spec_hash, f"{rule['name']}: policy/spec changed during execution")
    except Exception as error:
        infrastructure_error(result, 'evidence_metadata', error)
    return publish_result(out, result)


def render(policy):
    lines = ['# 受管测试执行策略', '', '本清单排除的是默认运行，不排除编译；硬性访问禁令独立生效。',
             '', '由 scripts/test_policy.py 从 test-execution-policy.json 生成，不手工维护规则。',
             '裸 ctest 与手动直接运行程序不等于受管默认集合；注册包装层仅能约束通过该注册启动的命令。',
             '受管默认入口：python scripts/release_gate.py test --config <构建目录>/managed-test-config.json --output <新证据目录>。',
             '在当前任务另有授权时，--names <准确名称数组 JSON> 可显式选择默认排除项；硬性限制仍拒绝。发布准备不接受此选择参数。',
             f"当前授权范围：{policy['scope']['id']}。准确受限变体仅在该任务及复核期限内有效，不能作为后续任务的永久授权。", '',
             '| CTest 注册 | 执行 | 责任 | 原因 | 复核期限 |', '|---|---|---|---|---|']
    for x in policy['tests']:
        lines.append(f"| {x['name']} | {'required' if x.get('required') else x['execution']} | {x.get('target',x.get('script'))} | {x['reason']['detail'].replace('|','/')} | {x['reviewBy']} |")
    lines += ['', '## 资源约束', '', '| 路径 | 操作约束 | 复核期限 |', '|---|---|---|']
    for x in policy['resources']:
        lines.append(f"| {x['path']} | {x['operation']} | {x['reviewBy']} |")
    return '\n'.join(lines)+'\n'


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    sub = parser.add_subparsers(dest='action', required=True)
    p = sub.add_parser('guard'); p.add_argument('--config', required=True); p.add_argument('--output'); p.add_argument('--contracts', action='store_true')
    p = sub.add_parser('invoke'); p.add_argument('--policy', required=True); p.add_argument('--spec', required=True)
    p = sub.add_parser('render'); p.add_argument('--policy', required=True); p.add_argument('--output', required=True)
    args = parser.parse_args()
    try:
        if args.action=='guard':
            result = guard(args.config, args.output)
            if args.contracts:
                from test_policy_contracts import verify_contracts
                result['contracts'] = verify_contracts(args.config)
                if args.output:
                    dump(args.output, result)
            write_utf8(sys.stdout, json.dumps({k:v for k,v in result.items() if k not in {'inventory','mappings'}})+'\n')
        elif args.action=='invoke':
            return invoke(args.policy, args.spec)
        elif args.action=='render':
            Path(args.output).write_text(render(load_policy(args.policy)), encoding='utf8')
        return 0
    except Exception as error:
        record = {'status':'blocked' if isinstance(error, PolicyError) else 'infrastructure_error',
                  'error':str(error), 'timeUtc':stamp(), 'infrastructureErrors':[]}
        if isinstance(error, PolicyError):
            try:
                write_utf8(sys.stderr, f'TEST POLICY BLOCKED: {error}\n')
            except Exception as echo_error:
                infrastructure_error(record, 'policy_echo', echo_error)
                silence_broken_stream(sys.stderr)
        else:
            infrastructure_error(record, 'entry', error)
        if args.action=='guard' and args.output:
            try:
                dump(args.output, record)
            except Exception as evidence_error:
                infrastructure_error(record, 'guard_evidence', evidence_error)
        return 2


if __name__=='__main__':
    sys.exit(main())
