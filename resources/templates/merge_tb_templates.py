"""Prepare a merged catalog without modifying the existing user configuration."""
import argparse
import hashlib
import json
from pathlib import Path


def names(record):
    return {str(value).strip().lstrip('@').casefold()
            for value in [record.get('command', ''), *record.get('aliases', [])]
            if str(value).strip()}


def merge(existing, additions):
    if not isinstance(existing, dict) or not isinstance(existing.get('templates', []), list):
        raise ValueError('Expected a catalog object with a templates array')
    result = dict(existing)
    result['templates'] = list(existing.get('templates', []))
    report = {'added': [], 'unchanged': [], 'conflicts': []}
    for record in additions['templates']:
        matches = [old for old in result['templates']
                   if old.get('id') == record.get('id') or names(old) & names(record)]
        if matches:
            if len(matches) == 1 and matches[0] == record:
                report['unchanged'].append(record['command'])
            else:
                report['conflicts'].append({'command': record['command'],
                    'existingIds': [old.get('id') for old in matches],
                    'decision': 'existing content retained; incoming definition omitted'})
        else:
            result['templates'].append(record)
            report['added'].append(record['command'])
    return result, report


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--existing', type=Path, help='Existing catalog; omit for a new one')
    parser.add_argument('--templates', type=Path, default=Path(__file__).with_name('tb-dut.json'),
                        help='Catalog containing the definitions to merge')
    parser.add_argument('--output', required=True, type=Path, help='New reviewable output, must not exist')
    args = parser.parse_args()
    asset = args.templates
    raw = args.existing.read_bytes() if args.existing else b'{"version":1,"templates":[]}'
    result, report = merge(json.loads(raw.decode('utf-8-sig')), json.loads(asset.read_text(encoding='utf-8')))
    report.update(existing=str(args.existing) if args.existing else None,
                  existingSha256=hashlib.sha256(raw).hexdigest(),
                  assetSha256=hashlib.sha256(asset.read_bytes()).hexdigest())
    report_path = args.output.with_name(args.output.name + '.merge-report.json')
    if args.output.exists() or report_path.exists():
        parser.error('Output or report already exists; choose a new review destination')
    with args.output.open('x', encoding='utf-8') as output:
        output.write(json.dumps(result, ensure_ascii=False, indent=2) + '\n')
    with report_path.open('x', encoding='utf-8') as output:
        output.write(json.dumps(report, ensure_ascii=False, indent=2) + '\n')
    print(json.dumps(report, ensure_ascii=False))


if __name__ == '__main__':
    main()
