from pathlib import Path
import subprocess, json
R=Path.cwd()
BASE='fe8b1be76626bbb4c3d6e6288e6d6da35b94c411'
assert subprocess.check_output(['git','rev-parse','HEAD'],text=True).strip()==BASE
p=R/'tbe/data_bind/data_bind.c';s=p.read_text()
a='    text = salts_xml_node_text_view(node).data;'
b='    text = salts_xml_node_text_view(node).data;\n    /* An existing empty element binds as an empty string, not a missing field. */\n    if (!text && salts_xml_node_type(node) == SALTS_XML_ELEMENT) text = "";'
assert s.count(a)==1
s=s.replace(a,b)
s=s.replace('        salts_xml_node child = {0};\n          if (salts_xml_node_add_element(node, name, &child) != SALTS_XML_OK) return 0;', '        salts_xml_node child = {0};\n        if (salts_xml_node_add_element(node, name, &child) != SALTS_XML_OK) return 0;')
s=s.replace('      salts_xml_node child = {0};\n          if (salts_xml_node_add_element(node, "item", &child) != SALTS_XML_OK) return 0;', '      salts_xml_node child = {0};\n      if (salts_xml_node_add_element(node, "item", &child) != SALTS_XML_OK) return 0;')
p.write_text(s)
p=R/'tests/databind_direct_parsers/installed/consumer.c';s=p.read_text()
a='  failed = 0;'
b='  data_bind_value_free(value);\n  value = NULL;\n  const char xml[] = "<Event><at>Sat, 04 Mar 2006 13:27:54 GMT</at><id>7</id><name/></Event>";\n  if (data_bind_parse_xml(bind, "Event", xml, sizeof(xml) - 1, &value, &error) != DATA_BIND_OK)\n    goto cleanup;\n  name = data_bind_value_as_string(data_bind_value_get(value, "name"));\n  if (!name || name[0] != \'\\0\') goto cleanup;\n  failed = 0;'
assert s.count(a)==1;p.write_text(s.replace(a,b))
subprocess.run(['git','diff','--check'],check=True)
subprocess.run(['git','add','tbe/data_bind/data_bind.c','tests/databind_direct_parsers/installed/consumer.c'],check=True)
subprocess.run(['git','-c','user.name=github-actions[bot]','-c','user.email=41898282+github-actions[bot]@users.noreply.github.com','commit','-m','test(databind): preserve empty XML text with native installed consumers'],check=True)
head=subprocess.check_output(['git','rev-parse','HEAD'],text=True).strip()
tree=subprocess.check_output(['git','rev-parse','HEAD^{tree}'],text=True).strip()
subprocess.run(['git','push','origin','HEAD:refs/heads/refactor/databind-direct-salts-parsers'],check=True)
newpin='011d12c5931f147fea7266ef89c92fc9882e5034'
oldpin='c0e48acdb0aeec94199c1e6056c9f018f1e9f746'
workflow_names=['tbe-30-green.yml','tbe-32-33-schema-boundaries.yml','databind-direct-parsers.yml']
entries=[]
for name in workflow_names:
 p=R/'.github/workflows'/name;s=p.read_text();assert oldpin in s
 s=s.replace(oldpin,newpin).replace('Record verified CMeta dependency revision','Record native parser dependency revision')
 if name=='tbe-30-green.yml':
  s=s.replace("      - 'tbe/**'", "      - 'tbe/**'\n      - 'tests/databind_direct_parsers/**'")
  s=s.replace('cmake --preset linux-dev-user\n','cmake --preset linux-dev-user -DENABLE_SANITIZER_UNDEFINED=ON\n')
  s=s.replace('-DCMAKE_EXPORT_COMPILE_COMMANDS=ON 2>&1', '-DCMAKE_INSTALL_PREFIX="$RUNNER_TEMP/utils-native-installed" \\\n            -DCMAKE_EXPORT_COMPILE_COMMANDS=ON 2>&1')
  s=s.replace("required = {'tbe_typed.c'", "required = {'data_bind.c', 'test_data_bind_public_api.c', 'tbe_typed.c'")
  marker='      - name: Verify committed source was not modified'
  assert s.count(marker)==1
  step='''      - name: Verify direct parser dependencies
        shell: bash
        run: python3 tests/databind_direct_parsers/check_boundary.py -v

      - name: Install and verify the native parser consumer
        shell: bash
        env:
          SALTS_ROOT: /opt/salts/debug
        run: |
          set -euxo pipefail
          cmake --build build/linux-gcc-debug --target install -j2 \\
            2>&1 | tee "$RUNNER_TEMP/tbe30-evidence/install.log"
          cmake -S tests/databind_direct_parsers/installed -B build/native-consumer -G Ninja \\
            -DSaltsUtils_DIR="$RUNNER_TEMP/utils-native-installed/lib/cmake/SaltsUtils"
          cmake --build build/native-consumer -j2
          ctest --test-dir build/native-consumer --no-tests=error --output-on-failure \\
            --output-junit "$RUNNER_TEMP/tbe30-evidence/installed.xml"
          python3 - <<'PYCODE'
          from pathlib import Path
          import os
          profile = Path(os.environ['RUNNER_TEMP']) / 'utils-native-installed'
          assert not list((profile / 'include').rglob('turbo_parser*.h'))
          exported = (profile / 'lib/cmake/SaltsUtils/SaltsUtilsTargets.cmake').read_text()
          assert 'parser_compat' not in exported
          PYCODE

'''
  s=s.replace(marker,step+marker)
 p=R/'tests/.native-ci-staging'/name;p.parent.mkdir(parents=True,exist_ok=True);p.write_text(s)
 sha=subprocess.check_output(['git','hash-object',str(p)],text=True).strip()
 entries.append({'path':'.github/workflows/'+name,'mode':'100644','type':'blob','sha':sha})
subprocess.run(['git','add','tests/.native-ci-staging'],check=True)
subprocess.run(['git','-c','user.name=github-actions[bot]','-c','user.email=41898282+github-actions[bot]@users.noreply.github.com','commit','-m','chore: stage native parser CI file blobs for connected writer'],check=True)
subprocess.run(['git','push','origin','HEAD:refs/heads/work/databind-native-ci-staging'],check=True)
e=Path('../evidence');e.mkdir(exist_ok=True)
(e/'workflow-trees.json').write_text(json.dumps({'head':head,'base_tree':tree,'entries':entries},indent=2))
subprocess.run(['git','archive','--format=tar.gz',head,'-o',str(e/'source.tar.gz')],check=True)
print((e/'workflow-trees.json').read_text())
