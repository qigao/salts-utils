#!/usr/bin/env python3
"""Delegate native cache setup to the selected shared infrastructure release."""
import argparse
from pathlib import Path
import subprocess
import sys


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('contract', type=Path)
    contract = parser.parse_args().contract.resolve()
    subprocess.run([sys.executable, str(contract / 'scripts/setup-vcpkg-curl-cache.py'),
                    str(contract)], check=True)


if __name__ == '__main__':
    main()
