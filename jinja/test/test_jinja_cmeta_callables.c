#include "jinja_cmeta_test_support.h"

spec("Jinja CMeta callable values") {
  it("renders macros with defaults resolved inside the parameter frame") {
    JinjaTestRoot root = {{vstr_from_cstr("Ada"), 0}, false, {NULL, 0u, 0u, NULL}};
    JinjaTestModel model;
    jinja_test_model_init(&model);
    JINJA_CMETA_ERROR error = JINJA_CMETA_ERROR_INIT;
    char *output = NULL;
    check_equal(jinja_test_render("{% macro f(a=b,b=2) %}{{a}}:{{b}}{% endmacro %}{{f()}}|{{f(b=3)}}",
        &model, &root, NULL, &output, &error), JINJA_CMETA_OK);
    check_equal(output, ":2|3:3");
    free(output);
  }

  it("keeps macro captures lexical while observing later defining frame writes") {
    JinjaTestRoot root = {{vstr_from_cstr("Ada"), 0}, false, {NULL, 0u, 0u, NULL}};
    JinjaTestModel model;
    jinja_test_model_init(&model);
    JINJA_CMETA_ERROR error = JINJA_CMETA_ERROR_INIT;
    char *output = NULL;
    check_equal(jinja_test_render("{% set x='outer' %}{% macro f() %}{{x}}{% endmacro %}"
        "{% with x='caller' %}{{f()}}{% endwith %}{% set x='later' %}|{{f()}}",
        &model, &root, NULL, &output, &error), JINJA_CMETA_OK);
    check_equal(output, "outer|later");
    free(output);
  }

  it("supports recursive macro and caller invocation") {
    JinjaTestRoot root = {{vstr_from_cstr("Ada"), 0}, false, {NULL, 0u, 0u, NULL}};
    JinjaTestModel model;
    jinja_test_model_init(&model);
    JINJA_CMETA_ERROR error = JINJA_CMETA_ERROR_INIT;
    char *output = NULL;
    check_equal(jinja_test_render("{% macro f(n) %}{{n}}{% if n %}{{f(n-1)}}{% endif %}{% endmacro %}{{f(2)}}|"
        "{% macro wrap() %}[{{caller('Ada')}}]{% endmacro %}{% call(name) wrap() %}Hi {{name}}{% endcall %}",
        &model, &root, NULL, &output, &error), JINJA_CMETA_OK);
    check_equal(output, "210|[Hi Ada]");
    free(output);
  }

  it("expands filter arguments across builtin signatures and chains") {
    static const struct { const char *source; const char *expected; } cases[] = {
      {"{{missing|default(*['x'],**{'boolean':true})}}", "x"},
      {"{{' aba '|trim(**{'chars':' '})|replace(*['a','x'],**{'count':1})}}", "xba"},
      {"{{'x'|center(*[3])}}|{{user|attr(**{'name':'name'})}}", " x |Ada"},
      {"{{[1,2]|sum(**{'start':3})}}|{{[1,2]|join(*[':'])}}", "6|1:2"},
      {"{{[1,2]|length(*[],**{})}}|{{'<b>'|escape(*[])}}", "2|&lt;b&gt;"},
      {"{% filter replace(*['a','b']) %}a{% endfilter %}", "b"},
      {"{% set x|trim(**{'chars':' '}) %} a {% endset %}{{x}}", "a"},
      {"{% set it=['x',true]|reverse %}{{false|default(*it)}}|{{it|list}}", "True|[]"}
    };
    JinjaTestRoot root = {{vstr_from_cstr("Ada"), 0}, false, {NULL, 0u, 0u, NULL}};
    JinjaTestModel model;
    jinja_test_model_init(&model);
    for (size_t i = 0u; i < sizeof(cases) / sizeof(cases[0]); ++i) {
      JINJA_CMETA_ERROR error = JINJA_CMETA_ERROR_INIT;
      char *output = NULL;
      info("source: %s", cases[i].source);
      check_equal(jinja_test_render(cases[i].source, &model, &root, NULL, &output, &error), JINJA_CMETA_OK);
      check_equal(output, cases[i].expected);
      free(output);
    }
  }

  it("rejects invalid expanded filter bindings") {
    static const char *const sources[] = {
      "{{missing|default('x',**{'default_value':'y'})}}", "{{'a'|replace(*['b'])}}",
      "{{'a'|trim(**{'width':3})}}", "{{[1]|length(*[2])}}", "{{'a'|trim(**none)}}"
    };
    JinjaTestRoot root = {{vstr_from_cstr("Ada"), 0}, false, {NULL, 0u, 0u, NULL}};
    JinjaTestModel model;
    jinja_test_model_init(&model);
    for (size_t i = 0u; i < sizeof(sources) / sizeof(sources[0]); ++i) {
      JINJA_CMETA_ERROR error = JINJA_CMETA_ERROR_INIT;
      char *output = NULL;
      check_equal(jinja_test_render(sources[i], &model, &root, NULL, &output, &error), JINJA_CMETA_ERR_RENDER);
      check_null(output);
    }
  }

  it("bounds filter expansion and preserves eager failure ordering") {
    JinjaTestRoot root = {{vstr_from_cstr("Ada"), 0}, false, {NULL, 0u, 0u, NULL}};
    JinjaTestModel model;
    jinja_test_model_init(&model);
    JINJA_CMETA_ERROR error = JINJA_CMETA_ERROR_INIT;
    JINJA_CMETA_RENDER_OPTIONS options = JINJA_CMETA_RENDER_OPTIONS_INIT;
    char *output = NULL;
    check_equal(jinja_test_render("{{'a'|trim(*range(100))}}", &model, &root, NULL, &output, &error), JINJA_CMETA_ERR_CAPACITY);
    options.max_string_bytes = 2u;
    check_equal(jinja_test_render("{{'a'|trim(*none,chars=user.name|list)}}", &model, &root, &options, &output, &error), JINJA_CMETA_ERR_RENDER);
    check_equal(jinja_test_render("{{'a'|trim(*[],wrong=user.name|list)}}", &model, &root, &options, &output, &error), JINJA_CMETA_ERR_CAPACITY);
    check_null(output);
  }

  it("expands test positional and keyword arguments") {
    static const struct { const char *source; const char *expected; } cases[] = {
      {"{{6 is divisibleby(*[3])}}|{{6 is not divisibleby(**{'num':4})}}", "True|True"},
      {"{{2 is in(**{'seq':[1,2]})}}|{{2 is eq(*[2],**{})}}", "True|True"},
      {"{{missing is undefined(*[],**{})}}|{{user.name is string(*[])}}", "True|True"},
      {"{% set it=[3]|reverse %}{{6 is divisibleby(*it)}}|{{it|list}}", "True|[]"},
      {"{{false and (6 is divisibleby(**none))}}", "False"}
    };
    JinjaTestRoot root = {{vstr_from_cstr("Ada"), 0}, false, {NULL, 0u, 0u, NULL}};
    JinjaTestModel model;
    jinja_test_model_init(&model);
    for (size_t i = 0u; i < sizeof(cases) / sizeof(cases[0]); ++i) {
      JINJA_CMETA_ERROR error = JINJA_CMETA_ERROR_INIT;
      char *output = NULL;
      info("source: %s", cases[i].source);
      check_equal(jinja_test_render(cases[i].source, &model, &root, NULL, &output, &error), JINJA_CMETA_OK);
      check_equal(output, cases[i].expected);
      free(output);
    }
  }

  it("rejects invalid expanded test bindings and bounds argument snapshots") {
    static const char *const sources[] = {
      "{{6 is divisibleby(*[])}}", "{{6 is divisibleby(*[2,3])}}",
      "{{6 is divisibleby(2,**{'num':3})}}", "{{2 is eq(**{'b':2})}}",
      "{{missing is defined(**{'x':1})}}", "{{2 is in(**none)}}"
    };
    JinjaTestRoot root = {{vstr_from_cstr("Ada"), 0}, false, {NULL, 0u, 0u, NULL}};
    JinjaTestModel model;
    jinja_test_model_init(&model);
    for (size_t i = 0u; i < sizeof(sources) / sizeof(sources[0]); ++i) {
      JINJA_CMETA_ERROR error = JINJA_CMETA_ERROR_INIT;
      char *output = NULL;
      check_equal(jinja_test_render(sources[i], &model, &root, NULL, &output, &error), JINJA_CMETA_ERR_RENDER);
      check_null(output);
    }
    JINJA_CMETA_ERROR error = JINJA_CMETA_ERROR_INIT;
    char *output = NULL;
    check_equal(jinja_test_render("{{6 is divisibleby(*range(100))}}", &model, &root, NULL, &output, &error), JINJA_CMETA_ERR_CAPACITY);
    check_null(output);
  }

  it("expands loop methods and recursive iterable arguments") {
    static const struct { const char *source; const char *expected; } cases[] = {
      {"{% for x in [1,1,2] %}{{loop.cycle(*['a','b'],**{})}}:{{loop.changed(*[x])}};{% endfor %}", "a:True;b:False;a:True;"},
      {"{% for x in [1,2] %}{{loop.changed(*[])}}{% endfor %}", "TrueFalse"},
      {"{% for xs in [[1],[1,2],[1,2],[]] %}{{loop.changed(*xs)}};{% endfor %}", "True;True;False;True;"},
      {"{% set it=['a','b']|reverse %}{% for x in [1] %}{{loop.cycle(*it)}}{% endfor %}|{{it|list}}", "b|[]"},
      {"{% for x in [2] recursive %}{{x}}{% if x %}{{loop(*[[x-1]],**{})}}{% endif %}{% endfor %}", "210"},
      {"{% for x in [2] recursive %}{{x}}{% if x %}{{loop(**{'iterable':[x-1]})}}{% endif %}{% endfor %}", "210"}
    };
    JinjaTestRoot root = {{vstr_from_cstr("Ada"), 0}, false, {NULL, 0u, 0u, NULL}};
    JinjaTestModel model;
    jinja_test_model_init(&model);
    for (size_t i = 0u; i < sizeof(cases) / sizeof(cases[0]); ++i) {
      JINJA_CMETA_ERROR error = JINJA_CMETA_ERROR_INIT;
      char *output = NULL;
      info("source: %s", cases[i].source);
      check_equal(jinja_test_render(cases[i].source, &model, &root, NULL, &output, &error), JINJA_CMETA_OK);
      check_equal(output, cases[i].expected);
      free(output);
    }
  }

  it("rejects invalid expanded loop argument bindings") {
    static const char *const sources[] = {
      "{% for x in [1] %}{{loop.cycle(*[])}}{% endfor %}",
      "{% for x in [1] %}{{loop.changed(**{'x':1})}}{% endfor %}",
      "{% for x in [1] recursive %}{{loop(*[],**{})}}{% endfor %}",
      "{% for x in [1] recursive %}{{loop(*[[],[]])}}{% endfor %}",
      "{% for x in [1] recursive %}{{loop(*[[]],**{'iterable':[]})}}{% endfor %}",
      "{% for x in [1] %}{{loop.cycle(*none)}}{% endfor %}",
      "{% set it=['a','b']|reverse %}{% for x in [1,2] %}{{loop.cycle(*it)}}{% endfor %}"
    };
    JinjaTestRoot root = {{vstr_from_cstr("Ada"), 0}, false, {NULL, 0u, 0u, NULL}};
    JinjaTestModel model;
    jinja_test_model_init(&model);
    for (size_t i = 0u; i < sizeof(sources) / sizeof(sources[0]); ++i) {
      JINJA_CMETA_ERROR error = JINJA_CMETA_ERROR_INIT;
      char *output = NULL;
      check_equal(jinja_test_render(sources[i], &model, &root, NULL, &output, &error), JINJA_CMETA_ERR_RENDER);
      check_null(output);
    }
  }

  it("expands positional and keyword arguments on builtin callable values") {
    static const struct { const char *source; const char *expected; } cases[] = {
      {"{{range(*[1,5,2])|list}}", "[1, 3]"},
      {"{{range(*(3,))|list}}|{{range(*range(1,4))|list}}", "[0, 1, 2]|[1]"},
      {"{% set f=range %}{{[f][0](*[3])|join}}|{{range(4).count(*[2])}}", "012|1"},
      {"{{dict(**{'名称':2,'':3})}}", "{'名称': 2, '': 3}"},
      {"{{dict(**{user.name:1})}}", "{'Ada': 1}"},
      {"{{dict(**{'x':1,'x':2,'y':3})}}", "{'x': 2, 'y': 3}"},
      {"{{dict(if=1,**{'if':2})}}", "{'if': 2}"},
      {"{{dict(if=0,x=1,**{'x':2})}}", "{'if': 0, 'x': 2}"},
      {"{% set n=namespace(class=1,**{'class':2}) %}{{n.class}}", "2"},
      {"{% set n=namespace(**{user.name:1,'Ada':2}) %}{{n.Ada}}", "2"},
      {"{{dict(x=1,*[{'x':2}],**{'y':3})}}", "{'x': 1, 'y': 3}"},
      {"{% set n=namespace(**{'x':4}) %}{{n.x}}|{{dict(*[])}}", "4|{}"},
      {"{{range(*[range(*[2])|length])|list}}", "[0, 1]"},
      {"{{dict(*missing)}}|{{range(2,**{})|list}}", "{}|[0, 1]"}
    };
    JinjaTestRoot root = {{vstr_from_cstr("Ada"), 0}, false, {NULL, 0u, 0u, NULL}};
    JinjaTestModel model;
    jinja_test_model_init(&model);
    for (size_t i = 0u; i < sizeof(cases) / sizeof(cases[0]); ++i) {
      JINJA_CMETA_ERROR error = JINJA_CMETA_ERROR_INIT;
      char *output = NULL;
      info("source: %s", cases[i].source);
      check_equal(jinja_test_render(cases[i].source, &model, &root, NULL, &output, &error), JINJA_CMETA_OK);
      check_equal(output, cases[i].expected);
      free(output);
    }
  }

  it("rejects invalid expansion containers keys and duplicate keywords at render time") {
    static const char *const sources[] = {
      "{{dict(*none)}}", "{{dict(*1)}}", "{{dict(**none)}}", "{{dict(**[])}}",
      "{{dict(**{1:2})}}", "{{dict(x=1,**{'x':2})}}", "{{range(**{'stop':2})}}",
      "{{dict(x=1,**{'x':2,'x':3})}}",
      "{{dict(match=1,**{'match':2})}}", "{{dict(case=1,**{'case':2})}}",
      "{{range(3).count(*[1,2])}}", "{{dict(**namespace(x=1))}}", "{{dict(**missing)}}"
    };
    JinjaTestRoot root = {{vstr_from_cstr("Ada"), 0}, false, {NULL, 0u, 0u, NULL}};
    JinjaTestModel model;
    jinja_test_model_init(&model);
    for (size_t i = 0u; i < sizeof(sources) / sizeof(sources[0]); ++i) {
      JINJA_CMETA_ERROR error = JINJA_CMETA_ERROR_INIT;
      char *output = NULL;
      info("source: %s", sources[i]);
      check_equal(jinja_test_render(sources[i], &model, &root, NULL, &output, &error), JINJA_CMETA_ERR_RENDER);
      check_null(output);
    }
  }

  it("expands iterators before keyword expressions and reclaims call scratch") {
    static const struct { const char *source; const char *expected; } cases[] = {
      {"{% set it=[1,2]|reverse %}{{dict(x=it|list,*[{'y':it|first}])}}|{{it|list}}", "{'y': 2, 'x': [1]}|[]"},
      {"{% set it=[['a',1],['b',2]]|reverse %}{{dict(*[it])}}|{{it|list}}", "{'b': 2, 'a': 1}|[]"},
      {"{% for i in range(100) %}{{range(*[1])|join}}{% endfor %}|{{dict(**{'x':1})}}", NULL}
    };
    JinjaTestRoot root = {{vstr_from_cstr("Ada"), 0}, false, {NULL, 0u, 0u, NULL}};
    JinjaTestModel model;
    jinja_test_model_init(&model);
    for (size_t i = 0u; i < sizeof(cases) / sizeof(cases[0]); ++i) {
      JINJA_CMETA_ERROR error = JINJA_CMETA_ERROR_INIT;
      char *output = NULL;
      check_equal(jinja_test_render(cases[i].source, &model, &root, NULL, &output, &error), JINJA_CMETA_OK);
      if (cases[i].expected != NULL) check_equal(output, cases[i].expected);
      else { check_equal(strlen(output), (size_t)109u); check_equal(output + 100u, "|{'x': 1}"); }
      free(output);
    }
  }

  it("bounds expansion scratch and retains argument error ordering") {
    JinjaTestRoot root = {{vstr_from_cstr("Ada"), 0}, false, {NULL, 0u, 0u, NULL}};
    JinjaTestModel model;
    jinja_test_model_init(&model);
    JINJA_CMETA_ERROR error = JINJA_CMETA_ERROR_INIT;
    JINJA_CMETA_RENDER_OPTIONS options = JINJA_CMETA_RENDER_OPTIONS_INIT;
    char *output = NULL;
    check_equal(jinja_test_render("{{range(*range(100))}}", &model, &root, NULL, &output, &error), JINJA_CMETA_ERR_CAPACITY);
    check_null(output);
    check_equal(jinja_test_render("{% for x in [1] %}{{loop.cycle(*range(100))}}{% endfor %}",
        &model, &root, NULL, &output, &error), JINJA_CMETA_ERR_CAPACITY);
    check_equal(jinja_test_render("{% for x in [1] recursive %}{{loop(*[[x]])}}{% endfor %}",
        &model, &root, NULL, &output, &error), JINJA_CMETA_ERR_CAPACITY);
    options.max_string_bytes = 2u;
    check_equal(jinja_test_render("{{dict(*none,x=user.name|list)}}", &model, &root, &options, &output, &error), JINJA_CMETA_ERR_RENDER);
    check_equal(jinja_test_render("{{missing(*[],x=user.name|list)}}", &model, &root, &options, &output, &error), JINJA_CMETA_ERR_CAPACITY);
    check_equal(jinja_test_render("{{false and dict(**none)}}", &model, &root, NULL, &output, &error), JINJA_CMETA_OK);
    check_equal(output, "False");
    free(output);
  }

  it("preserves outer lookup across callable loop nodes and binds neighbor range methods") {
    static const struct { const char *source; const char *expected; } cases[] = {
      {"{% for f in [range] %}{{dict()}}|{{user.name}}{% endfor %}", "{}|Ada"},
      {"{% for r in [range(2),range(3)] %}{% if not loop.last %}{{loop.nextitem.count(1)}}{% endif %}{% endfor %}", "1"}
    };
    JinjaTestRoot root = {{vstr_from_cstr("Ada"), 0}, false, {NULL, 0u, 0u, NULL}};
    JinjaTestModel model;
    jinja_test_model_init(&model);
    for (size_t i = 0u; i < sizeof(cases) / sizeof(cases[0]); ++i) {
      JINJA_CMETA_ERROR error = JINJA_CMETA_ERROR_INIT;
      char *output = NULL;
      check_equal(jinja_test_render(cases[i].source, &model, &root, NULL, &output, &error), JINJA_CMETA_OK);
      check_equal(output, cases[i].expected);
      free(output);
    }
  }

  it("rejects noncallable values and invalid binding after eager arguments") {
    static const char *sources[] = {"{{missing()}}", "{{none()}}", "{{1()}}",
      "{% set f=range %}{{f(stop=2)}}", "{% set f=dict %}{{f(1,2)}}",
      "{% set f=namespace %}{{f(none)}}", "{{range(2).count(value=1)}}",
      "{% set range=missing %}{{range(2)}}", "{% set namespace=none %}{{namespace()}}",
      "{{range(2).count}}", "{{range(2).count|string}}", "{{range is callable(1)}}"};
    JinjaTestRoot root = {{vstr_from_cstr("Ada"), 0}, false, {NULL, 0u, 0u, NULL}};
    JinjaTestModel model;
    jinja_test_model_init(&model);
    JINJA_CMETA_ERROR error = JINJA_CMETA_ERROR_INIT;
    JINJA_CMETA_RENDER_OPTIONS options = JINJA_CMETA_RENDER_OPTIONS_INIT;
    char *output = NULL;
    for (size_t i = 0u; i < sizeof(sources) / sizeof(sources[0]); ++i) {
      info("source: %s", sources[i]);
      check_equal(jinja_test_render(sources[i], &model, &root, NULL, &output, &error), JINJA_CMETA_ERR_RENDER);
      check_null(output);
    }
    static const char *eager[] = {"{{missing(user.name|list)}}",
      "{% set range=none %}{{range(user.name|list)}}", "{{range(2).count(other=user.name|list)}}",
      "{{dict(1,other=user.name|list)}}", "{{range is callable(user.name|list)}}"};
    options.max_string_bytes = 2u;
    for (size_t i = 0u; i < sizeof(eager) / sizeof(eager[0]); ++i) {
      check_equal(jinja_test_render(eager[i], &model, &root, &options, &output, &error), JINJA_CMETA_ERR_CAPACITY);
      check_null(output);
    }
    options.max_string_bytes = 0u;
    options.max_nodes = 2u;
    check_equal(jinja_test_render("{% set f=range %}{{f(3)|list}}", &model, &root, &options, &output, &error), JINJA_CMETA_ERR_CAPACITY);
    check_null(output);
    check_equal(jinja_test_render("{% for x in [1] recursive %}{{(range)(loop([x]))}}{% endfor %}",
                                 &model, &root, NULL, &output, &error), JINJA_CMETA_ERR_CAPACITY);
    check_null(output);
  }

  it("reports callable capability and formats stable builtin representations") {
    static const struct { const char *source; const char *expected; } cases[] = {
      {"{{range is callable}}|{{dict is callable}}|{{namespace is callable}}|{{missing is callable}}|{{none is callable}}|{{1 is callable}}", "True|True|True|True|False|False"},
      {"{% set r=range(2) %}{{r.count is callable}}|{{r is callable}}|{{namespace() is callable}}|{% for x in [1] %}{{loop is callable}}{% endfor %}", "True|False|False|True"},
      {"{{range|string}}|{{[dict,namespace]}}", "<class 'range'>|[<class 'dict'>, <class 'jinja2.utils.Namespace'>]"},
      {"{% autoescape true %}{{dict}}{% endautoescape %}", "&lt;class &#39;dict&#39;&gt;"}
    };
    JinjaTestRoot root = {{vstr_from_cstr("Ada"), 0}, false, {NULL, 0u, 0u, NULL}};
    JinjaTestModel model;
    jinja_test_model_init(&model);
    for (size_t i = 0u; i < sizeof(cases) / sizeof(cases[0]); ++i) {
      JINJA_CMETA_ERROR error = JINJA_CMETA_ERROR_INIT;
      char *output = NULL;
      info("source: %s", cases[i].source);
      check_equal(jinja_test_render(cases[i].source, &model, &root, NULL, &output, &error), JINJA_CMETA_OK);
      check_equal(output, cases[i].expected);
      free(output);
    }
  }

  it("retains bound range method identity and evaluates computed targets once") {
    static const struct { const char *source; const char *expected; } cases[] = {
      {"{% set r=range(4) %}{% set f=r.count %}{% set g=r.index %}{{f(2)}}|{{g(3)}}|{{r['index'](1)}}", "1|3|1"},
      {"{% set r=range(3) %}{% set s=range(3) %}{{r.count==r.count}}|{{r.count==s.count}}|{{r.index==r.count}}|{{r[:].count==r.count}}", "True|False|False|False"},
      {"{% set r=range(3) %}{% set f=r.count %}{% set r=none %}{{f(1)}}", "1"},
      {"{% set n=namespace(count=range,index=dict) %}{{n.count(2)|join}}|{{n.index(x=1)}}", "01|{'x': 1}"},
      {"{% set it=[3,range]|reverse %}{{(it|first)(it|first)|join}}|{{it|list}}", "012|[]"},
      {"{% set r=range(2) %}{{{r.count:7}[r.count]}}|{{{range:8}[range]}}", "7|8"}
    };
    JinjaTestRoot root = {{vstr_from_cstr("Ada"), 0}, false, {NULL, 0u, 0u, NULL}};
    JinjaTestModel model;
    jinja_test_model_init(&model);
    for (size_t i = 0u; i < sizeof(cases) / sizeof(cases[0]); ++i) {
      JINJA_CMETA_ERROR error = JINJA_CMETA_ERROR_INIT;
      char *output = NULL;
      info("source: %s", cases[i].source);
      check_equal(jinja_test_render(cases[i].source, &model, &root, NULL, &output, &error), JINJA_CMETA_OK);
      check_equal(output, cases[i].expected);
      free(output);
    }
  }

  it("calls builtin identities through aliases containers and computed targets") {
    static const struct { const char *source; const char *expected; } cases[] = {
      {"{% set f=range %}{{f(3)|list}}", "[0, 1, 2]"},
      {"{% set f=dict %}{{f(a=1)}}|{% set n=namespace %}{% set x=n(a=2) %}{{x.a}}", "{'a': 1}|2"},
      {"{% set f=range %}{% set range=none %}{{f(2)|list}}", "[0, 1]"},
      {"{{[range][0](3)|join}}|{{{'f':dict}.f(x=1)}}", "012|{'x': 1}"},
      {"{% set n=namespace(f=range) %}{{n.f(2)|list}}|{{(n).f(1)|list}}", "[0, 1]|[0]"},
      {"{{(range if true else dict)(2)|list}}|{{missing|default(range)(2)|list}}", "[0, 1]|[0, 1]"},
      {"{{range==range}}|{{range!=dict}}|{{range is defined}}|{% if range %}Y{% endif %}", "True|True|True|Y"},
      {"{% for f in [range,range] %}{{f(loop.index)|join}}{% endfor %}", "001"},
      {"{{false and missing()}}|{{true or 1()}}", "False|True"},
      {"{% with range=dict %}{{range(x=2)}}{% endwith %}{{range(2)|join}}", "{'x': 2}01"}
    };
    JinjaTestRoot root = {{vstr_from_cstr("Ada"), 0}, false, {NULL, 0u, 0u, NULL}};
    JinjaTestModel model;
    jinja_test_model_init(&model);
    for (size_t i = 0u; i < sizeof(cases) / sizeof(cases[0]); ++i) {
      JINJA_CMETA_ERROR error = JINJA_CMETA_ERROR_INIT;
      char *output = NULL;
      info("source: %s", cases[i].source);
      check_equal(jinja_test_render(cases[i].source, &model, &root, NULL, &output, &error), JINJA_CMETA_OK);
      check_equal(output, cases[i].expected);
      free(output);
    }
  }
}
