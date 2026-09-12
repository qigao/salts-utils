#include "jinja_cmeta_test_support.h"

spec("Jinja recursive container representations") {
  static JinjaTestModel model;
  static JinjaTestRoot root;
  static JINJA_CMETA_ERROR error;
  static JINJA_CMETA_RENDER_OPTIONS options;
  static JINJA_CMETA_TEMPLATE *templ;
  static char *output;
  before_each() {
    jinja_test_model_init(&model);
    root = (JinjaTestRoot){{vstr_from_cstr("Ada"), 1}, false, {NULL, 0u, 0u, NULL}};
    error = (JINJA_CMETA_ERROR)JINJA_CMETA_ERROR_INIT;
    options = (JINJA_CMETA_RENDER_OPTIONS)JINJA_CMETA_RENDER_OPTIONS_INIT;
    templ = NULL;
    output = NULL;
  }
  after_each() { free(output); jinja_cmeta_release(templ); }

  it("marks a list back edge through a namespace at the original list") {
    check_equal(jinja_test_render("{% set n=namespace() %}{% set x=[n] %}"
        "{% set n.x=x %}{{x}}", &model, &root, NULL, &output, &error), JINJA_CMETA_OK);
    check_equal(output, "[<Namespace {'x': [...]}>]");
  }

  it("marks a tuple back edge without a singleton comma in the recursion marker") {
    check_equal(jinja_test_render("{% set n=namespace() %}{% set x=(n,) %}"
        "{% set n.x=x %}{{x}}", &model, &root, NULL, &output, &error), JINJA_CMETA_OK);
    check_equal(output, "(<Namespace {'x': (...)}>,)");
  }

  it("marks a dictionary back edge through a namespace at the original dictionary") {
    check_equal(jinja_test_render("{% set n=namespace() %}{% set x={'n':n} %}"
        "{% set n.x=x %}{{x}}", &model, &root, NULL, &output, &error), JINJA_CMETA_OK);
    check_equal(output, "{'n': <Namespace {'x': {...}}>}");
  }

  it("repr_cycle_mixed") {
    check_equal(jinja_test_render("{%set n=namespace()%}{%set x={'n':[(n,)]}%}{%set n.x=x%}{{x}}",
        &model, &root, NULL, &output, &error), JINJA_CMETA_OK);
    check_equal(output, "{'n': [(<Namespace {'x': {...}}>,)]}");
  }

  it("repr_cycle_sibling_aliases") {
    check_equal(jinja_test_render("{% set n=namespace() %}{% set x=[n] %}{% set n.x=x %}{{[x,x]}}",
        &model, &root, NULL, &output, &error), JINJA_CMETA_OK);
    check_equal(output, "[[<Namespace {'x': [...]}>], [<Namespace {'x': [...]}>]]");
  }

  it("repr_cycle_namespace_root") {
    check_equal(jinja_test_render("{% set n=namespace() %}{% set x=[n] %}{% set n.x=x %}{{n}}",
        &model, &root, NULL, &output, &error), JINJA_CMETA_OK);
    check_equal(output, "<Namespace {'x': [<Namespace {...}>]}>");
  }

  it("repr_cycle_shallow_list_copy") {
    check_equal(jinja_test_render("{% set n=namespace() %}{% set x=[n] %}{% set n.x=x %}{{x|list}}",
        &model, &root, NULL, &output, &error), JINJA_CMETA_OK);
    check_equal(output, "[<Namespace {'x': [<Namespace {...}>]}>]");
  }

  it("repr_cycle_list_slice") {
    check_equal(jinja_test_render("{% set n=namespace() %}{% set x=[n] %}{% set n.x=x %}{{x[:]}}",
        &model, &root, NULL, &output, &error), JINJA_CMETA_OK);
    check_equal(output, "[<Namespace {'x': [<Namespace {...}>]}>]");
  }

  it("repr_cycle_list_repeat") {
    check_equal(jinja_test_render("{% set n=namespace() %}{% set x=[n] %}{% set n.x=x %}{{x*1}}",
        &model, &root, NULL, &output, &error), JINJA_CMETA_OK);
    check_equal(output, "[<Namespace {'x': [<Namespace {...}>]}>]");
  }

  it("repr_cycle_tuple_slice") {
    check_equal(jinja_test_render("{% set n=namespace() %}{% set x=(n,) %}{% set n.x=x %}{{x[:]}}",
        &model, &root, NULL, &output, &error), JINJA_CMETA_OK);
    check_equal(output, "(<Namespace {'x': (...)}>,)");
  }

  it("repr_cycle_dict_copy") {
    check_equal(jinja_test_render("{% set n=namespace() %}{% set x={'n':n} %}{% set n.x=x %}{{dict(x)}}",
        &model, &root, NULL, &output, &error), JINJA_CMETA_OK);
    check_equal(output, "{'n': <Namespace {'x': {'n': <Namespace {...}>}}>}");
  }

  it("repr_cycle_key_tuple") {
    check_equal(jinja_test_render("{% set n=namespace() %}{% set x=(n,) %}{% set n.x=x %}{{{x:1}}}",
        &model, &root, NULL, &output, &error), JINJA_CMETA_OK);
    check_equal(output, "{(<Namespace {'x': (...)}>,): 1}");
  }

  it("repr_cycle_string") {
    check_equal(jinja_test_render("{% set n=namespace() %}{% set x=[n] %}{% set n.x=x %}{{x|string}}",
        &model, &root, NULL, &output, &error), JINJA_CMETA_OK);
    check_equal(output, "[<Namespace {'x': [...]}>]");
  }

  it("repr_cycle_concat") {
    check_equal(jinja_test_render("{% set n=namespace() %}{% set x=[n] %}{% set n.x=x %}{{'A'~x~'B'}}",
        &model, &root, NULL, &output, &error), JINJA_CMETA_OK);
    check_equal(output, "A[<Namespace {'x': [...]}>]B");
  }

  it("repr_cycle_join") {
    check_equal(jinja_test_render("{% set n=namespace() %}{% set x=[n] %}{% set n.x=x %}{{[x,x]|join('|')}}",
        &model, &root, NULL, &output, &error), JINJA_CMETA_OK);
    check_equal(output, "[<Namespace {'x': [...]}>]|[<Namespace {'x': [...]}>]");
  }

  it("repr_cycle_capture") {
    check_equal(jinja_test_render("{% set n=namespace() %}{% set x=[n] %}{% set n.x=x %}{% set text %}{{x}}{% endset %}{{text}}",
        &model, &root, NULL, &output, &error), JINJA_CMETA_OK);
    check_equal(output, "[<Namespace {'x': [...]}>]");
  }

  it("repr_cycle_macro") {
    check_equal(jinja_test_render("{% set n=namespace() %}{% set x=[n] %}{% set n.x=x %}{% macro f(y) %}{{y}}{% endmacro %}{{f(x)}}",
        &model, &root, NULL, &output, &error), JINJA_CMETA_OK);
    check_equal(output, "[<Namespace {'x': [...]}>]");
  }

  it("repr_cycle_loop") {
    check_equal(jinja_test_render("{% set n=namespace() %}{% set x=[n] %}{% set n.x=x %}{% for y in [x,x] %}{{y}};{% endfor %}",
        &model, &root, NULL, &output, &error), JINJA_CMETA_OK);
    check_equal(output, "[<Namespace {'x': [...]}>];[<Namespace {'x': [...]}>];");
  }

  it("repr_cycle_after_namespace") {
    check_equal(jinja_test_render("{% set n=namespace() %}{% set x=[n] %}{% set n.x=x %}{{n}}|{{x}}",
        &model, &root, NULL, &output, &error), JINJA_CMETA_OK);
    check_equal(output, "<Namespace {'x': [<Namespace {...}>]}>|[<Namespace {'x': [...]}>]");
  }

  it("repr_cycle_after_mutation") {
    check_equal(jinja_test_render("{% set n=namespace() %}{% set x=[n] %}{% set n.x=x %}{{x}}{% set n.x=0 %}|{{x}}",
        &model, &root, NULL, &output, &error), JINJA_CMETA_OK);
    check_equal(output, "[<Namespace {'x': [...]}>]|[<Namespace {'x': 0}>]");
  }

  it("repr_cycle_escape") {
    check_equal(jinja_test_render("{% set n=namespace() %}{% set x=[n] %}{% set n.x=x %}{% autoescape true %}{{x}}|{{x|string}}{% endautoescape %}",
        &model, &root, NULL, &output, &error), JINJA_CMETA_OK);
    check_equal(output, "[&lt;Namespace {&#39;x&#39;: [...]}&gt;]|[&lt;Namespace {&#39;x&#39;: [...]}&gt;]");
  }

  it("repr_cycle_multiple_namespaces") {
    check_equal(jinja_test_render("{% set a=namespace() %}{% set b=namespace() %}{% set x=[a,b] %}{% set a.x=x %}{% set b.x=x %}{{x}}",
        &model, &root, NULL, &output, &error), JINJA_CMETA_OK);
    check_equal(output, "[<Namespace {'x': [...]}>, <Namespace {'x': [...]}>]");
  }

  it("repr_cycle_unshared_equal") {
    check_equal(jinja_test_render("{% set x=[1] %}{% set y=[1] %}{{[x,y,x]}}",
        &model, &root, NULL, &output, &error), JINJA_CMETA_OK);
    check_equal(output, "[[1], [1], [1]]");
  }

  it("repr_cycle_empty_containers") {
    check_equal(jinja_test_render("{{[[],[],(),(),{},{}]}}",
        &model, &root, NULL, &output, &error), JINJA_CMETA_OK);
    check_equal(output, "[[], [], (), (), {}, {}]");
  }

  it("repr_cycle_acyclic_depth_edge") {
    check_equal(jinja_test_render("{% set n=namespace(x=0) %}{% for i in range(63) %}{% set n.x=[n.x] %}{% endfor %}{{n.x}}",
        &model, &root, NULL, &output, &error), JINJA_CMETA_OK);
    check_equal(output, "[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[0]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]");
  }


  it("retains exact final byte limits for a recursive representation") {
    static const char expected[] = "[<Namespace {'x': [...]}>]";
    static const char source[] = "{% set n=namespace() %}{% set x=[n] %}{% set n.x=x %}{{x}}";
    options.max_string_bytes = sizeof(expected) - 2u;
    check_equal(jinja_test_render(source, &model, &root, &options, &output, &error),
        JINJA_CMETA_ERR_CAPACITY);
    check_null(output);
    options.max_string_bytes = sizeof(expected) - 1u;
    check_equal(jinja_test_render(source, &model, &root, &options, &output, &error), JINJA_CMETA_OK);
    check_equal(output, expected);
  }

  it("retains exact temporary string limits for a recursive conversion") {
    static const char expected[] = "[<Namespace {'x': [...]}>]";
    static const char source[] = "{% set n=namespace() %}{% set x=[n] %}{% set n.x=x %}{{x|string}}";
    options.max_string_bytes = sizeof(expected) - 2u;
    check_equal(jinja_test_render(source, &model, &root, &options, &output, &error),
        JINJA_CMETA_ERR_CAPACITY);
    check_null(output);
    options.max_string_bytes = sizeof(expected) - 1u;
    check_equal(jinja_test_render(source, &model, &root, &options, &output, &error), JINJA_CMETA_OK);
    check_equal(output, expected);
  }

  it("propagates a rejected recursion marker and can render the template again") {
    static const char source[] = "{% set n=namespace() %}{% set x=[n] %}{% set n.x=x %}"
        "0123456789{{x}}";
    static const char expected_prefix[] = "0123456789[<Namespace {'x': ";
    JinjaTestByteSink sink = {0};
    JINJA_CMETA_RENDERER renderer = {jinja_test_byte_sink_write};
    templ = jinja_cmeta_compile(vstr_from_cstr(source), NULL, &error);
    check_not_null(templ);
    check_equal(jinja_cmeta_render(templ, &model.root_desc, &root, NULL, &renderer, &sink, &error),
        JINJA_CMETA_ERR_RENDER);
    check_equal(sink.length, sizeof(expected_prefix) - 1u);
    check_equal(sink.bytes, expected_prefix, sink.length);
    check_equal(error.offset, (size_t)(strstr(source, "{{x}}") - source));
    check_equal(jinja_cmeta_render_string(templ, &model.root_desc, &root, NULL, &output, &error),
        JINJA_CMETA_OK);
    check_equal(output, "0123456789[<Namespace {'x': [...]}>]");
  }

  it("does not skip malformed borrowed siblings after a recursion marker") {
    root.user.name = (vstr){NULL, 1u};
    check_equal(jinja_test_render("{% set n=namespace() %}{% set x=[n,user.name] %}"
        "{% set n.x=x %}{{x}}", &model, &root, NULL, &output, &error), JINJA_CMETA_ERR_METADATA);
    check_null(output);
  }

  it("enforces borrowed byte limits after emitting a recursive sibling") {
    static const char source[] = "{% set n=namespace() %}{% set x=[n,user.name] %}"
        "{% set n.x=x %}{{x}}";
    static const char expected_prefix[] = "[<Namespace {'x': [...]}>, ";
    enum { BORROWED_BYTE_LIMIT = 4 };
    root.user.name = vstr_from_cstr("abcdefgh");
    options.max_string_bytes = BORROWED_BYTE_LIMIT;
    JinjaTestByteSink sink = {0};
    JINJA_CMETA_RENDERER renderer = {jinja_test_byte_sink_write};
    templ = jinja_cmeta_compile(vstr_from_cstr(source), NULL, &error);
    check_not_null(templ);
    check_equal(jinja_cmeta_render(templ, &model.root_desc, &root, &options, &renderer, &sink, &error),
        JINJA_CMETA_ERR_CAPACITY);
    check_equal(sink.length, sizeof(expected_prefix) - 1u);
    check_equal(sink.bytes, expected_prefix, sink.length);
    check_equal(error.offset, (size_t)(strstr(source, "{{x}}") - source));
  }

  it("still rejects one level beyond the acyclic representation depth bound") {
    check_equal(jinja_test_render("{% set n=namespace(x=0) %}"
        "{% for i in range(64) %}{% set n.x=[n.x] %}{% endfor %}{{n.x}}",
        &model, &root, NULL, &output, &error), JINJA_CMETA_ERR_CAPACITY);
    check_null(output);
  }

  it("does not reinterpret value comparison budgets as representation budgets") {
    options.max_value_visits = 1u;
    options.max_value_depth = 1u;
    check_equal(jinja_test_render("{{[[1]]}}", &model, &root, &options, &output, &error), JINJA_CMETA_OK);
    check_equal(output, "[[1]]");
  }

  it("permits exactly 64 active container frames across filtered loop repr reentry") {
    check_equal(jinja_test_render("{%set n=namespace(y=0)%}"
        "{%for j in range(32)%}{%set n.y=[n.y]%}{%endfor%}"
        "{%macro pred(i)%}{%if i==2%}{%set n.s=n.y|string%}{%endif%}Y{%endmacro%}"
        "{%for i in [1,2] if pred(i)%}{%set n.x=loop%}"
        "{%for j in range(32)%}{%set n.x=[n.x]%}{%endfor%}"
        "{%set rendered%}{{n.x}}{%endset%}{{rendered|length}};{%endfor%}|{{n.s|length}}",
        &model, &root, NULL, &output, &error), JINJA_CMETA_OK);
    check_equal(output, "81;81;|65");
  }

  it("rejects the 65th reentrant container frame and permits a fresh render at the limit") {
    static const char source[] = "{%set n=namespace(y=0)%}"
        "{%for j in range(user.age)%}{%set n.y=[n.y]%}{%endfor%}"
        "{%macro pred(i)%}{%if i==2%}{%set n.s=n.y|string%}{%endif%}Y{%endmacro%}"
        "{%for i in [1,2] if pred(i)%}{%set n.x=loop%}"
        "{%for j in range(63)%}{%set n.x=[n.x]%}{%endfor%}"
        "{%set rendered%}{{n.x}}{%endset%}{{rendered|length}};{%endfor%}|{{n.s}}";
    templ = jinja_cmeta_compile(vstr_from_cstr(source), NULL, &error);
    check_not_null(templ);
    root.user.age = 2;
    check_equal(jinja_cmeta_render_string(templ, &model.root_desc, &root, NULL, &output, &error),
        JINJA_CMETA_ERR_CAPACITY);
    check_null(output);
    check_equal(error.offset, (size_t)(strstr(source, "{%set n.s=") - source));
    root.user.age = 1;
    check_equal(jinja_cmeta_render_string(templ, &model.root_desc, &root, NULL, &output, &error),
        JINJA_CMETA_OK);
    check_equal(output, "143;143;|[0]");
  }

  it("emits a recursive marker without pushing beyond a full reentrant ancestor chain") {
    check_equal(jinja_test_render("{%set n=namespace()%}"
        "{%macro pred(i)%}{%if i==2%}{%set n.s=[n.x]|string%}{%endif%}Y{%endmacro%}"
        "{%for i in [1,2] if pred(i)%}{%set n.x=loop%}"
        "{%for j in range(63)%}{%set n.x=[n.x]%}{%endfor%}"
        "{%set rendered%}{{n.x}}{%endset%}{{rendered|length}};{%endfor%}|{{n.s}}|{{[1]}}",
        &model, &root, NULL, &output, &error), JINJA_CMETA_OK);
    check_equal(output, "143;143;|[[...]]|[1]");
  }

  it("does not reset the namespace ancestor bound in nested filtered macros") {
    check_equal(jinja_test_render("{%macro pred(k,x)%}{%if k>0 and x==2%}{{f(k-1)}}{%endif%}Y{%endmacro%}"
        "{%macro f(k)%}{%for x in [1,2] if pred(k,x)%}{%set n=namespace(v=loop)%}"
        "{%for j in range(22)%}{%set n.v=namespace(v=n.v)%}{%endfor%}{{n.v}};{%endfor%}"
        "{%endmacro%}{{f(2)}}", &model, &root, NULL, &output, &error), JINJA_CMETA_ERR_CAPACITY);
    check_null(output);
  }

}
