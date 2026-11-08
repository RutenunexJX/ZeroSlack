"""Small release contracts; no C++ build, real installation, or fixture scan."""
import json
from pathlib import Path
from unittest.mock import patch

import release_gate as gate
from test_policy import PolicyError, digest, dump, require


def verify_release_reuse(root,check):
    root=Path(root)/'release-reuse';root.mkdir()
    policy={'resources':[]}
    def rejected(action,name):
        try:action()
        except (PolicyError,OSError,ValueError,KeyError):check(True,name)
        else:check(False,name)

    check(gate.absolute('scripts/release_gate.py')==gate.absolute(str(Path(gate.__file__).resolve())),
          'release paths have one repository-relative interpretation')
    gate.accept_source_delta({'README.md':{'kind':'external_metadata','before':'old','after':'new'}})
    check(True,'external release text does not demand compilation')
    rejected(lambda:gate.accept_source_delta({'VERSION':{'kind':'compiled_version','after':'new'}}),'compiled version cannot reuse old program identity')
    rejected(lambda:gate.accept_source_delta({'src/a.cpp':{'kind':'build_or_function_input','after':'new'}}),'functional change cannot reuse old test conclusions')
    rejected(lambda:gate.accept_source_delta({'README.md':{'kind':'external_metadata','after':None}}),'removed external package input needs explicit contract update')
    package=root/'package';package.mkdir();(package/'payload.txt').write_text('real bytes',encoding='utf8')
    gate.write_checksums(package,policy);gate.verify_checksums(package,policy)
    check(True,'actual package checksum manifest accepted')
    (package/'payload.txt').write_text('changed bytes',encoding='utf8')
    rejected(lambda:gate.verify_checksums(package,policy),'changed payload rejected')
    (package/'payload.txt').write_text('real bytes',encoding='utf8')
    (package/'extra.txt').write_text('user data',encoding='utf8')
    rejected(lambda:gate.verify_checksums(package,policy),'unexpected staging file rejected')
    gate.verify_checksums(package,policy,allow_extras=True)
    check(True,'installed unknown files are retained without weakening owned-file hashes')
    (package/'payload.txt').unlink()
    rejected(lambda:gate.verify_checksums(package,policy,allow_extras=True),'missing owned payload rejected even with user extras')
    (package/'SHA256SUMS.txt').write_text('0'*64+'  ../outside\n',encoding='utf8')
    rejected(lambda:gate.verify_checksums(package,policy),'checksum traversal rejected before access')
    dep=root/'dep';dep.mkdir();(dep/'runtime.dll').write_bytes(b'actual dependency')
    dependencies={'runtime':{'root':str(dep),'files':{'runtime.dll':digest(dep/'runtime.dll')}}}
    gate.verify_dependencies(dependencies,policy)
    (dep/'runtime.dll').write_bytes(b'changed dependency')
    rejected(lambda:gate.verify_dependencies(dependencies,policy),'dependency content mutation rejected')
    source=root/'source';build=root/'build';source.mkdir();(build/'generated').mkdir(parents=True)
    (source/'VERSION').write_text('1.2.3',encoding='utf8');(build/'generated/version.h').write_text('#define APP_VERSION "1.2.4"',encoding='utf8')
    rejected(lambda:gate.prepared_version({'sourceRoot':str(source),'buildDirectory':str(build)}),'generated and source versions must agree')
    # These are deliberately invalid inputs, never fabricated passing evidence.
    invalid=root/'invalid.json'
    for status in ['failed','timeout','restricted','running']:
        dump(invalid,{'schema':'zeroslack.release-preparation/v1','status':status})
        rejected(lambda:gate.verify_receipt(invalid),'old '+status+' result cannot become reused success')
    rejected(lambda:gate.require_original_build_identity({'compile':{'logSha256':'fixture'}}),
             'legacy generated-input evidence cannot be sampled retroactively')
    rejected(lambda:gate.require_original_build_identity({'compile':{'logSha256':'fixture'},'buildIdentity':{'files':{'generated/x.h':'now'}}}),
             'a current fingerprint without build-time origin is not original evidence')
    fixture={'compile':{'logSha256':'fixture'},'buildIdentity':{'scope':gate.BUILD_IDENTITY_SCOPE,'files':{'generated/x.h':'original'}},
             'buildIdentityOrigin':{'phase':'after_compile_before_required_tests','compileLogSha256':'wrong','capturedUtc':'unit fixture'}}
    rejected(lambda:gate.require_original_build_identity(fixture),'original fingerprint must bind the successful compile log')
    fixture['buildIdentityOrigin']['compileLogSha256']='fixture'
    with patch.object(gate,'build_identity',return_value={'files':{'generated/x.h':'changed'}}):
        rejected(lambda:gate.verify_build_identity(fixture,policy),'generated content drift is rejected even with identical commands')
    inputs=build/'inputs';inputs.mkdir()
    for name in ['export.def','link.rsp','generator.data','no_extension']:
        (inputs/name).write_bytes(b'original build input')
    commands=build/'Testing/ManagedCommands';commands.mkdir(parents=True)
    (commands/'test.json').write_text('{}',encoding='utf8')
    artifacts=build/'Testing/Temporary';artifacts.mkdir()
    (artifacts/'LastTest.log').write_text('test output',encoding='utf8')
    identity=gate.build_file_identity(build,policy)
    check(all('inputs/'+name in identity for name in ['export.def','link.rsp','generator.data','no_extension'])
          and 'Testing/ManagedCommands/test.json' in identity and 'Testing/Temporary/LastTest.log' not in identity,
          'complete build identity includes linker inputs and test definitions without a suffix allowlist')
    (inputs/'link.rsp').write_bytes(b'changed response file')
    check(gate.build_file_identity(build,policy)!=identity,'response-file byte drift changes original input identity')
    fixture['buildIdentity'].pop('scope')
    rejected(lambda:gate.require_original_build_identity(fixture),'older partial collectors cannot claim complete original proof')
    sources=root/'sources.json';dump(sources,{'schema':'zeroslack.package-sources/v1','sources':{}})
    with patch.object(gate,'load_policy',return_value=policy):
        rejected(lambda:gate.verify_package_sources(sources,{'sourceRoot':str(source)}),
                 'first assembly cannot omit component/resource/license provenance')
    with (patch.object(gate,'prepare',side_effect=AssertionError('unexpected build')),
          patch.object(gate,'test_set',side_effect=AssertionError('unexpected tests'))):
        # A missing imported record fails before either execution path is entered.
        rejected(lambda:gate.reuse(root/'missing.json',gate.REPOSITORY,root/'unused.json'),'missing evidence cannot trigger silent fallback execution')
    return {'status':'passed','root':str(root)}


if __name__=='__main__':
    import argparse
    parser=argparse.ArgumentParser();parser.add_argument('--output',required=True);args=parser.parse_args()
    output=gate.absolute(args.output);require(not output.exists(),'Contract output must be new');output.mkdir(parents=True)
    rows=[]
    def check(value,name):
        rows.append({'name':name,'passed':bool(value)});require(value,name)
    status='failed'
    try:
        verify_release_reuse(output,check);status='passed'
    finally:dump(output/'result.json',{'status':status,'checks':rows})
    print(json.dumps({'status':status,'checks':len(rows)}))
