"""Exercise the shared CMake runtime path and its fail-closed tool validation."""
from copy import deepcopy
import json
import os
from pathlib import Path
import subprocess
import sys

from test_policy import PolicyError, read_json, runtime_tools
from validation_process import run


def verify_runtime_path(config, root, base_env, check):
    source = root/'runtime-source'
    source.mkdir()
    (source/'test_sv').mkdir()
    policy = read_json(config['policy'])
    (source/'test_sv/test-execution-policy.json').write_text(json.dumps(policy), encoding='utf8')
    extra = root/'declared-extra'
    extra.mkdir()
    unrelated = root/'unrelated-path'
    if os.name == 'nt':
        import _winapi
        _winapi.CreateJunction(str(extra), str(unrelated))
    else:
        unrelated.symlink_to(extra, target_is_directory=True)
    check(unrelated.is_junction() if os.name == 'nt' else unrelated.is_symlink(),
          'runtime PATH negative input is an isolated redirection')

    def quote(value):
        return '[==['+str(value).replace('\\','/')+']==]'

    cmake = Path(config['ctest']).with_name('cmake.exe' if os.name == 'nt' else 'cmake')
    git = config['runtimeTools']['git']['path']
    compiler = str(Path(config['compilerBin'])/('g++.exe' if os.name == 'nt' else 'g++'))
    qt_core = str(Path(config['qtBin'])/('Qt6Core.dll' if os.name == 'nt' else 'libQt6Core.so'))
    lines = ['cmake_minimum_required(VERSION 3.30)', 'project(RuntimePathContract NONE)',
             'enable_testing()', 'set(Python_EXECUTABLE '+quote(sys.executable)+')',
             'set(GIT_EXECUTABLE '+quote(git)+')', 'set(CMAKE_CXX_COMPILER '+quote(compiler)+')',
             'set(ZEROSLACK_TEST_EXTRA_TOOL_DIRS '+quote(extra)+' CACHE STRING "")',
             'add_library(Qt6::Core SHARED IMPORTED)',
             'set_target_properties(Qt6::Core PROPERTIES IMPORTED_LOCATION '+quote(qt_core)+')',
             'include('+quote(Path(config['source'])/'cmake/TestExecution.cmake')+')',
             'zeroslack_test_runtime_path(runtime_path)',
             'file(WRITE "${CMAKE_BINARY_DIR}/runtime-path.txt" "${runtime_path}")',
             r'string(REPLACE ";" "\\;" runtime_path "${runtime_path}")',
             'add_test(NAME zeroslack_cli_test COMMAND "${CMAKE_COMMAND}" -E env "PATH=${runtime_path}" '+quote(git)+' --version)']
    (source/'CMakeLists.txt').write_text('\n'.join(lines)+'\n', encoding='utf8')
    snapshots = []
    for label, caller in [('clean', base_env['PATH']), ('unrelated', str(unrelated)+os.pathsep+base_env['PATH'])]:
        build = root/('runtime-'+label)
        env = dict(base_env, PATH=caller)
        result = run([str(cmake), '-S', str(source), '-B', str(build), '-G', 'Ninja',
                      '-DCMAKE_MAKE_PROGRAM='+config['ninja']], root, env, root/('runtime-'+label+'.log'), 30)
        check(result['exitCode'] == 0, label+': isolated runtime CMake configuration')
        path = (build/'runtime-path.txt').read_text(encoding='utf8')
        spec = read_json(build/'Testing/ManagedCommands/zeroslack_cli_test.json')
        inventory = subprocess.run([config['ctest'], '--test-dir', str(build), '--show-only=json-v1'],
                                   env=env, capture_output=True, timeout=10)
        check(inventory.returncode == 0, label+': CTest runtime properties available')
        test = json.loads(inventory.stdout)['tests'][0]
        modifications = next(p['value'] for p in test['properties'] if p['name']=='ENVIRONMENT_MODIFICATION')
        check(modifications[0].replace('\\;', ';') == 'PATH=set:'+path,
              label+': CTest resets caller PATH before preserving target additions')
        check(spec['command'][3].replace('\\;', ';') == 'PATH='+path,
              label+': inner command uses the same declared PATH')
        check(str(extra).replace('\\','/') in path and str(unrelated).replace('\\','/') not in path,
              label+': declared extra included and unrelated caller path excluded')
        snapshots.append((path, spec['command'], modifications))
    check(snapshots[0] == snapshots[1], 'caller PATH does not alter generated command or CTest environment')
    rule = next(r for r in policy['tests'] if r['name']=='zeroslack_cli_test')
    command = snapshots[0][1]
    check(bool(runtime_tools(rule, config, command, policy)), 'declared Git identity accepted with isolated PATH')
    poisoned = list(command)
    poisoned[3] = 'PATH='+str(unrelated)+os.pathsep+snapshots[0][0]
    wrong_tool = deepcopy(config)
    wrong_tool['runtimeTools']['git']['path'] = str(unrelated/'git.exe')
    wrong_hash = deepcopy(config)
    wrong_hash['runtimeTools']['git']['sha256'] = '0'*64
    for name, cfg, cmd, diagnostic in [
            ('declared PATH redirection', config, poisoned, 'Path redirection is unsupported'),
            ('declared tool redirection', wrong_tool, command, 'Path redirection is unsupported'),
            ('changed tool identity', wrong_hash, command, 'runtime tool changed')]:
        try:
            runtime_tools(rule, cfg, cmd, policy)
        except PolicyError as error:
            check(diagnostic in str(error), name+': rejection remains fail-closed')
        else:
            check(False, name+': must be rejected')
