#include "jinja_cmeta_test_support.h"

spec("Jinja reentrant error locations") {
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

  it("locates macro_type_error") {
    static const char source[] = "{%macro m()%}Y{%endmacro%}{{m() + 1}}";
    check_equal(jinja_test_render(source, &model, &root, NULL, &output, &error),
        JINJA_CMETA_ERR_RENDER);
    check_null(output);
    check_equal(error.offset, (size_t)(strstr(source, "{{m()") - source));
  }

  it("locates nested_macro_type_error") {
    static const char source[] = "{%macro h()%}Z{%endmacro%}{%macro m()%}{{h() + 1}}{%endmacro%}{{m()}}";
    check_equal(jinja_test_render(source, &model, &root, NULL, &output, &error),
        JINJA_CMETA_ERR_RENDER);
    check_null(output);
    check_equal(error.offset, (size_t)(strstr(source, "{{h()") - source));
  }

  it("locates eager_second_macro") {
    static const char source[] = "{%macro a()%}A{%endmacro%}{%macro b()%}B{%endmacro%}{{a()/b()}}";
    check_equal(jinja_test_render(source, &model, &root, NULL, &output, &error),
        JINJA_CMETA_ERR_RENDER);
    check_null(output);
    check_equal(error.offset, (size_t)(strstr(source, "{{a()") - source));
  }

  it("locates macro_default") {
    static const char source[] = "{%macro h()%}Z{%endmacro%}{%macro m(a=h())%}Y{%endmacro%}{{m()+1}}";
    check_equal(jinja_test_render(source, &model, &root, NULL, &output, &error),
        JINJA_CMETA_ERR_RENDER);
    check_null(output);
    check_equal(error.offset, (size_t)(strstr(source, "{{m()") - source));
  }

  it("locates unpack_after_macro") {
    static const char source[] = "{%macro m()%}Y{%endmacro%}{%set a,b=m()%}";
    check_equal(jinja_test_render(source, &model, &root, NULL, &output, &error),
        JINJA_CMETA_ERR_RENDER);
    check_null(output);
    check_equal(error.offset, (size_t)(strstr(source, "{%set a,b") - source));
  }

  it("locates if_after_macro") {
    static const char source[] = "{%macro m()%}Y{%endmacro%}{%if m()+1%}X{%endif%}";
    check_equal(jinja_test_render(source, &model, &root, NULL, &output, &error),
        JINJA_CMETA_ERR_RENDER);
    check_null(output);
    check_equal(error.offset, (size_t)(strstr(source, "{%if m()") - source));
  }

  it("locates for_after_macro") {
    static const char source[] = "{%macro m()%}Y{%endmacro%}{%for x in m()+1%}X{%endfor%}";
    check_equal(jinja_test_render(source, &model, &root, NULL, &output, &error),
        JINJA_CMETA_ERR_RENDER);
    check_null(output);
    check_equal(error.offset, (size_t)(strstr(source, "{%for x") - source));
  }

  it("locates print_after_macro") {
    static const char source[] = "{%macro m()%}Y{%endmacro%}P{%print m()+1%}";
    check_equal(jinja_test_render(source, &model, &root, NULL, &output, &error),
        JINJA_CMETA_ERR_RENDER);
    check_null(output);
    check_equal(error.offset, (size_t)(strstr(source, "{%print") - source));
  }

  it("locates call_block") {
    static const char source[] = "{%macro m()%}{{caller() + 1}}{%endmacro%}{%call m()%}Y{%endcall%}";
    check_equal(jinja_test_render(source, &model, &root, NULL, &output, &error),
        JINJA_CMETA_ERR_RENDER);
    check_null(output);
    check_equal(error.offset, (size_t)(strstr(source, "{{caller()") - source));
  }

  it("locates recursive_loop") {
    static const char source[] = "{%for x in [0] recursive%}{%if x==0%}{{loop([1])+1}}{%else%}Y{%endif%}{%endfor%}";
    check_equal(jinja_test_render(source, &model, &root, NULL, &output, &error),
        JINJA_CMETA_ERR_RENDER);
    check_null(output);
    check_equal(error.offset, (size_t)(strstr(source, "{{loop(") - source));
  }

  it("locates signature_after_macro") {
    static const char source[] = "{%macro m()%}Y{%endmacro%}{{range(m())}}";
    check_equal(jinja_test_render(source, &model, &root, NULL, &output, &error),
        JINJA_CMETA_ERR_RENDER);
    check_null(output);
    check_equal(error.offset, (size_t)(strstr(source, "{{range") - source));
  }

  it("locates filter_end_after_macro") {
    static const char source[] = "{%macro m()%}Y{%endmacro%}{%filter center(m())%}X{%endfilter%}";
    check_equal(jinja_test_render(source, &model, &root, NULL, &output, &error),
        JINJA_CMETA_ERR_RENDER);
    check_null(output);
    check_equal(error.offset, (size_t)(strstr(source, "{%endfilter") - source));
  }

  it("locates capture_end_after_macro") {
    static const char source[] = "{%macro m()%}Y{%endmacro%}{%set s|center(m())%}X{%endset%}";
    check_equal(jinja_test_render(source, &model, &root, NULL, &output, &error),
        JINJA_CMETA_ERR_RENDER);
    check_null(output);
    check_equal(error.offset, (size_t)(strstr(source, "{%endset") - source));
  }

  it("locates inner_failure") {
    static const char source[] = "{%macro m()%}{{1/0}}{%endmacro%}{{m()}}";
    check_equal(jinja_test_render(source, &model, &root, NULL, &output, &error),
        JINJA_CMETA_ERR_RENDER);
    check_null(output);
    check_equal(error.offset, (size_t)(strstr(source, "{{1/0") - source));
  }

  it("locates nested_inner_failure") {
    static const char source[] = "{%macro h()%}{{1/0}}{%endmacro%}{%macro m()%}{{h()}}{%endmacro%}{{m()}}";
    check_equal(jinja_test_render(source, &model, &root, NULL, &output, &error),
        JINJA_CMETA_ERR_RENDER);
    check_null(output);
    check_equal(error.offset, (size_t)(strstr(source, "{{1/0") - source));
  }

  it("locates recursive_inner_failure") {
    static const char source[] = "{%for x in [0] recursive%}{%if x==0%}{{loop([1])}}{%else%}{{1/0}}{%endif%}{%endfor%}";
    check_equal(jinja_test_render(source, &model, &root, NULL, &output, &error),
        JINJA_CMETA_ERR_RENDER);
    check_null(output);
    check_equal(error.offset, (size_t)(strstr(source, "{{1/0") - source));
  }

  it("locates caller_inner_failure") {
    static const char source[] = "{%macro m()%}{{caller()}}{%endmacro%}{%call m()%}{{1/0}}{%endcall%}";
    check_equal(jinja_test_render(source, &model, &root, NULL, &output, &error),
        JINJA_CMETA_ERR_RENDER);
    check_null(output);
    check_equal(error.offset, (size_t)(strstr(source, "{{1/0") - source));
  }

  it("locates unicode_prefix") {
    static const char source[] = "甲\r\n{%macro m()%}乙{%endmacro%}前{{m()+1}}";
    check_equal(jinja_test_render(source, &model, &root, NULL, &output, &error),
        JINJA_CMETA_ERR_RENDER);
    check_null(output);
    check_equal(error.offset, (size_t)(strstr(source, "{{m()") - source));
  }

  it("preserves the failing instruction when the final string sink changes the error category") {
    static const char source[] = "{%macro m()%}Y{%endmacro%}P{{m()}}";
    templ = jinja_cmeta_compile(vstr_from_cstr(source), NULL, &error);
    check_not_null(templ);
    options.max_string_bytes = 1u;
    check_equal(jinja_cmeta_render_string(templ, &model.root_desc, &root, &options, &output, &error),
        JINJA_CMETA_ERR_CAPACITY);
    check_null(output);
    check_equal(error.status, JINJA_CMETA_ERR_CAPACITY);
    check_equal(error.offset, (size_t)(strstr(source, "{{m()") - source));
  }

  it("locates final output capacity failure after a discarded comment") {
    static const char source[] = "{#prefix#}AB";
    options.max_string_bytes = 1u;
    check_equal(jinja_test_render(source, &model, &root, &options, &output, &error),
        JINJA_CMETA_ERR_CAPACITY);
    check_null(output);
    check_equal(error.offset, (size_t)(strstr(source, "AB") - source));
  }

  it("allows omitted diagnostics when a nested final string write exceeds its budget") {
    templ = jinja_cmeta_compile(vstr_from_cstr("{%macro m()%}Y{%endmacro%}P{{m()}}"), NULL, &error);
    check_not_null(templ);
    options.max_string_bytes = 1u;
    check_equal(jinja_cmeta_render_string(templ, &model.root_desc, &root, &options, &output, NULL),
        JINJA_CMETA_ERR_CAPACITY);
    check_null(output);
  }

  it("locates a failing renderer at the macro call without rolling back prior output") {
    static const char source[] = "{%macro m()%}Y{%endmacro%}P{{m()}}";
    JinjaTestByteSink sink = {{0}, JINJA_TEST_BYTE_SINK_CAPACITY - 1u};
    const JINJA_CMETA_RENDERER renderer = {jinja_test_byte_sink_write};
    templ = jinja_cmeta_compile(vstr_from_cstr(source), NULL, &error);
    check_not_null(templ);
    check_equal(jinja_cmeta_render(templ, &model.root_desc, &root, NULL, &renderer, &sink, &error),
        JINJA_CMETA_ERR_RENDER);
    check_equal(error.offset, (size_t)(strstr(source, "{{m()") - source));
    check_equal(sink.length, (size_t)JINJA_TEST_BYTE_SINK_CAPACITY);
    check_equal(sink.bytes[sink.length - 1u], (unsigned char)'P');
  }

  it("retains the inner borrowed metadata failure while unwinding a macro") {
    static const char source[] = "{%macro m()%}{{user.name}}{%endmacro%}{{m()}}";
    root.user.name = (vstr){NULL, 1u};
    check_equal(jinja_test_render(source, &model, &root, NULL, &output, &error),
        JINJA_CMETA_ERR_METADATA);
    check_null(output);
    check_equal(error.offset, (size_t)(strstr(source, "{{user.name") - source));
  }

  it("retains the inner workspace failure while unwinding a macro") {
    static const char source[] = "{%macro m()%}{{123|string}}{%endmacro%}{{m()}}";
    options.max_string_bytes = 1u;
    check_equal(jinja_test_render(source, &model, &root, &options, &output, &error),
        JINJA_CMETA_ERR_CAPACITY);
    check_null(output);
    check_equal(error.offset, (size_t)(strstr(source, "{{123") - source));
  }

  it("restores the outer location after filtered loop length invokes a macro") {
    static const char source[] = "P{%macro p(i)%}{%if i==2%}{%set ignored=123|string%}{%endif%}"
        "Y{%endmacro%}{%for i in [1,2] if p(i)%}{{[7,loop]|string}};{%endfor%}";
    JinjaTestByteSink sink = {{0}, 0u};
    const JINJA_CMETA_RENDERER renderer = {jinja_test_byte_sink_write};
    templ = jinja_cmeta_compile(vstr_from_cstr(source), NULL, &error);
    check_not_null(templ);
    options.max_string_bytes = 19u;
    check_equal(jinja_cmeta_render(templ, &model.root_desc, &root, &options, &renderer, &sink, &error),
        JINJA_CMETA_ERR_CAPACITY);
    check_equal(error.offset, (size_t)(strstr(source, "{{[7,loop]") - source));
    check_equal(sink.length, (size_t)1u);
    check_equal(sink.bytes[0], (unsigned char)'P');
    check_equal(jinja_cmeta_render_string(templ, &model.root_desc, &root, &options, &output, &error),
        JINJA_CMETA_ERR_CAPACITY);
    check_null(output);
    check_equal(error.offset, (size_t)(strstr(source, "{{[7,loop]") - source));
  }

  it("retains original UTF8 and CRLF byte positions with custom delimiters") {
    static const char source[] = "甲\r\n<%macro m()%>乙<%endmacro%>前[[m()+1]]";
    JINJA_CMETA_COMPILE_OPTIONS compile = JINJA_CMETA_COMPILE_OPTIONS_INIT;
    compile.variable_start_string = vstr_from_cstr("[[");
    compile.variable_end_string = vstr_from_cstr("]]");
    compile.block_start_string = vstr_from_cstr("<%");
    compile.block_end_string = vstr_from_cstr("%>");
    compile.newline_sequence = vstr_from_cstr("\r");
    templ = jinja_cmeta_compile(vstr_from_cstr(source), &compile, &error);
    check_not_null(templ);
    check_equal(jinja_cmeta_render_string(templ, &model.root_desc, &root, NULL, &output, &error),
        JINJA_CMETA_ERR_RENDER);
    check_null(output);
    check_equal(error.offset, (size_t)(strstr(source, "[[m()") - source));
  }

  it("clears diagnostic state when the same template is rendered again successfully") {
    static const char source[] = "{%macro m()%}Y{%endmacro%}P{{m()+1 if not active else m()}}";
    templ = jinja_cmeta_compile(vstr_from_cstr(source), NULL, &error);
    check_not_null(templ);
    check_equal(jinja_cmeta_render_string(templ, &model.root_desc, &root, NULL, &output, &error),
        JINJA_CMETA_ERR_RENDER);
    check_null(output);
    check_equal(error.offset, (size_t)(strstr(source, "{{m()") - source));
    root.active = true;
    check_equal(jinja_cmeta_render_string(templ, &model.root_desc, &root, NULL, &output, &error),
        JINJA_CMETA_OK);
    check_equal(output, "PY");
    check_equal(error.status, JINJA_CMETA_OK);
    check_equal(error.offset, (size_t)0u);
    check_equal(error.message[0], '\0');
  }

  it("restores diagnostics after a macro breaks out of a captured filter") {
    static const char source[] = "{%macro m()%}{%for x in [1]%}{%filter string%}"
        "A{%break%}{%endfilter%}{%endfor%}Y{%endmacro%}{{m()+1}}";
    check_equal(jinja_test_render(source, &model, &root, NULL, &output, &error),
        JINJA_CMETA_ERR_RENDER);
    check_null(output);
    check_equal(error.offset, (size_t)(strstr(source, "{{m()") - source));
  }

  it("restores diagnostics after a macro continues through captured filters") {
    static const char source[] = "{%macro m()%}{%for x in [1,2]%}{%filter string%}"
        "A{%continue%}{%endfilter%}{%endfor%}Y{%endmacro%}{{m()+1}}";
    check_equal(jinja_test_render(source, &model, &root, NULL, &output, &error),
        JINJA_CMETA_ERR_RENDER);
    check_null(output);
    check_equal(error.offset, (size_t)(strstr(source, "{{m()") - source));
  }
}

