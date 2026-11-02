#!/usr/bin/env python3
"""Bootstrap the pinned shared vcpkg contract with the curl package adapter."""
import argparse
import json
import os
from pathlib import Path
import re
import tempfile
import subprocess
import sys
import time

from github_packages import export


def bootstrap(contract, temporary):
    """Use the shared curl bootstrap as the sole owner of tools/ABI paths."""
    with tempfile.TemporaryDirectory(prefix='saltsutils-bootstrap-', dir=temporary) as stage:
        stage = Path(stage)
        environment = dict(os.environ, GITHUB_ENV=str(stage / 'environment'),
                           GITHUB_OUTPUT=str(stage / 'outputs'))
        subprocess.run([
            sys.executable, str(contract / 'scripts/setup-vcpkg-curl-cache.py'), str(contract),
        ], env=environment, check=True)
        values = {}
        for line in (stage / 'environment').read_text(encoding='utf-8-sig').splitlines():
            name, value = line.split('=', 1)
            if not re.fullmatch('[A-Z][A-Z0-9_]*', name):
                raise ValueError('invalid shared bootstrap environment key')
            values[name] = value
        for name in ('VCPKG_ROOT', 'VCPKG_BINARY_CACHE', 'VCPKG_CACHE_REPOSITORY_ROOT',
                     'QIGAO_VCPKG_WINDOWS_TRIPLET', 'QIGAO_VCPKG_WINDOWS_HOST_TRIPLET'):
            if not values.get(name):
                raise ValueError('shared curl bootstrap omitted ' + name)
        return values


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('contract', type=Path)
    args = parser.parse_args()
    contract = args.contract.resolve()
    temporary = Path(os.environ['RUNNER_TEMP'])
    values = bootstrap(contract, temporary)
    os.environ.update(values)
    cache = Path(values['VCPKG_BINARY_CACHE'])
    ready = temporary / 'saltsutils-curl-cache-ready.json'
    ready.unlink(missing_ok=True)
    log = temporary / 'saltsutils-curl-cache.log'
    with log.open('w') as out:
        process = subprocess.Popen([
            sys.executable, str(Path(__file__).with_name('github_packages.py')), 'serve-cache',
            '--packages', str(cache), '--ready', str(ready),
        ], stdin=subprocess.DEVNULL, stdout=out, stderr=subprocess.STDOUT,
            start_new_session=os.name != 'nt')
    for _ in range(100):
        if ready.is_file():
            try:
                port = json.loads(ready.read_text())['port']
                break
            except (ValueError, KeyError):
                pass
        if process.poll() is not None:
            raise RuntimeError('curl binary-cache adapter failed; see ' + str(log))
        time.sleep(0.1)
    else:
        process.terminate()
        raise RuntimeError('curl binary-cache adapter did not start')
    values['VCPKG_BINARY_SOURCES'] = (
        f'clear;files,{cache},readwrite;'
        f'http,http://127.0.0.1:{port}/{{name}}/{{triplet}}/{{version}}/{{sha}},read')
    export(values)


if __name__ == '__main__':
    main()
