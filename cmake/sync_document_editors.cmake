foreach(required PYTHON_EXECUTABLE SOURCE_ROOT BUILD_ROOT RUNTIME_ROOT)
    if(NOT DEFINED ${required} OR "${${required}}" STREQUAL "")
        message(FATAL_ERROR "Missing document resource sync argument: ${required}")
    endif()
endforeach()

set(sync_code [=[
import hashlib, os, pathlib, shutil, stat, sys

def checked(path):
    path = pathlib.Path(path)
    if not path.is_absolute() or '..' in path.parts:
        raise RuntimeError('Resource sync requires an absolute normalized path: ' + str(path))
    current = pathlib.Path(path.anchor)
    for part in path.parts[1:]:
        current /= part
        if os.path.lexists(current):
            info = current.lstat()
            if stat.S_ISLNK(info.st_mode) or getattr(info, 'st_file_attributes', 0) & 0x400:
                raise RuntimeError('Resource sync refuses a reparse point: ' + str(current))
    return path.resolve()

def inventory(root):
    files, directories = {}, set()
    if not root.exists():
        return files, directories
    if not root.is_dir():
        raise RuntimeError('Resource sync root is not a directory: ' + str(root))
    for parent, names, leaves in os.walk(root, followlinks=False):
        for name in names + leaves:
            path = checked(pathlib.Path(parent) / name)
            info = path.lstat()
            key = path.relative_to(root)
            if stat.S_ISDIR(info.st_mode):
                directories.add(key)
            elif stat.S_ISREG(info.st_mode):
                files[key] = path
            else:
                raise RuntimeError('Resource sync refuses a special file: ' + str(path))
    return files, directories

source, build, runtime = map(checked, sys.argv[1:])
if not source.is_dir() or not build.is_dir() or not runtime.is_dir():
    raise RuntimeError('Resource source, build and runtime roots must exist')
if not runtime.is_relative_to(build):
    raise RuntimeError('Runtime root must be inside this build')
target = checked(runtime / 'resources' / 'document-editors')
if target == build or not target.is_relative_to(build):
    raise RuntimeError('Resource target must be the dedicated directory inside this build')
if source.is_relative_to(target) or target.is_relative_to(source):
    raise RuntimeError('Resource source and target overlap')
source_files, source_dirs = inventory(source)
target_files, target_dirs = inventory(target)
if source_files.keys() & target_dirs or source_dirs & target_files.keys():
    raise RuntimeError('Resource source and target have conflicting file/directory types')

# Validate both complete trees before writing or deleting generated files.
target.mkdir(parents=True, exist_ok=True)
for key in sorted(source_dirs, key=lambda p: (len(p.parts), str(p))):
    checked(target / key).mkdir(exist_ok=True)
for key, path in source_files.items():
    destination = checked(target / key)
    if not destination.exists() or hashlib.sha256(path.read_bytes()).digest() != hashlib.sha256(destination.read_bytes()).digest():
        shutil.copy2(checked(path), destination)
for key in target_files.keys() - source_files.keys():
    checked(target / key).unlink()
for key in sorted(target_dirs - source_dirs, key=lambda p: (-len(p.parts), str(p))):
    checked(target / key).rmdir()
print('Synchronized document resources: ' + str(len(source_files)) + ' files')
]=])

execute_process(
    COMMAND "${PYTHON_EXECUTABLE}" -I -B -X utf8 -c "${sync_code}"
        "${SOURCE_ROOT}" "${BUILD_ROOT}" "${RUNTIME_ROOT}"
    RESULT_VARIABLE sync_result)
if(NOT sync_result EQUAL 0)
    message(FATAL_ERROR "Document resource synchronization failed (${sync_result})")
endif()
