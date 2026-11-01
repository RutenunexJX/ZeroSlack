"""Bounded validation processes; a Windows job owns only this invocation's children."""
from contextlib import contextmanager
import ctypes
import os
from pathlib import Path
import subprocess
import sys
import time


def write_utf8(stream, text):
    """Emit complete Unicode independently of the caller's console code page."""
    if hasattr(stream, 'buffer'):
        data = memoryview(text.encode('utf8'))
        while data:
            written = stream.buffer.write(data)
            if not written:
                raise OSError('Output stream made no progress')
            data = data[written:]
        stream.buffer.flush()
    else:
        stream.write(text)
        stream.flush()


def silence_broken_stream(stream):
    # Prevent Python's final flush from replacing an already determined exit
    # code with 120 after a broken pipe. The original log remains the evidence.
    try:
        with open(os.devnull, 'wb') as sink:
            os.dup2(sink.fileno(), stream.fileno())
    except Exception:
        for name in ('stdout', 'stderr', '__stdout__', '__stderr__'):
            if getattr(sys, name) is stream:
                setattr(sys, name, open(os.devnull, 'w', encoding='utf8'))


def infrastructure_error(result, stage, error):
    record = {'stage': stage, 'type': type(error).__name__, 'message': str(error)}
    result.setdefault('infrastructureErrors', []).append(record)
    try:
        write_utf8(sys.stderr, f"TEST INFRASTRUCTURE ERROR [{stage}]: {type(error).__name__}: {error}\n")
    except Exception:
        silence_broken_stream(sys.stderr)


@contextmanager
def process_job(child):
    handle = None
    if os.name == 'nt':
        from ctypes import wintypes as w
        class BASIC(ctypes.Structure):
            _fields_ = [('processTime', ctypes.c_int64), ('jobTime', ctypes.c_int64),
                        ('flags', w.DWORD), ('minWorkingSet', ctypes.c_size_t),
                        ('maxWorkingSet', ctypes.c_size_t), ('activeProcesses', w.DWORD),
                        ('affinity', ctypes.c_size_t), ('priority', w.DWORD), ('scheduling', w.DWORD)]
        class IO(ctypes.Structure):
            _fields_ = [(n, ctypes.c_uint64) for n in ['readOps','writeOps','otherOps','readBytes','writeBytes','otherBytes']]
        class EXTENDED(ctypes.Structure):
            _fields_ = [('basic', BASIC), ('io', IO), ('processMemory', ctypes.c_size_t),
                        ('jobMemory', ctypes.c_size_t), ('peakProcessMemory', ctypes.c_size_t),
                        ('peakJobMemory', ctypes.c_size_t)]
        api = ctypes.WinDLL('kernel32', use_last_error=True)
        api.CreateJobObjectW.argtypes = [ctypes.c_void_p, w.LPCWSTR]
        api.CreateJobObjectW.restype = w.HANDLE
        api.SetInformationJobObject.argtypes = [w.HANDLE, ctypes.c_int, ctypes.c_void_p, w.DWORD]
        api.AssignProcessToJobObject.argtypes = [w.HANDLE, w.HANDLE]
        api.CloseHandle.argtypes = [w.HANDLE]
        handle = api.CreateJobObjectW(None, None)
        limits = EXTENDED()
        limits.basic.flags = 0x2000  # JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE
        if (not handle or not api.SetInformationJobObject(handle, 9, ctypes.byref(limits), ctypes.sizeof(limits))
                or not api.AssignProcessToJobObject(handle, w.HANDLE(int(child._handle)))):
            error = ctypes.get_last_error()
            child.kill()
            child.wait()
            if handle:
                api.CloseHandle(handle)
            raise OSError(error, 'Cannot contain validation process')
    try:
        yield
    finally:
        if handle:
            api.CloseHandle(handle)
        elif child.poll() is None:
            child.kill()
        if child.poll() is None:
            child.wait()


def run(command, cwd, env, log, timeout, progress=False):
    log = Path(log)
    started = time.monotonic()
    result = {'command': list(map(str, command)), 'cwd': str(cwd), 'timeoutSeconds': timeout,
              'exitCode': None, 'timeout': False, 'log': str(log),
              'processStarted': False, 'processCompleted': False, 'infrastructureErrors': []}
    try:
        log.parent.mkdir(parents=True, exist_ok=True)
        with log.open('wb') as output:
            child = subprocess.Popen(command, cwd=cwd, env=env, stdout=output, stderr=subprocess.STDOUT)
            result['processStarted'] = True
            with process_job(child):
                while child.poll() is None:
                    remaining = timeout - (time.monotonic() - started)
                    if remaining <= 0:
                        result['timeout'] = True
                        result['exitCode'] = 124
                        break
                    try:
                        child.wait(timeout=min(30, remaining))
                    except subprocess.TimeoutExpired:
                        if progress:
                            try:
                                write_utf8(sys.stdout, f'{time.monotonic()-started:.0f}s: {log}\n')
                            except Exception as error:
                                infrastructure_error(result, 'progress', error)
                                silence_broken_stream(sys.stdout)
                                progress = False
                if not result['timeout']:
                    result['exitCode'] = child.returncode
                result['processCompleted'] = True
    except Exception as error:
        infrastructure_error(result, 'process_log' if result['processCompleted'] else 'process_start_or_wait', error)
        if not result['processCompleted']:
            result.update(exitCode=125, startError=str(error))
    result['seconds'] = round(time.monotonic()-started, 3)
    return result
