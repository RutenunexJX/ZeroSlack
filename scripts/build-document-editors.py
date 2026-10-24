"""Build pinned, offline editor resources without a global Node/package-manager install."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import shutil
import subprocess
import tarfile
import urllib.request
import zipfile

REPO = Path(__file__).resolve().parents[1]
CONFIG = REPO / 'thirdparty/document-editors'


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def write_json(path, value):
    path.write_text(json.dumps(value, ensure_ascii=False, indent=2) + '\n', encoding='utf-8')


def obtain(entry, directory):
    path = directory / entry['file']
    if not path.exists():
        print('Downloading ' + entry['file'], flush=True)
        with urllib.request.urlopen(entry['url'], timeout=120) as response:
            data = response.read()
        if hashlib.sha256(data).hexdigest() != entry['sha256']:
            raise ValueError('Input checksum mismatch: ' + entry['file'])
        path.write_bytes(data)
    if sha(path) != entry['sha256']:
        raise ValueError('Input checksum mismatch: ' + str(path))
    return path


def extract(path, destination):
    stamp = destination / '.input-sha256'
    if stamp.exists():
        if stamp.read_text() != sha(path): raise ValueError('Changed extraction input: ' + str(path))
        return
    destination.mkdir(parents=True, exist_ok=False)
    if zipfile.is_zipfile(path):
        with zipfile.ZipFile(path) as archive:
            for name in archive.namelist():
                (destination / name).resolve().relative_to(destination.resolve())
            archive.extractall(destination)
    else:
        with tarfile.open(path) as archive:
            archive.extractall(destination, filter='data')
    stamp.write_text(sha(path))


def collect_notices(kind, work, output, fallback):
    graph = json.loads((work / 'build-meta.json').read_text(encoding='utf-8'))
    packages = {}
    for source in graph['inputs']:
        try:
            path = (work / source).resolve()
            if not path.is_file(): continue
            path.relative_to(work.resolve())
        except (OSError, ValueError):
            continue
        for parent in path.parents:
            if parent == work.parent: break
            if (parent / 'package.json').is_file():
                metadata = json.loads((parent / 'package.json').read_text(encoding='utf-8'))
                if 'name' in metadata and 'version' in metadata:
                    packages[(metadata['name'], metadata['version'])] = (parent, metadata)
                break
    result = []
    missing = []
    for (name, version), (path, metadata) in sorted(packages.items()):
        key = re.sub(r'[^A-Za-z0-9._-]', '_', name) + '-' + version
        destination = output / 'licenses' / kind / key
        destination.mkdir(parents=True, exist_ok=True)
        shutil.copy2(path / 'package.json', destination / 'package.json')
        notices = []
        for directory, dirs, files in os.walk(path):
            dirs[:] = [d for d in dirs if d not in ('node_modules', '.git')]
            for file in files:
                if not re.match(r'^(licen[cs]e|notice|copying|copyright|ofl)([._-]|$)', file, re.I): continue
                source = Path(directory) / file
                relative = source.relative_to(path)
                target = destination / relative
                target.parent.mkdir(parents=True, exist_ok=True)
                shutil.copy2(source, target)
                notices.append(relative.as_posix())
        if not notices and (name.startswith('@blocksuite/') or name == '@muyajs/core'):
            shutil.copy2(fallback, destination / 'UPSTREAM-LICENSE.txt')
            notices.append('UPSTREAM-LICENSE.txt')
        if not notices:
            for source in path.glob('*'):
                if not source.is_file() or not source.name.lower().startswith('readme'): continue
                content = source.read_text(encoding='utf-8', errors='replace')
                if 'Permission is hereby granted' in content or 'Permission to use, copy, modify' in content:
                    shutil.copy2(source, destination / source.name)
                    notices.append(source.name)
        supplements = CONFIG / 'license-supplements' / key
        if supplements.is_dir():
            for source in supplements.iterdir():
                if source.is_file():
                    shutil.copy2(source, destination / source.name)
                    notices.append(source.name)
            # Retain the declarations, credits and original source headers of packages
            # whose tarball omitted a standalone license file.
            for source in path.rglob('*'):
                if source.is_file() and 'node_modules' not in source.relative_to(path).parts:
                    relative = source.relative_to(path)
                    target = destination / 'published-source' / relative
                    target.parent.mkdir(parents=True, exist_ok=True)
                    shutil.copy2(source, target)
        if not notices: missing.append(name + '@' + version)
        result.append(dict(name=name, version=version, declaredLicense=metadata.get('license'),
                           repository=metadata.get('repository'), notices=notices))
    write_json(output / 'licenses' / (kind + '-dependencies.json'), result)
    return missing


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--node', required=True, type=Path)
    parser.add_argument('--work-directory', required=True, type=Path)
    parser.add_argument('--output-directory', required=True, type=Path)
    parser.add_argument('--archive-directory', type=Path)
    parser.add_argument('--store-directory', type=Path)
    parser.add_argument('--skip-install', action='store_true', help='Use an already installed, unchanged lockfile workspace')
    args = parser.parse_args()
    work, output = args.work_directory.resolve(), args.output_directory.resolve()
    if output.exists(): raise ValueError('Output must be a new directory, so stale chunks cannot be shipped.')
    if output == work or work in output.parents and output.name in ('markdown', 'blocks'):
        raise ValueError('Output must be separate from the package-manager workspaces.')
    work.mkdir(parents=True, exist_ok=True)
    output.mkdir(parents=True)
    archive_dir = (args.archive_directory or work / 'archives').resolve()
    archive_dir.mkdir(parents=True, exist_ok=True)
    inputs = json.loads((CONFIG / 'inputs.json').read_text(encoding='utf-8'))
    archives = {key: obtain(value, archive_dir) for key, value in inputs.items() if isinstance(value, dict)}
    for key, path in archives.items(): extract(path, work / 'vendor' / key)
    marktext = work / 'vendor/marktext' / ('marktext-' + inputs['marktext']['commit'])
    blocksuite = work / 'vendor/blocksuite' / ('blocksuite-' + inputs['blocksuite']['commit'])
    pnpm = work / 'vendor/pnpm/package/bin/pnpm.cjs'
    env = os.environ.copy()
    env['PATH'] = str(args.node.resolve().parent) + os.pathsep + env.get('PATH', '')
    for name in ('user', 'global'):
        path = work / (name + '.npmrc')
        path.write_text('')
        env['NPM_CONFIG_' + name.upper() + 'CONFIG'] = str(path)
    env['PNPM_HOME'] = str(work / 'pnpm-home')
    (work / 'temp').mkdir(exist_ok=True)
    env['TEMP'] = env['TMP'] = str(work / 'temp')
    env['NPM_CONFIG_CACHE'] = str(work / 'npm-cache')
    env['XDG_CACHE_HOME'] = str(work / 'cache')
    env['ZEROSLACK_SOURCE_ROOT'] = str(REPO)
    missing = []
    for kind in ('markdown', 'blocks'):
        directory = work / kind
        directory.mkdir(exist_ok=True)
        if args.skip_install:
            for name in ('package.json', 'pnpm-lock.yaml'):
                existing = directory / name
                if not existing.is_file() or existing.read_text(encoding='utf-8') != (CONFIG / kind / name).read_text(encoding='utf-8'):
                    raise ValueError('The installed workspace inputs changed. Run without --skip-install: ' + kind)
            if not (directory / 'node_modules/.pnpm/lock.yaml').is_file():
                raise ValueError('The dependency installation is missing: ' + kind)
        for name in ('package.json', 'pnpm-lock.yaml', 'build.mjs'):
            shutil.copy2(CONFIG / kind / name, directory / name)
        if kind == 'markdown': shutil.copytree(marktext / 'packages/muya', directory / 'muya', dirs_exist_ok=True)
        if not args.skip_install:
            subprocess.run([str(args.node), str(pnpm), 'install', '--frozen-lockfile', '--ignore-scripts',
                            '--store-dir', str((args.store_directory or work / 'pnpm-store').resolve()),
                            '--reporter', 'append-only', '--config.update-notifier=false'], cwd=directory, env=env, check=True)
        env['ZEROSLACK_WEB_OUTPUT'] = str(output / kind)
        # These flat files are generated by the current build hooks; do not
        # distribute an obsolete adaptation left by a previous local build.
        patched_sources = directory / 'patched-sources'
        if patched_sources.exists():
            for generated_source in patched_sources.glob('*.ts'):
                if generated_source.is_file(): generated_source.unlink()
        subprocess.run([str(args.node), 'build.mjs'], cwd=directory, env=env, check=True)
        missing += collect_notices(kind, directory, output, (marktext if kind == 'markdown' else blocksuite) / 'LICENSE')
    licenses = output / 'licenses'
    for source, name in ((marktext / 'LICENSE', 'MarkText-Muya-MIT.txt'), (blocksuite / 'LICENSE', 'BlockSuite-MPL-2.0.txt'),
                         (work / 'vendor/webview2/LICENSE.txt', 'WebView2-SDK-LICENSE.txt'),
                         (CONFIG / 'OpenSans-OFL.txt', 'OpenSans-OFL.txt')):
        shutil.copy2(source, licenses / name)
    shutil.copy2(CONFIG / 'NOTICE.txt', output / 'NOTICE.txt')
    sources = output / 'sources'
    sources.mkdir()
    for key in ('marktext', 'blocksuite'): shutil.copy2(archives[key], sources / archives[key].name)
    shutil.copytree(CONFIG, sources / 'build-config')
    shutil.copytree(REPO / 'src/documents/web', sources / 'zeroslack-adapters')
    shutil.copy2(__file__, sources / 'build-document-editors.py')
    if (work / 'blocks/patched-sources').exists(): shutil.copytree(work / 'blocks/patched-sources', sources / 'modified-blocksuite')
    if (work / 'markdown/patched-sources').exists(): shutil.copytree(work / 'markdown/patched-sources', sources / 'modified-muya')
    write_json(output / 'licenses/missing-license-text.json', missing)
    if missing: raise ValueError('Dependency license text requires review: ' + ', '.join(missing))
    manifest = dict(format='zeroslack.document-editors', version=1, inputs=inputs,
                    runtimeFiles={'WebView2Loader.dll': sha(work / 'vendor/webview2/build/native/x64/WebView2Loader.dll')},
                    files={p.relative_to(output).as_posix(): sha(p) for p in sorted(output.rglob('*')) if p.is_file()})
    write_json(output / 'manifest.json', manifest)
    print(json.dumps(dict(resources=str(output), sdk=str(work / 'vendor/webview2'), files=len(manifest['files']))), flush=True)


if __name__ == '__main__':
    main()
