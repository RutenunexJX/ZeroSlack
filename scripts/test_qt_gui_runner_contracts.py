"""Synthetic Qt logging protocol inputs; real Qt integration uses registered tests."""
import json
import os
from pathlib import Path
import sys
from test_policy import observe_qt_gui_result
from validation_process import run

CHILD = r"""
from pathlib import Path
import sys,time
mode,marker,*args=sys.argv[1:]
Path(marker).write_text('synthetic child started',encoding='utf8')
outputs={}
for i,arg in enumerate(args):
    if arg=='-o':
        path,kind=args[i+1].rsplit(',',1);outputs[kind]=Path(path)
if mode=='timeout':
    time.sleep(30)
if mode=='silent':sys.exit(17)
if mode=='missing':sys.exit(0)
failed=mode in ('failure','finalize')
text='SYNTHETIC QT PROTOCOL INPUT; not a real Qt test result\n'
if failed:
    text+='QWARN  : synthetic long prefix\n'*8192
    text+='FAIL!  : QtProtocolInput::case(data) assertion 中文 ; λ\n'
    text+='   Actual (value): 1\n   Expected (value): 2\n'
    text+='synthetic/fixture.cpp(73) : failure location\n'
else:text+='PASS   : QtProtocolInput::case(data)\n'
text+='Totals: '+('0 passed, 1 failed' if failed else '1 passed, 0 failed')+'\n'
outputs['txt'].write_text(text,encoding='utf8')
xml='<testsuite name="SyntheticProtocol"><testcase name="case(data)">'
if failed:xml+='<failure message="synthetic assertion"/>'
xml+='</testcase></testsuite>'
outputs['junitxml'].write_text(xml,encoding='utf8')
if mode=='finalize':(outputs['txt'].parent/'exit-code.txt').mkdir()
sys.exit(7 if failed else 0)
"""


