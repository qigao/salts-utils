"""Inspect actual installed images; do not build, edit, or suppress diagnostics."""
import re
import subprocess
import sys
from pathlib import Path


PUBLIC_XML_FUNCTIONS = {
    'salts_xml_parse', 'salts_xml_document_destroy',
    'salts_xml_document_root', 'salts_xml_node_qualified_name',
}


def defined_symbols(nm: str, image: Path, *, dynamic: bool) -> set[str]:
    command = [nm, '--defined-only', '--extern-only', '--format=just-symbols']
    if dynamic:
        command.append('--dynamic')
    text = subprocess.check_output(command + [str(image)], text=True)
    names = set()
    for line in text.splitlines():
        fields = line.split()
        if not fields:
            continue
        if len(fields) != 1:
            raise RuntimeError(f'{image}: unexpected nm symbol record: {line!r}')
        names.add(fields[0])
    if not names:
        raise RuntimeError(f'{image}: empty defined-symbol table')
    return names


def private_xml_symbols(nm: str, archive: Path) -> set[str]:
    names = defined_symbols(nm, archive, dynamic=False)
    if not PUBLIC_XML_FUNCTIONS <= names:
        raise RuntimeError(f'{archive}: missing public XML archive functions')
    # The installed facade archive contains the private engine as well as its
    # salts_xml_* entry points. Derive ownership from that archive, not a prefix:
    # engine globals such as _xpath_parser and bpow_LUTable have no cxml prefix.
    private = {name for name in names if not name.startswith('salts_xml_')}
    if 'cxml_find_attribute_list' not in private:
        raise RuntimeError(f'{archive}: missing the private engine regression symbol')
    return private


def check_exports(image: Path, names: set[str], private_symbols: set[str]) -> None:
    private = sorted((names & private_symbols) | {
        name for name in names if re.search(r'(?:^|[._])_?cxml', name)
    })
    if private:
        raise RuntimeError(f'{image}: private XML definitions exported: {private}')
    if not PUBLIC_XML_FUNCTIONS <= names:
        raise RuntimeError(f'{image}: missing public XML facade: '
                           f'{sorted(PUBLIC_XML_FUNCTIONS - names)}')


def verify(nm: str, archive: Path, images: list[Path]) -> None:
    private = private_xml_symbols(nm, archive)
    for image in images:
        check_exports(image, defined_symbols(nm, image, dynamic=True), private)
        print(f'{image}: public XML facade retained; '
              f'{len(private)} archive-owned private definitions absent')


if __name__ == '__main__':
    if len(sys.argv) != 5:
        raise SystemExit('usage: verify_exports.py NM XML_STATIC_ARCHIVE '
                         'FIRST_SHARED_LIBRARY SECOND_SHARED_LIBRARY')
    verify(sys.argv[1], Path(sys.argv[2]).resolve(strict=True),
           [Path(argument).resolve(strict=True) for argument in sys.argv[3:]])
