"""Small release contracts; no C++ build, real installation, or fixture scan."""
import json
import importlib.util
from pathlib import Path
from unittest.mock import patch

import release_gate as gate
from test_policy import PolicyError, digest, dump, read_json, require


def verify_wave_component_identity(root,check):
    fixture=Path(root)/'wave-identity-fixture';component=fixture/'components/tickx';component.mkdir(parents=True)
    metadata={'version':'fixture','baseCommit':'frozen-base','sourceDirty':True,
              'sourcePatchSha256':'a'*64,'capabilities':['wave-style/v1','wave-workbench/v1']}
    names=['wavewidgets.dll','WaveWorkbenchEla.dll','LICENSE','NOTICE','THIRD-PARTY-NOTICES.md','build-info.json']
    for name in names:
        if name=='build-info.json': dump(component/name,metadata)
        else: (component/name).write_bytes(('non-executable contract fixture: '+name).encode())
    info={**metadata,'revision':metadata['baseCommit']}
    dump(fixture/'build-info.json',{'nativeComponents':{'tickx':info}})
    receipt={'configuration':{'ZEROSLACK_TICKX_WIDGETS_ROOT':'frozen-fixture'},
             'packageInputs':{'components/tickx/'+name:digest(component/name) for name in names}}
    gate.verify_tickx_payload(fixture,receipt);check(True,'Wave component accepts complete frozen source identity')
    def rejected(action,name):
        try: action()
        except PolicyError: check(True,name)
        else: check(False,name)
    for name in names:
        path=component/name;original=path.read_bytes();path.write_bytes(original+b'changed')
        rejected(lambda:gate.verify_tickx_payload(fixture,receipt),'Wave component rejects changed '+name)
        path.write_bytes(original)
    for field,value in [('revision','other-base'),('sourceDirty',False),('sourcePatchSha256','b'*64),('capabilities',[])]:
        dump(fixture/'build-info.json',{'nativeComponents':{'tickx':{**info,field:value}}})
        rejected(lambda:gate.verify_tickx_payload(fixture,receipt),'Wave component rejects mismatched '+field)
    dump(fixture/'build-info.json',{'nativeComponents':{'tickx':info}})
    gate.verify_tickx_payload(fixture,receipt);check(True,'Wave component restored fixture remains valid')


def verify_first_package_sources(root,check):
    fixture=Path(root)/'first-package-fixture';fixture.mkdir()
    native={'version':'2.16.2','channel':'formal','dirty':False,'sourceDirty':False,
            'suiteSdk':'1.0.2','nativeSurfaceAbi':1,'qt':'6.10.2','fixtureOnly':True}
    adapter={'coreVersion':'2.14.2','protocolVersion':1,'qt':'6.10.2','buildType':'Release','fixtureOnly':True}
    payloads={'native':{'build-info.json':json.dumps(native).encode(),'component.dll':b'unit fixture'},
              'nativeLicenses':{'LICENSE.txt':b'unit fixture license'},
              'adapter':{'build-info.json':json.dumps(adapter).encode(),'adapter.exe':b'unit fixture'},
              'documents':{'index.html':b'<p>unit fixture</p>','bridge.js':b'// unit fixture'}}
    sources={}
    for role,files in payloads.items():
        directory=fixture/role;directory.mkdir()
        for name,content in files.items():(directory/name).write_bytes(content)
        hashes={name:digest(directory/name) for name in files}
        proof=fixture/(role+'-provenance.json')
        dump(proof,{'fixtureOnly':True,'files':hashes})
        sources[role]={'root':str(directory),'files':hashes,
                       'provenance':{'file':str(proof),'sha256':digest(proof),'keys':['files']}}
    baseline=fixture/'approved-baseline-fixture.json'
    dump(baseline,{'fixtureOnly':True,'native':native,'adapter':adapter})
    dependencies=fixture/'dependencies.json'
    dump(dependencies,{'ZEROSLACK_DOCUMENT_EDITORS_DIR':{'files':sources['documents']['files']}})
    build=fixture/'build';(build/'generated').mkdir(parents=True)
    suite=build/'generated/suiteapp-build.json';dump(suite,{'sdkVersion':'1.0.2'})
    receipt={'sourceRoot':str(gate.REPOSITORY),'buildDirectory':str(build),'dependencyManifest':str(dependencies)}
    plan={'schema':'zeroslack.package-sources/v1','sources':sources,'distribution':'standalone',
          'componentBaseline':{'file':str(baseline),'sha256':digest(baseline)}}
    plan_file=fixture/'sources.json'
    def verify(value=None):
        dump(plan_file,plan if value is None else value)
        return gate.verify_package_sources(plan_file,receipt)
    def rejected(message,name,value=None):
        try:verify(value)
        except (PolicyError,OSError) as error:check(message in str(error),name)
        else:check(False,name)
    check(verify()==plan and not (fixture/'welcome').exists(),
          'first package accepts four actual source inventories without welcome')
    for role,entry in sources.items():
        changed=json.loads(json.dumps(plan));del changed['sources'][role]
        rejected('First packaging needs explicit',role+' source is mandatory',changed)
        proof=Path(entry['provenance']['file']);original_proof=proof.read_bytes()
        proof.write_bytes(original_proof+b'\n')
        rejected('Package source provenance changed',role+' changed provenance rejected')
        proof.unlink()
        rejected(proof.name,role+' missing provenance rejected')
        proof.write_bytes(original_proof)
        name=next(iter(entry['files']))
        changed=json.loads(json.dumps(plan));changed['sources'][role]['files'][name]='0'*64
        rejected('not fully covered by original provenance',role+' uncovered source hash rejected',changed)
        payload=Path(entry['root'])/name;original_payload=payload.read_bytes()
        payload.write_bytes(original_payload+b'changed')
        rejected('Package source inventory/content changed',role+' content mutation rejected')
        payload.unlink()
        rejected('Package source inventory/content changed',role+' missing payload rejected')
        payload.write_bytes(original_payload)
    changed=json.loads(json.dumps(plan));changed['sources']['welcome']=sources['documents']
    rejected('First packaging needs explicit','retired welcome source is not an accepted role',changed)
    changed=json.loads(json.dumps(plan));changed['componentBaseline']['sha256']='0'*64
    rejected('Approved component baseline changed','component approval fingerprint remains mandatory',changed)
    dump(suite,{'sdkVersion':'0.9.0'})
    rejected('Suite SDK downgrade refused','generated SDK compatibility remains mandatory')
    dump(suite,{'sdkVersion':'1.0.2'})
    dump(dependencies,{'ZEROSLACK_DOCUMENT_EDITORS_DIR':{'files':{}}})
    rejected('Document resources differ','accepted document dependency identity remains mandatory')
    dump(dependencies,{'ZEROSLACK_DOCUMENT_EDITORS_DIR':{'files':sources['documents']['files']}})
    check(verify()==plan,'restored real fixture sources pass without relaxing evidence checks')