spec("Jinja reentrant string construction") {
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

  it("reentrant_string") {
    check_equal(jinja_test_render("{%macro p(i)%}{%if i==2%}{%set ignored=123|string%}{%endif%}Y{%endmacro%}{%for i in [1,2] if p(i)%}{{loop|string}};{%endfor%}",
        &model, &root, NULL, &output, &error), JINJA_CMETA_OK);
    check_equal(output, "<LoopContext 1/2>;<LoopContext 2/2>;");
  }

  it("reentrant_concat") {
    check_equal(jinja_test_render("{%macro p(i)%}{%if i==2%}{%set ignored=123|string%}{%endif%}Y{%endmacro%}{%for i in [1,2] if p(i)%}{{'A'~loop~'B'}};{%endfor%}",
        &model, &root, NULL, &output, &error), JINJA_CMETA_OK);
    check_equal(output, "A<LoopContext 1/2>B;A<LoopContext 2/2>B;");
  }

  it("reentrant_join") {
    check_equal(jinja_test_render("{%macro p(i)%}{%if i==2%}{%set ignored=123|string%}{%endif%}Y{%endmacro%}{%for i in [1,2] if p(i)%}{{[loop,loop]|join('|')}};{%endfor%}",
        &model, &root, NULL, &output, &error), JINJA_CMETA_OK);
    check_equal(output, "<LoopContext 1/2>|<LoopContext 1/2>;<LoopContext 2/2>|<LoopContext 2/2>;");
  }

  it("reentrant_list_repr") {
    check_equal(jinja_test_render("{%macro p(i)%}{%if i==2%}{%set ignored=123|string%}{%endif%}Y{%endmacro%}{%for i in [1,2] if p(i)%}{{[loop]|string}};{%endfor%}",
        &model, &root, NULL, &output, &error), JINJA_CMETA_OK);
    check_equal(output, "[<LoopContext 1/2>];[<LoopContext 2/2>];");
  }

  it("reentrant_tuple_repr") {
    check_equal(jinja_test_render("{%macro p(i)%}{%if i==2%}{%set ignored=123|string%}{%endif%}Y{%endmacro%}{%for i in [1,2] if p(i)%}{{(loop,)|string}};{%endfor%}",
        &model, &root, NULL, &output, &error), JINJA_CMETA_OK);
    check_equal(output, "(<LoopContext 1/2>,);(<LoopContext 2/2>,);");
  }

  it("reentrant_dict_repr") {
    check_equal(jinja_test_render("{%macro p(i)%}{%if i==2%}{%set ignored=123|string%}{%endif%}Y{%endmacro%}{%for i in [1,2] if p(i)%}{{{'l':loop}|string}};{%endfor%}",
        &model, &root, NULL, &output, &error), JINJA_CMETA_OK);
    check_equal(output, "{'l': <LoopContext 1/2>};{'l': <LoopContext 2/2>};");
  }

  it("reentrant_retained_inner") {
    check_equal(jinja_test_render("{%set n=namespace(s='',calls=0)%}{%macro p(i)%}{%set n.calls=n.calls+1%}{%if i==2%}{%set n.s=123|string%}{%endif%}Y{%endmacro%}{%for i in [1,2] if p(i)%}{{'A'~loop~'B'}}:{{n.s}};{%endfor%}|{{n.s}}|{{n.calls}}",
        &model, &root, NULL, &output, &error), JINJA_CMETA_OK);
    check_equal(output, "A<LoopContext 1/2>B:123;A<LoopContext 2/2>B:123;|123|2");
  }

  it("reentrant_inner_concat") {
    check_equal(jinja_test_render("{%set n=namespace(s='',calls=0)%}{%macro p(i)%}{%set n.calls=n.calls+1%}{%if i==2%}{%set n.s='α'~[1,2]%}{%endif%}Y{%endmacro%}{%for i in [1,2] if p(i)%}{{[loop]|string}};{%endfor%}|{{n.s}}|{{n.calls}}",
        &model, &root, NULL, &output, &error), JINJA_CMETA_OK);
    check_equal(output, "[<LoopContext 1/2>];[<LoopContext 2/2>];|α[1, 2]|2");
  }

  it("reentrant_inner_join") {
    check_equal(jinja_test_render("{%set n=namespace(s='',calls=0)%}{%macro p(i)%}{%set n.calls=n.calls+1%}{%if i==2%}{%set n.s=[12,34]|join('-')%}{%endif%}Y{%endmacro%}{%for i in [1,2] if p(i)%}{{loop|string}};{%endfor%}|{{n.s}}",
        &model, &root, NULL, &output, &error), JINJA_CMETA_OK);
    check_equal(output, "<LoopContext 1/2>;<LoopContext 2/2>;|12-34");
  }

  it("reentrant_inner_slice") {
    check_equal(jinja_test_render("{%set n=namespace(s='',calls=0)%}{%macro p(i)%}{%set n.calls=n.calls+1%}{%if i==2%}{%set n.s='é甲🦊'[::-1]%}{%endif%}Y{%endmacro%}{%for i in [1,2] if p(i)%}{{loop|string}};{%endfor%}|{{n.s}}",
        &model, &root, NULL, &output, &error), JINJA_CMETA_OK);
    check_equal(output, "<LoopContext 1/2>;<LoopContext 2/2>;|🦊甲é");
  }

  it("reentrant_inner_escape") {
    check_equal(jinja_test_render("{%set n=namespace(s='',calls=0)%}{%macro p(i)%}{%set n.calls=n.calls+1%}{%if i==2%}{%set n.s='<b>'|escape%}{%endif%}Y{%endmacro%}{%for i in [1,2] if p(i)%}{{loop|string}};{%endfor%}|{{n.s}}",
        &model, &root, NULL, &output, &error), JINJA_CMETA_OK);
    check_equal(output, "<LoopContext 1/2>;<LoopContext 2/2>;|&lt;b&gt;");
  }

  it("reentrant_inner_replace") {
    check_equal(jinja_test_render("{%set n=namespace(s='',calls=0)%}{%macro p(i)%}{%set n.calls=n.calls+1%}{%if i==2%}{%set n.s='abc'|replace('b','XX')%}{%endif%}Y{%endmacro%}{%for i in [1,2] if p(i)%}{{loop|string}};{%endfor%}|{{n.s}}",
        &model, &root, NULL, &output, &error), JINJA_CMETA_OK);
    check_equal(output, "<LoopContext 1/2>;<LoopContext 2/2>;|aXXc");
  }

  it("reentrant_empty_inner") {
    check_equal(jinja_test_render("{%set n=namespace(s='',calls=0)%}{%macro p(i)%}{%set n.calls=n.calls+1%}{%if i==2%}{%set n.s=missing|string%}{%endif%}Y{%endmacro%}{%for i in [1,2] if p(i)%}{{loop|string}};{%endfor%}|{{n.s}}",
        &model, &root, NULL, &output, &error), JINJA_CMETA_OK);
    check_equal(output, "<LoopContext 1/2>;<LoopContext 2/2>;|");
  }

  it("reentrant_unicode") {
    check_equal(jinja_test_render("{%set n=namespace(s='',calls=0)%}{%macro p(i)%}{%set n.calls=n.calls+1%}{%if i==2%}{%set n.s='内\\x00部'~[2]%}{%endif%}Y{%endmacro%}{%for i in [1,2] if p(i)%}{{'甲\\x00'~loop~'🦊'}};{%endfor%}|{{n.s}}",
        &model, &root, NULL, &output, &error), JINJA_CMETA_OK);
    static const char expected[] = "甲\000<LoopContext 1/2>🦊;甲\000<LoopContext 2/2>🦊;|内\000部[2]";
    check_equal(output, expected, sizeof(expected));
  }

  it("reentrant_outer_escape") {
    check_equal(jinja_test_render("{%autoescape true%}{%set n=namespace(s='',calls=0)%}{%macro p(i)%}{%set n.calls=n.calls+1%}{%if i==2%}{%set n.s='<x>'~[1]%}{%endif%}Y{%endmacro%}{%for i in [1,2] if p(i)%}{{loop|string}};{%endfor%}|{{n.s}}{%endautoescape%}",
        &model, &root, NULL, &output, &error), JINJA_CMETA_OK);
    check_equal(output, "&lt;LoopContext 1/2&gt;;&lt;LoopContext 2/2&gt;;|&lt;x&gt;[1]");
  }

  it("reentrant_outer_markup") {
    check_equal(jinja_test_render("{%autoescape true%}{%set n=namespace(s='',calls=0)%}{%macro p(i)%}{%set n.calls=n.calls+1%}{%if i==2%}{%set n.s=123|string%}{%endif%}Y{%endmacro%}{%for i in [1,2] if p(i)%}{{'<b>'|safe~loop}};{%endfor%}|{{n.s}}{%endautoescape%}",
        &model, &root, NULL, &output, &error), JINJA_CMETA_OK);
    check_equal(output, "<b>&lt;LoopContext 1/2&gt;;<b>&lt;LoopContext 2/2&gt;;|123");
  }

  it("reentrant_outer_join_escape") {
    check_equal(jinja_test_render("{%autoescape true%}{%set n=namespace(s='',calls=0)%}{%macro p(i)%}{%set n.calls=n.calls+1%}{%if i==2%}{%set n.s=123|string%}{%endif%}Y{%endmacro%}{%for i in [1,2] if p(i)%}{{[loop,'<x>'|safe]|join('|')}};{%endfor%}|{{n.s}}{%endautoescape%}",
        &model, &root, NULL, &output, &error), JINJA_CMETA_OK);
    check_equal(output, "&lt;LoopContext 1/2&gt;|<x>;&lt;LoopContext 2/2&gt;|<x>;|123");
  }

  it("reentrant_center") {
    check_equal(jinja_test_render("{%set n=namespace(s='',calls=0)%}{%macro p(i)%}{%set n.calls=n.calls+1%}{%if i==2%}{%set n.s=123|string%}{%endif%}Y{%endmacro%}{%for i in [1,2] if p(i)%}{{loop|center(20)}};{%endfor%}|{{n.s}}",
        &model, &root, NULL, &output, &error), JINJA_CMETA_OK);
    check_equal(output, " <LoopContext 1/2>  ; <LoopContext 2/2>  ;|123");
  }

  it("reentrant_replace") {
    check_equal(jinja_test_render("{%set n=namespace(s='',calls=0)%}{%macro p(i)%}{%set n.calls=n.calls+1%}{%if i==2%}{%set n.s=123|string%}{%endif%}Y{%endmacro%}{%for i in [1,2] if p(i)%}{{loop|replace('Loop','X')}};{%endfor%}|{{n.s}}",
        &model, &root, NULL, &output, &error), JINJA_CMETA_OK);
    check_equal(output, "<XContext 1/2>;<XContext 2/2>;|123");
  }

  it("reentrant_nested_macros") {
    check_equal(jinja_test_render("{%set n=namespace(s='')%}{%macro p(k,i)%}{%if k>0 and i==2%}{%set n.s=f(k-1)%}{%endif%}Y{%endmacro%}{%macro f(k)%}{%for i in [1,2] if p(k,i)%}{{'A'~loop~'B'}};{%endfor%}{%endmacro%}{{f(1)}}|{{n.s}}",
        &model, &root, NULL, &output, &error), JINJA_CMETA_OK);
    check_equal(output, "A<LoopContext 1/2>B;A<LoopContext 2/2>B;|A<LoopContext 1/2>B;A<LoopContext 2/2>B;");
  }
  it("charges retained inner strings and pending outer bytes at the exact limit") {
    static const char source[] = "{%set n=namespace(s='')%}{%macro p(i)%}{%if i==2%}{%set n.s=123|string%}{%endif%}Y{%endmacro%}{%for i in [1,2] if p(i)%}{{'AB'~loop}}{%break%}{%endfor%}{{n.s}}";
    templ = jinja_cmeta_compile(vstr_from_cstr(source), NULL, &error);
    check_not_null(templ);
    options.max_string_bytes = 21u;
    check_equal(jinja_cmeta_render_string(templ, &model.root_desc, &root, &options, &output, &error),
        JINJA_CMETA_ERR_CAPACITY);
    check_null(output);
    options.max_string_bytes = 22u;
    check_equal(jinja_cmeta_render_string(templ, &model.root_desc, &root, &options, &output, &error),
        JINJA_CMETA_OK);
    check_equal(output, "AB<LoopContext 1/2>123");
  }

  it("reserves outer pending bytes before inner string conversion") {
    static const char source[] = "P{%set n=namespace(s='')%}{%macro p(i)%}{%if i==2%}{%set n.s=123|string%}{%endif%}Y{%endmacro%}{%for i in [1,2] if p(i)%}{{'AB'~loop}}{%break%}{%endfor%}{{n.s}}";
    JinjaTestByteSink sink = {{0}, 0u};
    const JINJA_CMETA_RENDERER renderer = {jinja_test_byte_sink_write};
    templ = jinja_cmeta_compile(vstr_from_cstr(source), NULL, &error);
    check_not_null(templ);
    options.max_string_bytes = 4u;
    check_equal(jinja_cmeta_render(templ, &model.root_desc, &root, &options, &renderer, &sink, &error),
        JINJA_CMETA_ERR_CAPACITY);
    check_equal(error.offset, (size_t)(strstr(source, "{%set n.s=") - source));
    check_equal(sink.length, (size_t)1u);
    check_equal(sink.bytes[0], (unsigned char)'P');
    check_equal(jinja_cmeta_render_string(templ, &model.root_desc, &root, &options, &output, &error),
        JINJA_CMETA_ERR_CAPACITY);
    check_null(output);
    options.max_string_bytes = 64u;
    check_equal(jinja_cmeta_render_string(templ, &model.root_desc, &root, &options, &output, &error),
        JINJA_CMETA_OK);
    check_equal(output, "PAB<LoopContext 1/2>123");
  }

  it("reserves outer pending bytes before direct inner slicing") {
    static const char source[] = "P{%set n=namespace(s='')%}{%macro p(i)%}{%if i==2%}{%set n.s='cba'[::-1]%}{%endif%}Y{%endmacro%}{%for i in [1,2] if p(i)%}{{'AB'~loop}}{%break%}{%endfor%}{{n.s}}";
    JinjaTestByteSink sink = {{0}, 0u};
    const JINJA_CMETA_RENDERER renderer = {jinja_test_byte_sink_write};
    templ = jinja_cmeta_compile(vstr_from_cstr(source), NULL, &error);
    check_not_null(templ);
    options.max_string_bytes = 4u;
    check_equal(jinja_cmeta_render(templ, &model.root_desc, &root, &options, &renderer, &sink, &error),
        JINJA_CMETA_ERR_CAPACITY);
    check_equal(error.offset, (size_t)(strstr(source, "{%set n.s=") - source));
    check_equal(sink.length, (size_t)1u);
    check_equal(sink.bytes[0], (unsigned char)'P');
    check_equal(jinja_cmeta_render_string(templ, &model.root_desc, &root, &options, &output, &error),
        JINJA_CMETA_ERR_CAPACITY);
    check_null(output);
    options.max_string_bytes = 64u;
    check_equal(jinja_cmeta_render_string(templ, &model.root_desc, &root, &options, &output, &error),
        JINJA_CMETA_OK);
    check_equal(output, "PAB<LoopContext 1/2>abc");
  }

  it("preserves an inner metadata failure without publishing outer construction") {
    static const char source[] =
        "P{%set n=namespace(s='')%}{%macro p(i)%}{%if i==2%}"
        "{%set n.s=[user.name]|string%}{%endif%}Y{%endmacro%}"
        "{%for i in [1,2] if p(i)%}{{'AB'~loop}}{%break%}{%endfor%}{{n.s}}";
    JinjaTestByteSink sink = {{0}, 0u};
    const JINJA_CMETA_RENDERER renderer = {jinja_test_byte_sink_write};
    templ = jinja_cmeta_compile(vstr_from_cstr(source), NULL, &error);
    check_not_null(templ);
    root.user.name = (vstr){NULL, 1u};
    check_equal(jinja_cmeta_render(templ, &model.root_desc, &root, NULL, &renderer, &sink, &error),
        JINJA_CMETA_ERR_METADATA);
    check_equal(error.offset, (size_t)(strstr(source, "{%set n.s=") - source));
    check_equal(sink.length, (size_t)1u);
    check_equal(sink.bytes[0], (unsigned char)'P');
    check_equal(jinja_cmeta_render_string(templ, &model.root_desc, &root, NULL, &output, &error),
        JINJA_CMETA_ERR_METADATA);
    check_null(output);
    root.user.name = vstr_from_cstr("Ada");
    check_equal(jinja_cmeta_render_string(templ, &model.root_desc, &root, NULL, &output, &error),
        JINJA_CMETA_OK);
    check_equal(output, "PAB<LoopContext 1/2>['Ada']");
  }

  it("stops a failed renderer after reentrant construction and permits a new render") {
    static const char source[] = "P{%set n=namespace(s='')%}{%macro p(i)%}{%if i==2%}{%set n.s=123|string%}{%endif%}Y{%endmacro%}{%for i in [1,2] if p(i)%}{{'AB'~loop}}{%break%}{%endfor%}{{n.s}}";
    JinjaTestByteSink sink = {{0}, JINJA_TEST_BYTE_SINK_CAPACITY - 1u};
    const JINJA_CMETA_RENDERER renderer = {jinja_test_byte_sink_write};
    templ = jinja_cmeta_compile(vstr_from_cstr(source), NULL, &error);
    check_not_null(templ);
    check_equal(jinja_cmeta_render(templ, &model.root_desc, &root, NULL, &renderer, &sink, &error),
        JINJA_CMETA_ERR_RENDER);
    check_equal(sink.length, (size_t)JINJA_TEST_BYTE_SINK_CAPACITY);
    check_equal(sink.bytes[sink.length - 1u], (unsigned char)'P');
    check_equal(jinja_cmeta_render_string(templ, &model.root_desc, &root, NULL, &output, &error),
        JINJA_CMETA_OK);
    check_equal(output, "PAB<LoopContext 1/2>123");
  }

  it("does not charge empty conversions against the one byte budget") {
    options.max_string_bytes = 1u;
    check_equal(jinja_test_render("{{missing|string}}{{''~missing}}X",
        &model, &root, &options, &output, &error), JINJA_CMETA_OK);
    check_equal(output, "X");
  }

  it("allows small captured output when the configured budget exceeds string storage limits") {
    options.max_string_bytes = SIZE_MAX;
    check_equal(jinja_test_render("{%macro m()%}x{%endmacro%}{{m()}}",
        &model, &root, &options, &output, &error), JINJA_CMETA_OK);
    check_equal(output, "x");
  }
}
