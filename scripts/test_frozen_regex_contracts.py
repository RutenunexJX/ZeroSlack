"""Exercise reviewed regex inventories through the production CMake checker."""
from pathlib import Path
from hashlib import sha256
import os
import re

from validation_process import run


def verify_frozen_regex(config, root, env, check):
    source_root = Path(config['source'])
    guard = (source_root/'cmake/legacy_field_policy_guard.ctest').read_text(encoding='utf8')
    revision = re.search(r'set\(SIMDOCK_FROZEN_REGEX_SOURCE_REVISION "([^"]+)"\)', guard).group(1)
    extractor = re.search(r'set\(SIMDOCK_FROZEN_REGEX_EXTRACTION_VERSION "([^"]+)"\)', guard).group(1)
    inventory = re.search(r'set\(SIMDOCK_FROZEN_REGEX_USAGE\s+(.*?)\n\)', guard, re.S).group(1)
    entries = re.findall(r'"(src/simulation/simdock/core/[^"]+)"', inventory)
    expected_paths = {
        'src/simulation/simdock/core/analyzer.cpp',
        'src/simulation/simdock/core/questasession.cpp',
        'src/simulation/simdock/core/workspace.cpp',
    }
    check(len(entries) == 3 and {entry.split('|')[0] for entry in entries} == expected_paths,
          'frozen regex inventory retains the three shared core files after TB generator retirement')
    contents = {entry.split('|')[0]: (source_root/entry.split('|')[0]).read_bytes() for entry in entries}
    cmake = Path(config['ctest']).with_name('cmake.exe' if os.name == 'nt' else 'cmake')
    controls = root/'frozen-regex'
    controls.mkdir()

    def quote(value):
        return '[==['+str(value).replace('\\', '/')+']==]'

    def probe(name, expected_message=None, changed=None, source_revision=revision,
              extraction_version=extractor, declared=None, fixture_contents=contents,
              contract_entries=entries, contract='zeroslack_verify_simdock_regex_contract',
              extra_args=(), changed_source=None, line_ending=None):
        case = controls/name
        case.mkdir()
        for path, data in fixture_contents.items():
            target = case/'source'/path
            target.parent.mkdir(parents=True, exist_ok=True)
            data = (changed_source or {}).get(path, data)
            if line_ending:
                data = data.replace(b'\r\n', b'\n')
                if line_ending == 'crlf':
                    data = data.replace(b'\n', b'\r\n')
            target.write_bytes(data + (b'\nQRegularExpression isolatedChangedUsage;\n' if path == changed else b''))
        values = contract_entries if declared is None else declared
        script = case/'check.cmake'
        script.write_text('include('+quote(source_root/'cmake/SimDockRegexContract.cmake')+')\n'
            +contract+'('+quote(case/'source')+' '
            +quote(source_revision)+' '+quote(extraction_version)+' '
            +' '.join(quote(value) for value in (*extra_args, *values))+')\n', encoding='utf8')
        result = run([str(cmake), '-P', str(script)], case, env, case/'process.log', 15)
        text = (case/'process.log').read_text(encoding='utf8', errors='replace')
        check(not result['timeout'] and
              (result['exitCode'] == 0 if expected_message is None
               else result['exitCode'] != 0 and expected_message in text),
              'frozen regex '+name+': '+('accepted reviewed input' if expected_message is None
                                       else 'rejected '+expected_message))

    probe('reviewed')
    for path in contents:
        probe('changed-'+Path(path).stem, 'Frozen SimDock regex contract changed', changed=path)
    probe('wrong-source', 'source revision mismatch', source_revision='0'*40)
    probe('wrong-extractor', 'extraction version mismatch', extraction_version='unreviewed-extractor')
    probe('malformed-hash', 'hash metadata is malformed', declared=[entries[0].split('|')[0]+'|invalid', *entries[1:]])
    probe('missing-file', 'source inventory mismatch', declared=entries[:-1])
    probe('duplicated-file', 'source inventory mismatch', declared=[entries[0], *entries[:-1]])
    probe('wrong-expectation', 'Frozen SimDock regex contract changed',
          declared=[entries[0].split('|')[0]+'|'+'0'*64, *entries[1:]])
    probe('unknown-file', 'source inventory mismatch',
          declared=['src/unreviewed.cpp|'+entries[0].split('|')[1], *entries[1:]])

    phase_revision = re.search(r'set\(PHASE_L_REVIEWED_REGEX_SOURCE_REVISION "([^"]+)"\)', guard).group(1)
    phase_extractor = re.search(r'set\(PHASE_L_REVIEWED_REGEX_EXTRACTION_VERSION "([^"]+)"\)', guard).group(1)
    normalizer = re.search(r'set\(PHASE_L_REVIEWED_SOURCE_NORMALIZATION "([^"]+)"\)', guard).group(1)
    phase_inventory = re.search(r'set\(PHASE_L_REVIEWED_REGEX_USAGE\s+(.*?)\n\)', guard, re.S).group(1)
    phase_entries = re.findall(r'"(src/[^"]+)"', phase_inventory)
    check(len(phase_entries) == 10 and
          all(entry.split('|')[0] != 'src/ui/welcomeanimation.cpp' for entry in phase_entries),
          'reviewed Phase L inventory retains ten source files after welcome animation retirement')
    phase_contents = {entry.split('|')[0]: (source_root/entry.split('|')[0]).read_bytes() for entry in phase_entries}

    def reviewed_probe(name, expected_message=None, **options):
        arguments = dict(source_revision=phase_revision, extraction_version=phase_extractor,
                         fixture_contents=phase_contents, contract_entries=phase_entries,
                         contract='zeroslack_verify_reviewed_regex_contract', extra_args=(normalizer,))
        arguments.update(options)
        probe('phase-l-'+name, expected_message, **arguments)

    reviewed_probe('reviewed')
    reviewed_probe('lf-source', line_ending='lf')
    reviewed_probe('crlf-source', line_ending='crlf')
    for path, data in phase_contents.items():
        reviewed_probe('changed-api-'+Path(path).stem, 'Reviewed Phase L regex contract changed', changed=path)
        reviewed_probe('changed-body-'+Path(path).stem, 'complete source changed',
                       changed_source={path: data+b'\n// isolated non-API source mutation\n'})
    multiline = 'src/completion/simulationcommandprefix.cpp'
    # The production @ capture was retired. Keep its multiline blind-spot
    # regression as an isolated fixture, using the unchanged production checker.
    data = (b'static const QRegularExpression isolatedPattern(\n'
            b'    QStringLiteral("^[A-Za-z_][A-Za-z0-9_]*$"));\n')
    original = data.splitlines()[1]
    mutated = original.replace(b'A-Za-z0-9_', b'A-Za-z0-9_$')
    fixture_entry = '|'.join([multiline, sha256(data.splitlines()[0]+b'\n').hexdigest(),
                             sha256(data).hexdigest()])
    fixture_entries = [fixture_entry if entry.split('|')[0] == multiline else entry
                       for entry in phase_entries]
    reviewed_probe('multiline-fixture', declared=fixture_entries, changed_source={multiline: data})
    check(mutated != original and b'QRegularExpression' not in original,
          'multi-line pattern control changes an actual literal outside API matching lines')
    reviewed_probe('changed-multiline-pattern', 'complete source changed',
                   declared=fixture_entries, changed_source={multiline: data.replace(original, mutated)})
    reviewed_probe('wrong-source', 'source revision mismatch', source_revision='0'*40)
    reviewed_probe('wrong-extractor', 'extraction version mismatch', extraction_version='unreviewed-extractor')
    reviewed_probe('wrong-normalizer', 'source normalization mismatch', extra_args=('unreviewed-normalizer',))
    first = phase_entries[0].split('|')
    reviewed_probe('malformed-api-hash', 'hash metadata is malformed',
                   declared=['|'.join([first[0], 'invalid', first[2]]), *phase_entries[1:]])
    reviewed_probe('malformed-source-hash', 'hash metadata is malformed',
                   declared=['|'.join([first[0], first[1], 'invalid']), *phase_entries[1:]])
    reviewed_probe('missing-source-hash', 'entry metadata is malformed',
                   declared=['|'.join(first[:2]), *phase_entries[1:]])
    reviewed_probe('missing-file', 'source inventory mismatch', declared=phase_entries[:-1])
    reviewed_probe('duplicated-file', 'source inventory mismatch', declared=[phase_entries[0], *phase_entries[:-1]])
    reviewed_probe('wrong-api-expectation', 'Reviewed Phase L regex contract changed',
                   declared=['|'.join([first[0], '0'*64, first[2]]), *phase_entries[1:]])
    reviewed_probe('wrong-source-expectation', 'complete source changed',
                   declared=['|'.join([first[0], first[1], '0'*64]), *phase_entries[1:]])
    reviewed_probe('unknown-file', 'source inventory mismatch',
                   declared=['|'.join(['src/unreviewed.cpp', *first[1:]]), *phase_entries[1:]])

    # Use the actual source inventory block with an excluded cyclic build tree.
    # The root CMakeLists is a literal entry; only src/include are recursive.
    inventory_root = controls/'inventory-source'
    expected = ['CMakeLists.txt', 'include/zeroslack/owned.h', 'src/owned.cpp', 'src/owned.h']
    for relative in expected + ['build/CMakeLists.txt', 'thirdparty/vendor/CMakeLists.txt']:
        path = inventory_root/relative
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text('// isolated source inventory probe\n', encoding='utf8')
    alias = inventory_root/'build/alias'
    if os.name == 'nt':
        import _winapi
        _winapi.CreateJunction(str(inventory_root/'build'), str(alias))
    else:
        alias.symlink_to(inventory_root/'build', target_is_directory=True)
    for variable, end_marker in (
            ('phase_j_repo_sources', 'list(APPEND phase_j_repo_sources'),
            ('phase_j_zero_root_sources', 'set(phase_j_zero_test_sources')):
        start = guard.index('file(GLOB_RECURSE '+variable)
        end = guard.index(end_marker, start)
        script = controls/(variable+'.cmake')
        output = controls/(variable+'.txt')
        script.write_text('set(ZEROSLACK_SOURCE_DIR '+quote(inventory_root)+')\n'+guard[start:end]
            +'list(SORT '+variable+')\n'
            +'file(WRITE '+quote(output)+' "${'+variable+'}")\n', encoding='utf8')
        result = run([str(cmake), '-P', str(script)], controls, env, controls/(variable+'.log'), 15)
        actual = output.read_text(encoding='utf8').split(';') if output.is_file() else []
        check(result['exitCode'] == 0 and not result['timeout'] and actual == expected,
              variable+' keeps root CMake and all owned source classes without traversing excluded cyclic build tree')