def verify_qt_gui_runner(config, root, base_env, check):
    host = json.loads((Path(config['build'])/'Testing/ManagedCommands/simdock_workbench_host.json').read_text(encoding='utf8'))['command']
    filters = ['hostIsolationAndState','waveformSettingsAreTransactional','hostThemeAuthority','responsiveGeometry','resizePreservesState',
               'narrowWorkspaceActions','rapidContextsAndOwnedWorker','existingTestbenchWorkflow','destroyWithOpenDialog']
    encoded = [arg for arg in host if arg.startswith('-DQT_TEST_ARGUMENTS=')]
    check(len(encoded)==1 and encoded[0].split('=',1)[1].split(';')==filters
          and not any(arg in host for arg in filters),
          'Qt protocol: generated SimDock host command retains all eight original methods plus waveform settings in one argument')
    work=root/'qt-protocol';work.mkdir()
    child=work/'child.py';child.write_text(CHILD,encoding='utf8')
    wrapper=Path(config['source'])/'cmake/run_qt_gui_test.cmake'
    cmake=Path(config['ctest']).with_name('cmake.exe' if os.name=='nt' else 'cmake')
    env=base_env.copy();env.pop('ZEROSLACK_QT_TEST_LOG_ROOT',None)

    def invoke(label,mode,*,executable=None,report_root=None,observe=True):
        output=work/label;output.mkdir()
        reports=report_root if report_root is not None else output/'qt'
        marker=output/'child-started.txt'
        arguments=';'.join(['-X','utf8','-B',str(child),mode,str(marker)])
        command=[str(cmake),'-DQT_TEST_NAME=synthetic_protocol',
                 '-DQT_TEST_EXECUTABLE='+str(executable or sys.executable),
                 '-DQT_TEST_ARGUMENTS='+arguments,'-DQT_TEST_LOG_ROOT='+str(reports),
                 '-P',str(wrapper)]
        result=run(command,work,env,output/'output.log',1 if mode=='timeout' else 15)
        text=(output/'output.log').read_text(encoding='utf8',errors='replace')
        contexts=list(reports.glob('*/*/invocation.txt')) if reports.is_dir() else []
        if observe:observe_qt_gui_result(result,reports)
        return result,text,contexts,marker,reports

    ok,text,contexts,marker,reports=invoke('success','success')
    check(ok['exitCode']==0 and not ok['infrastructureErrors'], 'Qt protocol: success retains complete evidence')
    first=contexts[0].parent
    check(ok['qtInvocation']['phase']=='complete' and ok['qtInvocation']['rawExitCode']=='0', 'Qt protocol: completed phase and exact raw code')
    check('PASS   : QtProtocolInput::case(data)' in text and 'Totals:' in text, 'Qt protocol: successful text summary reaches parent log')
    before=(first/'test.log').read_bytes()
    second,_,next_contexts,_,_=invoke('separate-invocation','success',report_root=reports,observe=False)
    check(second['exitCode']==0 and len(next_contexts)==2 and (first/'test.log').read_bytes()==before,
          'Qt protocol: repeated root uses distinct evidence without overwriting earlier output')

    fail,text,contexts,_,_=invoke('assertion','failure')
    check(fail['exitCode']!=0 and fail['qtInvocation']['rawExitCode']=='7' and not fail['infrastructureErrors'],
          'Qt protocol: real nonzero child result is not changed by successful logging')
    report=contexts[0].parent
    check((report/'test.log').stat().st_size>65536 and 'synthetic/fixture.cpp(73)' in text[:65536],
          'Qt protocol: failure location after a long warning prefix reaches the leading parent summary')
    check('assertion 中文 ; λ' in text and 'Actual (value): 1' in text and 'Expected (value): 2' in text,
          'Qt protocol: assertion data and Unicode survive summary extraction')
    check((report/'result.xml').is_file() and (report/'test.log').read_text(encoding='utf8').endswith('Totals: 0 passed, 1 failed\n'),
          'Qt protocol: complete original text and XML are retained')

    silent,text,_,marker,_=invoke('silent','silent')
    check(silent['exitCode']!=0 and silent['qtInvocation']['rawExitCode']=='17' and marker.is_file(),
          'Qt protocol: silent nonzero process remains a failure with its raw code')
    check('phase=preflight' in text and str(sys.executable) in text and 'no Qt text output' in text and not silent['infrastructureErrors'],
          'Qt protocol: absent Qt output has invocation context without a fabricated assertion or cause')

    missing,text,_,_,_=invoke('missing-logs','missing')
    check(missing['exitCode']!=0 and missing['qtInvocation']['rawExitCode']=='0' and missing['infrastructureErrors'],
          'Qt protocol: missing logs after child success are infrastructure failure')
    missing_exe,text,_,marker,_=invoke('missing-executable','success',executable=work/'missing.exe')
    check(missing_exe['exitCode']!=0 and missing_exe['infrastructureErrors'] and not marker.exists() and 'executable_preflight' in text,
          'Qt protocol: missing executable is diagnosed before child launch')

    unwritable=work/'not-a-directory';unwritable.write_text('preserve',encoding='utf8')
    denied,text,_,marker,_=invoke('unwritable','success',report_root=unwritable)
    check(denied['exitCode']!=0 and denied['infrastructureErrors'] and not marker.exists()
          and 'qt_gui/directory' in text and unwritable.read_text(encoding='utf8')=='preserve',
          'Qt protocol: unwritable evidence root fails before launch and reaches parent infrastructure record')

    final,text,_,_,_=invoke('finalization','finalize')
    check(final['exitCode']!=0 and final['infrastructureErrors'] and final['qtInvocation']['phase']=='finalizing'
          and 'Qt child result: synthetic_protocol: 7' in text,
          'Qt protocol: finalization failure cannot hide the already emitted raw child failure')
    timed,text,contexts,marker,_=invoke('timeout','timeout')
    check(timed['timeout'] and timed['exitCode']==124 and marker.is_file() and len(contexts)==1,
          'Qt protocol: bounded outer timeout keeps launch context')
    check(timed['qtInvocation']['phase']=='running' and timed['qtInvocation']['rawExitCode'] is None
          and not timed['infrastructureErrors'],
          'Qt protocol: timeout phase is retained without inventing a completed child exit')
