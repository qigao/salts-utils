"""Inspect actual installed images; do not build, edit, or suppress diagnostics."""
import re
import subprocess
import sys
from pathlib import Path


def verify(nm: str, image: Path) -> None:
    text = subprocess.check_output([nm, '-D', '--defined-only', str(image)], text=True)
    names = {line.split()[-1] for line in text.splitlines() if line.split()}
    private = sorted(name for name in names if re.search(r'(?:^|[._])_?cxml', name))
    if private:
        raise RuntimeError(f'{image}: private cxml definitions exported: {private}')
    required = {'salts_xml_parse', 'salts_xml_document_destroy',
                'salts_xml_document_root', 'salts_xml_node_qualified_name'}
    if not required <= names:
        raise RuntimeError(f'{image}: missing public XML facade: {sorted(required - names)}')
    print(f'{image}: public XML facade retained; private cxml definitions absent')


if __name__ == '__main__':
    if len(sys.argv) != 4:
        raise SystemExit('usage: verify_exports.py NM FIRST_SHARED_LIBRARY SECOND_SHARED_LIBRARY')
    for argument in sys.argv[2:]:
        verify(sys.argv[1], Path(argument).resolve(strict=True))
