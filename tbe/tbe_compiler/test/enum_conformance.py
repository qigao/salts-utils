"""Compile and execute real C/C++/Go enum output; reject invalid schemas atomically."""
import argparse
import os
from pathlib import Path
import subprocess
import tempfile
import unittest

PARSER = argparse.ArgumentParser()
PARSER.add_argument('--compiler', required=True, type=Path)
PARSER.add_argument('--source', required=True, type=Path)
PARSER.add_argument('--salts-include', required=True, type=Path)
ARGS, REST = PARSER.parse_known_args()
ARGS.compiler = ARGS.compiler.resolve()
ARGS.source = ARGS.source.resolve()
ARGS.salts_include = ARGS.salts_include.resolve()


class EnumConformance(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory(prefix='tbe-enums-')
        self.addCleanup(self.temp.cleanup)
        self.path = Path(self.temp.name)

    def run_command(self, argv):
        return subprocess.run([str(a) for a in argv], cwd=self.path, text=True,
                              stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                              timeout=90, env={**os.environ, 'GOWORK': 'off',
                              'GOPROXY': 'off', 'GOTOOLCHAIN': 'local'})

    def generate(self, schema, language, expected=True):
        src = self.path / 'input.schema'
        src.write_text('schema enumprobe;\n' + schema, encoding='utf-8')
        out = self.path / ('types.' + {'c': 'h', 'cpp': 'hpp', 'go': 'go',
                                      'ts': 'ts', 'rust': 'rs', 'python': 'py'}[language])
        if language == 'python':
            out = self.path / 'enum_values.py'
        sentinel = b'previous-valid-output\n'
        out.write_bytes(sentinel)
        result = self.run_command([ARGS.compiler, src, '--lang', language, '--output', out])
        if expected:
            self.assertEqual(result.returncode, 0, result.stdout)
            self.assertNotEqual(out.read_bytes(), sentinel)
        else:
            self.assertNotEqual(result.returncode, 0, schema + '\n' + result.stdout)
            self.assertEqual(out.read_bytes(), sentinel, 'failure replaced existing output')
            out.unlink()
            result = self.run_command([ARGS.compiler, src, '--lang', language, '--output', out])
            self.assertNotEqual(result.returncode, 0, result.stdout)
            self.assertFalse(out.exists(), 'failure published a partial output')
        return out

    def compile_c(self, body, cpp=False):
        name = 'main.cpp' if cpp else 'main.c'
        source = self.path / name
        header = 'types.hpp' if cpp else 'types.h'
        source.write_text('#include <stdint.h>\n#include <string.h>\n#include "' + header +
                          '"\nint main(void) {\n' + body + '\nreturn 0;\n}\n')
        result = self.run_command(['c++' if cpp else 'cc', '-std=c++17' if cpp else '-std=c11',
                                  '-Wall', '-Wextra', '-Werror', '-pedantic',
                                  '-I' + str(ARGS.source / 'tbe/schema/include'),
                                  '-I' + str(ARGS.salts_include), source, '-o', 'probe'])
        self.assertEqual(result.returncode, 0, result.stdout)
        result = self.run_command([self.path / 'probe'])
        self.assertEqual(result.returncode, 0, result.stdout)

    def compile_go(self, body):
        (self.path / 'go.mod').write_text('module enumprobe\n\ngo 1.22\n')
        (self.path / 'types_test.go').write_text(
            'package enumprobe\nimport ("testing"; "reflect")\n'
            'func TestValues(t *testing.T) {\n' + body + '\n}\n')
        # C sources from the companion probe must not become cgo input.
        for suffix in ('main.c', 'main.cpp'):
            (self.path / suffix).unlink(missing_ok=True)
        result = self.run_command(['go', 'test', '-count=1', './...'])
        self.assertEqual(result.returncode, 0, result.stdout)

    def check_widths(self, signed):
        schemas, c_checks, go_checks = [], [], []
        for bits in (8, 16, 32, 64):
            prefix = 'int' if signed else 'uint'
            typ = prefix + str(bits)
            low = -(1 << (bits - 1)) if signed else 0
            high = (1 << (bits - int(signed))) - 1
            name = 'E' + str(bits)
            schemas.append(f'enum {name} <{typ}> {{ Low = {low}; High = {high}; }}')
            c_low = 'INT' + str(bits) + '_MIN' if signed else '0'
            c_high = prefix.upper() + str(bits) + '_MAX'
            c_checks.append(f'if (sizeof({name}_t) != {bits // 8} || {name}_Low != {c_low} || '
                            f'{name}_High != {c_high}) return 1;\n'
                            f'if ({name}_min() != {name}_Low || {name}_max() != {name}_High || '
                            f'!{name}_is_valid({name}_Low) || !{name}_is_valid({name}_High) || '
                            f'strcmp({name}_to_string({name}_Low), "Low") != 0) return 2;')
            go_checks.append(f'if reflect.TypeOf({name}_High).Kind() != reflect.{prefix.title()}{bits} '
                             f'|| {name}_Low != {low} || {name}_High != {high} '
                             '{ t.Fatal("storage or value changed") }')
        schema = '\n'.join(schemas)
        self.generate(schema, 'c')
        self.compile_c('\n'.join(c_checks))
        self.generate(schema, 'go')
        self.compile_go('\n'.join(go_checks))

    def test_unsigned_boundaries(self):
        self.check_widths(False)

    def test_signed_boundaries(self):
        self.check_widths(True)

    def test_alias_policy(self):
        for kind in ('enum', 'flags'):
            for values in ('A=1; B=1;', 'A=1; B=0x01;', 'A=1; B=01;', 'A=0; B; C=1;', 'A=1; A=2;'):
                with self.subTest(kind=kind, values=values):
                    self.generate(f'{kind} E <uint8> {{ {values} }}', 'c', False)

    def test_range_and_shape_rejections(self):
        for kind in ('enum', 'flags'):
            for typ, value in [('int8','128'), ('int8','-129'), ('uint8','-1'),
                               ('uint8','256'), ('int64','9223372036854775808'),
                               ('int64','-9223372036854775809'),
                               ('uint64','18446744073709551616'), ('uint64','9' * 100),
                               ('uint64','1.5'), ('uint64','1e3'), ('float','1'),
                               ('bool','1'), ('missing','1')]:
                with self.subTest(kind=kind, typ=typ, value=value):
                    self.generate(f'{kind} E <{typ}> {{ A={value}; }}', 'c', False)
            self.generate(f'{kind} E {{ }}', 'c', False)

    def test_implicit_overflow(self):
        for kind, typ, high in [('enum','uint8',255), ('enum','int8',127),
                               ('enum','uint64',(1 << 64)-1),
                               ('flags','uint64',1 << 63)]:
            with self.subTest(kind=kind, typ=typ):
                self.generate(f'{kind} E <{typ}> {{ Last={high}; Next; }}', 'c', False)
                self.generate(f'{kind} E <{typ}> {{ Last={high}; Reset=0; }}', 'c')

    def test_flags_and_integer_aliases(self):
        schema = ('flags F <u64> { Low=1; High=9223372036854775808; }\n'
                  'enum Alias <byte> { Last=255; }\n'
                  'enum Decimal <u16> { Eight=008; Nine; }')
        self.generate(schema, 'c')
        self.compile_c('if (sizeof(F_t)!=8 || !F_has(F_set(F_Low,F_High),F_High) || '
                       'F_clear(F_High,F_High)!=0 || Alias_Last!=255 || Decimal_Nine!=9) return 1;')
        self.generate(schema, 'go')
        self.compile_go('if reflect.TypeOf(F_High).Kind()!=reflect.Uint64 || '
                        'reflect.TypeOf(Alias_Last).Kind()!=reflect.Uint8 || Decimal_Nine!=9 '
                        '{ t.Fatal("alias width or value changed") }')

    def test_cpp_literal_portability(self):
        self.generate('enum Wide <uint64> { Max=18446744073709551615; } '
                      'enum Signed <int64> { Min=-9223372036854775808; }', 'cpp')
        self.compile_c('if (static_cast<uint64_t>(Wide::Max)!=UINT64_MAX || '
                       'static_cast<int64_t>(Signed::Min)!=INT64_MIN) return 1;', cpp=True)

    def test_rulesforge_rejects_value_loss_before_any_output(self):
        src = self.path / 'input.schema'
        header, dsl = self.path / 'types.h', self.path / 'types.rfl'
        sentinel = b'previous-valid-output\n'
        for members, expected in [('Zero=0; One=1;', True), ('One=1; Two=2;', False),
                                  ('High=9223372036854775808;', False)]:
            with self.subTest(members=members):
                src.write_text('schema enumprobe; enum E <uint64> {' + members + '}')
                header.write_bytes(sentinel)
                dsl.write_bytes(sentinel)
                result = self.run_command([ARGS.compiler, src, '--lang', 'c', '--output',
                                           header, '--dsl-output', dsl])
                if expected:
                    self.assertEqual(result.returncode, 0, result.stdout)
                    self.assertIn('enum E <uint64>', dsl.read_text())
                else:
                    self.assertNotEqual(result.returncode, 0, result.stdout)
                    self.assertEqual(header.read_bytes(), sentinel)
                    self.assertEqual(dsl.read_bytes(), sentinel)

    def test_python_keeps_exact_canonical_values(self):
        self.generate('enum E <uint64> { Eight=008; Max=18446744073709551615; } '
                      'enum S <int64> { Min=-9223372036854775808; }', 'python')
        result = self.run_command(['python3', '-c',
                                  ''
                                  'import runpy; d=runpy.run_path("enum_values.py"); '
                                  'assert int(d["E"].Max)==18446744073709551615; '
                                  'assert int(d["E"].Eight)==8; '
                                  'assert int(d["S"].Min)==-9223372036854775808'])
        self.assertEqual(result.returncode, 0, result.stdout)

    def test_typescript_rejects_inexact_values(self):
        self.generate('enum Wide <uint64> { Max=18446744073709551615; }', 'ts', False)
        self.generate('enum Wide <uint64> { Value=9007199254740993; }', 'ts', False)
        self.generate('enum Small <uint32> { Max=4294967295; }', 'ts')


if __name__ == '__main__':
    unittest.main(argv=['enum_conformance'] + REST, verbosity=2)