def verify_install_manifest(root,check):
    specification=importlib.util.spec_from_file_location('release_installer_contract',Path(gate.__file__).with_name('install-release.py'))
    installer=importlib.util.module_from_spec(specification);specification.loader.exec_module(installer)
    fixture=Path(root)/'install-manifest-fixture';fixture.mkdir()
    old={'generatedUtc':'old','unknownSuite':{'keep':['value']},'components':[
        {'id':'xips','version':'2.16.2','unknown':{'keep':True},'welcomeResources':{'unrelated':True}},
        {'id':'zeroslack','version':'1.2.3','welcomeResources':{'retired':True},
         'documentEditors':{'version':'old'},'unknownApp':{'keep':['value']},'enabled':False},
        {'id':'csrio','version':'3.4.5','unknown':[1,2]}]}
    info={'version':'1.2.4','documentEditors':{'version':'new'},'preparationEvidenceSha256':'fixture-only',
          'suiteGuiProvider':{'fixture':True},'suiteCliClient':{'fixture':True},'unownedPackageField':'not an index field'}
    manifest=fixture/'suite-manifest.json';build_info=fixture/'build-info.json'
    dump(manifest,old);dump(build_info,info)
    original=manifest.read_bytes();loaded=read_json(manifest)
    updated=installer.updated_suite_manifest(loaded,read_json(build_info),'1.2.3')
    transformed=fixture/'suite-manifest.next.json';dump(transformed,updated)
    actual=read_json(transformed);item=actual['components'][1]
    check('welcomeResources' not in item,'installed ZeroSlack index removes absent retired welcome metadata')
    check(item['version']=='1.2.4' and item['documentEditors']==info['documentEditors']
          and all(item[key]==info[key] for key in ['preparationEvidenceSha256','suiteGuiProvider','suiteCliClient']),
          'current package metadata and existing allowed evidence fields still update')
    check(actual['components'][0]==old['components'][0] and actual['components'][2]==old['components'][2],
          'all other applications and their fields remain unchanged')
    check(actual['unknownSuite']==old['unknownSuite'] and item['unknownApp']==old['components'][1]['unknownApp']
          and item['enabled'] is False and 'unownedPackageField' not in item,
          'unknown suite/application fields and package import boundary are preserved')
    check(loaded==old and manifest.read_bytes()==original and actual['generatedUtc']!='old',
          'conversion leaves the original manifest available for backup and rollback')
    again=installer.updated_suite_manifest(actual,info,'1.2.4')
    check(again['components']==actual['components'],'welcome cleanup is idempotent')
    legacy=dict(info,welcomeResources={'stillOwnedByLegacyPackage':True})
    check(installer.updated_suite_manifest(old,legacy,'1.2.3')['components'][1]['welcomeResources']==legacy['welcomeResources'],
          'legacy package metadata still follows its explicit source')
    for label,value,version,message in [
        ('missing owner',dict(old,components=[old['components'][0]]),'1.2.3','Ambiguous suite ownership'),
        ('duplicated owner',dict(old,components=old['components']+[old['components'][1]]),'1.2.3','Ambiguous suite ownership'),
        ('old version mismatch',old,'0.0.0','Suite index does not match old package')]:
        try:installer.updated_suite_manifest(value,info,version)
        except PolicyError as error:check(message in str(error),label+' is still rejected')
        else:check(False,label+' is still rejected')


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
    verify_first_package_sources(root,check)
    verify_wave_component_identity(root,check)
    verify_install_manifest(root,check)
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
