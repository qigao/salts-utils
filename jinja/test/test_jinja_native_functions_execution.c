#include "test_jinja_native_functions_support.h"
spec("Jinja native function execution") {
  static JINJA_CMETA_TEMPLATE *templ;
  static tstr output;
  static JINJA_CMETA_ERROR error;
  before_each() { templ = NULL; output = tstr_new(); }
  after_each() { jinja_cmeta_release(templ); tstr_free(output); }

  it("formats positional and named values with Unicode fields and markup") {
    static const struct { const char *source, *expected; } cases[] = {
      {"{{'%s:%04d:%.2f:%%'|format('甲',7,2.5)}}", "甲:0007:2.50:%"},
      {"{{'%*.*s'|format(-5,2,'甲😀乙')}}|{{'%(who)s:%(n)#06x'|format(**{'who':'😀','n':31})}}", "甲😀   |😀:0x001f"},
      {"{{'%+d/%#o/%X/%r/%a/%c'|format(-12,8,255,'甲','é',128512)}}", "-12/0o10/FF/'甲'/'\\xe9'/😀"},
      {"{{['%s=%d']|map('format','x',3)|first}}|{{'%.0d/%.0x/%#.0o'|format(0,0,0)}}", "x=3|0/0/0o0"},
      {"{% autoescape true %}{{'<b>%s</b>'|format('<x>')}}|{{'<b>%s</b>'|safe|format('<x>')}}|{{'%s'|safe|format('<i>'|safe)}}{% endautoescape %}", "&lt;b&gt;&lt;x&gt;&lt;/b&gt;|<b>&lt;x&gt;</b>|<i>"}
    };
    for (size_t i = 0u; i < sizeof(cases) / sizeof(cases[0]); ++i) {
      tstr_clear(output);
      info("format source=%s", cases[i].source);
      check_equal(test_native_render(cases[i].source, NULL, &output, &error), JINJA_CMETA_OK);
      check_equal(output, cases[i].expected);
    }
  }

  it("formats strings through modulo without changing numeric remainders") {
    static const struct { const char *source, *expected; } cases[] = {
      {"{{'%s/%d' % ('x',2)}}|{{'%(x)s' % {'x':'甲'}}}|{{'%s' % [1,2]}}", "x/2|甲|[1, 2]"},
      {"{{'%+08.2f/%#.0f/%G' % (2.5,2.0,1000000.0)}}|{{7%3}}|{{7.5%3}}", "+0002.50/2./1E+06|1|1.5"},
      {"{{'plain%%' % []}}|{{'plain%%' % range(3)}}|{{('safe%%'|safe) % []}}", "plain%|plain%|safe%"},
      {"{{('<b>%s</b>'|safe) % '<x>'}}|{{'%s' % {'a':1}}}", "<b>&lt;x&gt;</b>|{'a': 1}"}
    };
    for (size_t i = 0u; i < sizeof(cases) / sizeof(cases[0]); ++i) {
      tstr_clear(output);
      info("percent source=%s", cases[i].source);
      check_equal(test_native_render(cases[i].source, NULL, &output, &error), JINJA_CMETA_OK);
      check_equal(output, cases[i].expected);
    }
  }

  it("reports format argument errors and bounds padded output") {
    static const char *cases[] = {
      "{{'%d'|format()}}", "{{'%s'|format('a',x=1)}}", "{{'%' % ()}}", "{{'%d' % 'x'}}"
    };
    for (size_t i = 0u; i < sizeof(cases) / sizeof(cases[0]); ++i) {
      tstr_clear(output);
      check_equal(test_native_render(cases[i], NULL, &output, &error), JINJA_CMETA_ERR_RENDER);
    }
    JINJA_CMETA_RENDER_OPTIONS options = JINJA_CMETA_RENDER_OPTIONS_INIT;
    options.max_string_bytes = 8u;
    tstr_clear(output);
    check_equal(test_native_render("{{'%8s'|format('x')}}", &options, &output, &error), JINJA_CMETA_OK);
    check_equal(output, "       x");
    options.max_string_bytes = 7u;
    tstr_clear(output);
    check_equal(test_native_render("{{'%8s'|format('x')}}", &options, &output, &error), JINJA_CMETA_ERR_CAPACITY);
  }

  it("cycles retained values across calls loops and bound method aliases") {
    static const struct { const char *source, *expected; } cases[] = {
      {"{% set c=cycler('odd','even') %}{{c.current}}/{{c.next()}}/{{c.current}}/{{c.next()}}/{{c.next()}}/{{c.reset()}}/{{c.current}}", "odd/odd/even/even/odd/None/odd"},
      {"{% set c=cycler(*['甲','😀']) %}{% set n=c.next %}{% for x in [0,1] %}{{n()}}{% endfor %}|{{c['current']}}|{{c.items}}|{{c.pos}}|{{c.next==c.next}}|{{c.next==cycler(1).next}}", "甲😀|甲|('甲', '😀')|0|True|False"},
      {"{% set c=cycler([],{}) %}{{c.next() is sameas(c.items[0])}}|{{c.current is sameas(c.items[1])}}|{{c is callable}}|{{c is iterable}}|{{c.items is sameas(c.items)}}", "True|True|False|False|True"},
      {"{{cycler is callable}}|{{joiner is callable}}|{{cycler}}|{{joiner}}", "True|True|<class 'jinja2.utils.Cycler'>|<class 'jinja2.utils.Joiner'>"}
    };
    for (size_t i = 0u; i < sizeof(cases) / sizeof(cases[0]); ++i) {
      tstr_clear(output);
      info("cycler source=%s", cases[i].source);
      check_equal(test_native_render(cases[i].source, NULL, &output, &error), JINJA_CMETA_OK);
      check_equal(output, cases[i].expected);
    }
  }

  it("shares joiner state while preserving the original separator value") {
    static const struct { const char *source, *expected; } cases[] = {
      {"{% set j=joiner() %}{{j.used}}:{% for x in ['a','b','c'] %}{{j()}}{{x}}{% endfor %}:{{j.used}}:{{j.sep}}", "False:a, b, c:True:, "},
      {"{% set j=joiner(sep=' / ') %}{% set alias=j %}{{j()}}a{{alias()}}b|{{j is sameas(alias)}}|{{j is callable}}|{{j is iterable}}|{{j()==j.sep}}", "a / b|True|True|False|True"},
      {"{% set j=joiner() %}{% set sep=j.sep %}{{j.sep is sameas(sep)}}|{{j()}}|{{j() is sameas(sep)}}|{{joiner().sep is sameas(sep)}}", "True||True|True"},
      {"{% autoescape true %}{% set j=joiner('<br>'|safe) %}{{j()}}a{{j()}}b{% endautoescape %}|{% set v=[] %}{% set j=joiner(v) %}{{j()}}/{{j() is sameas(v)}}", "a<br>b|/True"}
    };
    for (size_t i = 0u; i < sizeof(cases) / sizeof(cases[0]); ++i) {
      tstr_clear(output);
      info("joiner source=%s", cases[i].source);
      check_equal(test_native_render(cases[i].source, NULL, &output, &error), JINJA_CMETA_OK);
      check_equal(output, cases[i].expected);
    }
  }

  it("rejects empty cyclers and invalid helper call arguments") {
    static const char *cases[] = {
      "{{cycler()}}", "{{cycler(items=[1,2])}}", "{{joiner(1,2)}}",
      "{% set j=joiner() %}{{j(1)}}"
    };
    for (size_t i = 0u; i < sizeof(cases) / sizeof(cases[0]); ++i) {
      tstr_clear(output);
      check_equal(test_native_render(cases[i], NULL, &output, &error), JINJA_CMETA_ERR_RENDER);
    }
  }

  it("wraps Unicode paragraphs with word and hyphen policies") {
    static const struct { const char *source, *expected; } cases[] = {
      {"{{'aa bb cc'|wordwrap(4)}}|{{['abcdef','甲😀乙']|map('wordwrap',2,wrapstring='/')|join(';')}}", "aa\nbb\ncc|ab/cd/ef;甲😀/乙"},
      {"{{'  aa   bb\\tcc  '|wordwrap(width=5)}}|{{'one\\n\\ntwo\\n'|wordwrap(4,wrapstring=';')}}", "  aa\nbb\tcc|one;;two"},
      {"{{'hello-world abc--def'|wordwrap(7,false,'/')}}|{{'hello-world'|wordwrap(7,false,'/',false)}}|{{'a-b-c-d-e-f'|wordwrap(3)}}", "hello-/world/abc--/def|hello-world|a-\nb-\nc-\nd-\ne-f"},
      {"{{'甲乙-丙丁 😀abcd'|wordwrap(4)}}|{{'a\\u00a0b c'|wordwrap(3)}}|{{'a\\u2028b\\r\\nc'|wordwrap(5,wrapstring='/')}}", "甲乙-\n丙丁 😀\nabcd|a b\nc|a/b/c"},
      {"{% autoescape true %}{{'<x> <y>'|safe|wordwrap(3)}}|{{'<x> <y>'|wordwrap(3,wrapstring='<br>'|safe)}}{% endautoescape %}", "&lt;x&gt;\n&lt;y&gt;|&lt;x&gt;<br>&lt;y&gt;"},
      {"{{''|wordwrap(0)}}|{{'ab cd'|wordwrap(2.5,break_long_words=false)}}|{{'abc'|wordwrap(0.5)}}", "|ab\ncd|a\nb\nc"}
    };
    for (size_t i = 0u; i < sizeof(cases) / sizeof(cases[0]); ++i) {
      tstr_clear(output);
      info("wordwrap source=%s", cases[i].source);
      check_equal(test_native_render(cases[i].source, NULL, &output, &error), JINJA_CMETA_OK);
      check_equal(output, cases[i].expected);
    }
  }

  it("uses the compiled newline sequence for wordwrap") {
    JINJA_CMETA_COMPILE_OPTIONS options = JINJA_CMETA_COMPILE_OPTIONS_INIT;
    char newline[] = "\r\n";
    options.newline_sequence = vstr_from_cstr(newline);
    templ = jinja_cmeta_compile(vstr_from_cstr("{{'aa bb cc'|wordwrap(4)}}"), &options, &error);
    check_not_null(templ);
    newline[0] = '!';
    const JINJA_CMETA_RENDERER renderer = {test_native_write};
    vstr root = vstr_from_cstr("");
    check_equal(jinja_cmeta_render(templ, jinja_cmeta_vstr_data(), &root, NULL,
        &renderer, &output, &error), JINJA_CMETA_OK);
    check_equal(output, "aa\r\nbb\r\ncc");
  }

  it("reports invalid wordwrap input and nonpositive width") {
    check_equal(test_native_render("{{12|wordwrap}}", NULL, &output, &error), JINJA_CMETA_ERR_RENDER);
    check_equal(test_native_render("{{'abc'|wordwrap(0)}}", NULL, &output, &error), JINJA_CMETA_ERR_RENDER);
  }

  it("renders escaped XML attributes from ordered dictionaries") {
    static const struct { const char *source, *expected; } cases[] = {
      {"{% set ns=namespace(v=0) %}{% macro p(i) %}{% set ns.v=i %}Y{% endmacro %}{% for i in [1,2] if p(i) %}{{ {'x':ns}|xmlattr(autospace=loop) }};{% endfor %}", " x=\"&lt;Namespace {&#39;v&#39;: 1}&gt;\"; x=\"&lt;Namespace {&#39;v&#39;: 2}&gt;\";"},
      {"{{{'id':'a&b','omit':none,'missing':missing,'zero':0,'off':false}|xmlattr}}", " id=\"a&amp;b\" zero=\"0\" off=\"False\""},
      {"{{{'x':'<甲>'}|xmlattr(false)}}|{{{'x':'v'}|xmlattr(autospace=false)}}|{{{}|xmlattr}}|{{{'bad key':none,1:missing}|xmlattr}}", "x=\"&lt;甲&gt;\"|x=\"v\"||"},
      {"{{{'a':'first','b':'second','a':'last'}|xmlattr}}|{{[{'x':'a&b'},{}]|map('xmlattr',false)|join(';')}}", " a=\"last\" b=\"second\"|x=\"a&amp;b\";"},
      {"{% autoescape true %}{{{'x':'a&b'}|xmlattr}}|{{({'x':'a&b'}|xmlattr) is escaped}}{% endautoescape %}|{{({'x':'a&b'}|xmlattr) is escaped}}|{{{'x':'a&b'}|xmlattr|escape}}", " x=\"a&amp;b\"|True|False| x=&#34;a&amp;amp;b&#34;"},
      {"{{{'a<b':'<b>'|safe,'甲':'😀','a b':'v'}|xmlattr}}|{{{'x':'\"\\''}|xmlattr}}", " a&lt;b=\"<b>\" 甲=\"😀\" a b=\"v\"| x=\"&#34;&#39;\""}
    };
    for (size_t i = 0u; i < sizeof(cases) / sizeof(cases[0]); ++i) {
      tstr_clear(output);
      info("xmlattr source=%s", cases[i].source);
      check_equal(test_native_render(cases[i].source, NULL, &output, &error), JINJA_CMETA_OK);
      check_equal(output, cases[i].expected);
    }
  }

  it("rejects invalid XML attribute names and nonmapping input") {
    static const char *cases[] = {
      "{{{'a b':1}|xmlattr}}", "{{{'a/b':1}|xmlattr}}",
      "{{{'a>b':1}|xmlattr}}", "{{{'a=b':1}|xmlattr}}",
      "{{{1:2}|xmlattr}}", "{{none|xmlattr}}"
    };
    for (size_t i = 0u; i < sizeof(cases) / sizeof(cases[0]); ++i) {
      tstr_clear(output);
      info("xmlattr source=%s", cases[i]);
      check_equal(test_native_render(cases[i], NULL, &output, &error), JINJA_CMETA_ERR_RENDER);
    }
  }

  it("counts Unicode word runs after normal string conversion") {
    static const struct { const char *source, *expected; } cases[] = {
      {"{{'Hello, world! don\"t _x １２³'|wordcount}}", "6"},
      {"{{'中文词 😀 Ελληνικά русский'|wordcount}}|{{'e\\u0301lan'|wordcount}}|{{'Ⅳ ½'|wordcount}}", "3|2|2"},
      {"{{missing|wordcount}}|{{none|wordcount}}|{{123.5|wordcount}}|{{['a b','甲😀乙']|map('wordcount')|list}}", "0|1|2|[2, 2]"}
    };
    for (size_t i = 0u; i < sizeof(cases) / sizeof(cases[0]); ++i) {
      tstr_clear(output);
      info("wordcount source=%s", cases[i].source);
      check_equal(test_native_render(cases[i].source, NULL, &output, &error), JINJA_CMETA_OK);
      check_equal(output, cases[i].expected);
    }
  }

  it("formats file sizes with decimal or binary prefixes") {
    static const struct { const char *source, *expected; } cases[] = {
      {"{{0|filesizeformat}}|{{1|filesizeformat}}|{{1.9|filesizeformat}}|{{1000|filesizeformat}}|{{1234567|filesizeformat}}", "0 Bytes|1 Byte|1 Bytes|1.0 kB|1.2 MB"},
      {"{{1023|filesizeformat(true)}}|{{1024|filesizeformat(binary=true)}}|{{1048576|filesizeformat(true)}}", "1023 Bytes|1.0 KiB|1.0 MiB"},
      {"{{'１２３４'|filesizeformat}}|{{(-1024.9)|filesizeformat}}|{{[1000,1000000]|map('filesizeformat')|join(',')}}", "1.2 kB|-1024 Bytes|1.0 kB,1.0 MB"},
      {"{{1e24|filesizeformat}}|{{[9.999999999999999e23,1e24,1.0000000000000001e24]|map('filesizeformat')|join(',')}}", "1000.0 ZB|1000.0 ZB,1000.0 ZB,1.0 YB"}
    };
    for (size_t i = 0u; i < sizeof(cases) / sizeof(cases[0]); ++i) {
      tstr_clear(output);
      info("filesizeformat source=%s", cases[i].source);
      check_equal(test_native_render(cases[i].source, NULL, &output, &error), JINJA_CMETA_OK);
      check_equal(output, cases[i].expected);
    }
  }

  it("encodes URL paths and ordered query pairs using UTF8 bytes") {
    static const struct { const char *source, *expected; } cases[] = {
      {"{{'甲😀/a b+'|urlencode}}|{{'~_.-'|safe|urlencode}}", "%E7%94%B2%F0%9F%98%80/a%20b%2B|~_.-"},
      {"{{{'q':'a/b c','n':2}|urlencode}}|{{[('a','1'),('a','2')]|urlencode}}", "q=a%2Fb+c&n=2|a=1&a=2"},
      {"{{['ab','甲😀']|urlencode}}|{{[['a','b']]|map('list')|urlencode}}", "a=b&%E7%94%B2=%F0%9F%98%80|a=b"},
      {"{{none|urlencode}}|{{true|urlencode}}|{{missing|urlencode}}|{{[]|urlencode}}", "None|True||"},
      {"{% autoescape true %}{{{'a':'<','b':'&'}|urlencode}}{% endautoescape %}|{{['a b','c/d']|map('urlencode')|join(',')}}", "a=%3C&amp;b=%26|a%20b,c/d"}
    };
    for (size_t i = 0u; i < sizeof(cases) / sizeof(cases[0]); ++i) {
      tstr_clear(output);
      info("urlencode source=%s", cases[i].source);
      check_equal(test_native_render(cases[i].source, NULL, &output, &error), JINJA_CMETA_OK);
      check_equal(output, cases[i].expected);
    }
  }

  it("converts integers with bases defaults and decimal text truncation") {
    static const struct { const char *source, *expected; } cases[] = {
      {"{{'42.23'|int}}|{{'-12.9'|int}}|{{true|int}}|{{12.8|int(base=16)}}", "42|-12|1|12"},
      {"{{'0x2a'|int(base=16)}}|{{'101'|int(0,2)}}|{{'z'|int(base=36)}}|{{'010'|int(base=0)}}", "42|5|35|10"},
      {"{{'　１２_３　'|int}}|{{'oops'|int(default='bad')}}|{{none|int(7)}}|{{'12__3'|int(7)}}", "123|bad|7|7"},
      {"{{['12','-3.5','bad']|map('int',9)|join(',')}}|{{'9223372036854775807'|int}}", "12,-3,9|9223372036854775807"}
    };
    for (size_t i = 0u; i < sizeof(cases) / sizeof(cases[0]); ++i) {
      tstr_clear(output);
      info("int source=%s", cases[i].source);
      check_equal(test_native_render(cases[i].source, NULL, &output, &error), JINJA_CMETA_OK);
      check_equal(output, cases[i].expected);
    }
  }

  it("converts floating numbers with Unicode digits and explicit defaults") {
    static const struct { const char *source, *expected; } cases[] = {
      {"{{' １２.５e１ '|float}}|{{'.5'|float}}|{{'1_000.'|float}}|{{true|float}}|{{12|float}}", "125.0|0.5|1000.0|1.0|12.0"},
      {"{{'bad'|float(default='bad')}}|{{none|float(7)}}|{{'0x1p2'|float(9)}}|{{'1__2'|float}}|{{'inf'|float}}", "bad|7|9|0.0|inf"},
      {"{{['1.5','2e2']|map('float')|sum}}|{{'-0'|float}}", "201.5|-0.0"}
    };
    for (size_t i = 0u; i < sizeof(cases) / sizeof(cases[0]); ++i) {
      tstr_clear(output);
      info("float source=%s", cases[i].source);
      check_equal(test_native_render(cases[i].source, NULL, &output, &error), JINJA_CMETA_OK);
      check_equal(output, cases[i].expected);
    }
  }

  it("rounds numeric filters with decimal precision and directed methods") {
    static const struct { const char *source, *expected; } cases[] = {
      {"{{2.5|round}}|{{3.5|round}}|{{2.675|round(2)}}|{{42.55|round}}", "2.0|4.0|2.67|43.0"},
      {"{{42.55|round(1,'floor')}}|{{(-42.55)|round(1,'ceil')}}|{{42.01|round(1,'ceil')}}", "42.5|-42.5|42.1"},
      {"{{150|round(-2)}}|{{250|round(-2)}}|{{5.01|round(-1)}}|{{(-0.1)|round}}|{{1.5|round(none)}}", "200|200|10.0|-0.0|2"},
      {"{{[1.25,2.75]|map('round',precision=1)|join(',')}}|{{42|round}}|{{42|round(1,'floor')}}", "1.2,2.8|42|42.0"}
    };
    for (size_t i = 0u; i < sizeof(cases) / sizeof(cases[0]); ++i) {
      tstr_clear(output);
      info("round source=%s", cases[i].source);
      check_equal(test_native_render(cases[i].source, NULL, &output, &error), JINJA_CMETA_OK);
      check_equal(output, cases[i].expected);
    }
  }

  it("maps attributes and existing filters through lazy chains") {
    static const struct { const char *source, *expected; } cases[] = {
      {"{{[-2,3]|map('abs')|join(',')}}", "2,3"},
      {"{{[{'user':{'name':'甲'}},{'user':{}}]|map(attribute='user.name',default='无')|join(',')}}", "甲,无"},
      {"{{[['甲'],['乙']]|map(attribute='0')|join(',')}}", "甲,乙"},
      {"{{[' a ',' b ']|map('trim',chars=' ')|map('replace','a','A')|join(',')}}", "A,b"},
      {"{{[[-1,2],[-3]]|map('map','abs')|map('join',':')|join(';')}}", "1:2;3"},
      {"{{[{}]|map(attribute='user.name',default={'name':'无'})|join(',')}}", "无"}
    };
    for (size_t i = 0u; i < sizeof(cases) / sizeof(cases[0]); ++i) {
      tstr_clear(output);
      info("transform source=%s", cases[i].source);
      check_equal(test_native_render(cases[i].source, NULL, &output, &error), JINJA_CMETA_OK);
      check_equal(output, cases[i].expected);
    }
  }

  it("selects or rejects original items using truth tests and attributes") {
    static const struct { const char *source, *expected; } cases[] = {
      {"{{[0,1,'',2]|select|list}}|{{[0,1,'',2]|reject|list}}", "[1, 2]|[0, '']"},
      {"{{range(6)|select('odd')|reject('equalto',3)|join(',')}}", "1,5"},
      {"{% set users=[{'name':'甲','active':true},{'name':'乙','active':false}] %}{{users|selectattr('active')|map(attribute='name')|join(',')}}|{{users|rejectattr('active')|map(attribute='name')|join(',')}}", "甲|乙"},
      {"{{[{'n':1},{'n':3}]|selectattr('n','ge',2)|map(attribute='n')|list}}|{{range(5)|select('divisibleby',num=2)|join(',')}}", "[3]|0,2,4"},
      {"{{'甲😀'|map('string')|join('/')}}|{{{'a':1,'b':2}|reject('equalto','a')|join}}", "甲/😀|b"}
    };
    for (size_t i = 0u; i < sizeof(cases) / sizeof(cases[0]); ++i) {
      tstr_clear(output);
      info("transform source=%s", cases[i].source);
      check_equal(test_native_render(cases[i].source, NULL, &output, &error), JINJA_CMETA_OK);
      check_equal(output, cases[i].expected);
    }
  }

  it("retains lazy filter arguments and shares consumption between aliases") {
    static const struct { const char *source, *expected; } cases[] = {
      {"{% set g=range(3)|map('abs') %}{% set alias=g %}{{g|first}}|{{alias|join(',')}}|{{g|list}}", "0|1,2|[]"},
      {"{% for x in range(5)|select('odd') %}{{loop.index}}:{{x}}/{{loop.length}};{% endfor %}", "1:1/2;2:3/2;"},
      {"{% set n='a' %}{% set g=['aa']|map('replace',n,'x') %}{% set n='b' %}{{g|join}}", "xx"},
      {"{{none|map|list}}|{{[]|selectattr|list}}|{{[]|map('missing')|list}}", "[]|[]|[]"}
    };
    for (size_t i = 0u; i < sizeof(cases) / sizeof(cases[0]); ++i) {
      tstr_clear(output);
      info("transform source=%s", cases[i].source);
      check_equal(test_native_render(cases[i].source, NULL, &output, &error), JINJA_CMETA_OK);
      check_equal(output, cases[i].expected);
    }
  }

  it("indents lines with numeric or string prefixes and Jinja markup semantics") {
    static const struct { const char *source, *expected; } cases[] = {
      {"{{'one\\n\\nthree\\n'|indent}}", "one\n\n    three\n"},
      {"{{'甲\\n\\n乙\\n'|indent(width='> ',first=true,blank=true)}}", "> 甲\n> \n> 乙\n> "},
      {"{{'a\\r\\nb\\rc\\u0085d\\u2028e\\u2029f\\vG\\fH\\x1cI\\x1dJ\\x1eK\\x1fL'|indent(1)}}",
          "a\n b\n c\n d\n e\n f\n G\n H\n I\n J\n K\x1fL"},
      {"{{'x\\r'|indent}}|{{''|indent(2,true)}}|{{'a\\nb'|indent(-2)}}", "x|  |a\nb"},
      {"{% autoescape true %}{{'<a>\\n<b>'|safe|indent('<i>',true)}}{% endautoescape %}", "<i><a>\n<i><b>"},
      {"{{'<a>\\n<b>'|indent('<i>'|safe)}}|{{'<a>\\n<b>'|indent('<i>'|safe,true)}}|"
          "{{'<a>\\n<b>'|indent('<i>'|safe,false,true)}}",
          "<a>\n<i>&lt;b&gt;|<i>&lt;a&gt;\n&lt;i&gt;&amp;lt;b&amp;gt;|&lt;a&gt;\n<i>&lt;b&gt;"}
    };
    for (size_t i = 0u; i < sizeof(cases) / sizeof(cases[0]); ++i) {
      tstr_clear(output);
      info("indent source=%s", cases[i].source);
      check_equal(test_native_render(cases[i].source, NULL, &output, &error), JINJA_CMETA_OK);
      check_equal(output, cases[i].expected);
    }
  }

  it("truncates Unicode text with word boundaries leeway and safe suffixes") {
    static const struct { const char *source, *expected; } cases[] = {
      {"{{'foo bar baz qux'|truncate(9)}}|{{'foo bar baz qux'|truncate(9,true)}}|"
          "{{'foo bar baz qux'|truncate(11)}}", "foo...|foo ba...|foo bar baz qux"},
      {"{{'foo bar baz qux'|truncate(length=11,killwords=false,end='...',leeway=0)}}", "foo bar..."},
      {"{{'甲😀乙丙丁'|truncate(3,true,'…',0)}}|{{'abcdefgh'|truncate(5,false,'..',0)}}", "甲😀…|abc.."},
      {"{% autoescape true %}{{'<abcdef>'|safe|truncate(4,true,'<&',0)}}|"
          "{{'<abcdef>'|truncate(4,true,'<&'|safe,0)}}{% endautoescape %}", "<a&lt;&amp;|&lt;a<&"},
      {"{{missing|truncate is undefined}}|{{'abc'|truncate(3,end='',leeway=none)}}|"
          "{{'a b c d'|truncate(*[4,true],**{'end':'!','leeway':0})}}", "True|abc|a b!"}
    };
    for (size_t i = 0u; i < sizeof(cases) / sizeof(cases[0]); ++i) {
      tstr_clear(output);
      info("truncate source=%s", cases[i].source);
      check_equal(test_native_render(cases[i].source, NULL, &output, &error), JINJA_CMETA_OK);
      check_equal(output, cases[i].expected);
    }
  }

  it("rejects invalid text formatting parameters and bounds indentation output") {
    static const char *sources[] = {
      "{{'a'|indent(1.5)}}", "{{'abc'|truncate(2)}}", "{{'abc'|truncate(leeway=-1)}}"
    };
    for (size_t i = 0u; i < sizeof(sources) / sizeof(sources[0]); ++i)
      check_equal(test_native_render(sources[i], NULL, &output, &error), JINJA_CMETA_ERR_RENDER);
    JINJA_CMETA_RENDER_OPTIONS options = JINJA_CMETA_RENDER_OPTIONS_INIT;
    options.max_string_bytes = 5u;
    check_equal(test_native_render("{{'a\\nb'|indent(2)}}", &options, &output, &error), JINJA_CMETA_OK);
    check_equal(output, "a\n  b");
    tstr_clear(output);
    options.max_string_bytes = 4u;
    check_equal(test_native_render("{{'a\\nb'|indent(2)}}", &options, &output, &error), JINJA_CMETA_ERR_CAPACITY);
  }

  it("renders named blocks with isolated or explicitly scoped context") {
    static const struct { const char *source, *expected; } cases[] = {
      {"{% set x='root' %}{% block a %}{{x}}{% set x='local' %}{{x}}{% endblock %}/{{x}}", "rootlocal/root"},
      {"{% for x in [1,2] %}{% block a %}[{{x}}]{% endblock %}{% endfor %}", "[][]"},
      {"{% for x in [1,2] %}{% block a scoped %}{{x}}{% endblock %}{% endfor %}", "12"},
      {"{% block a %}A{% block b %}B{% endblock b %}C{% endblock a %}", "ABC"},
      {"{% macro f(x) %}{% block a scoped %}{{x}}{% endblock %}{% endmacro %}{{f('M')}}", "M"}
    };
    for (size_t i = 0u; i < sizeof(cases) / sizeof(cases[0]); ++i) {
      tstr_clear(output);
      info("block source=%s", cases[i].source);
      check_equal(test_native_render(cases[i].source, NULL, &output, &error), JINJA_CMETA_OK);
      check_equal(output, cases[i].expected);
    }
  }

  it("calls named blocks through self independently of declaration order") {
    static const struct { const char *source, *expected; } cases[] = {
      {"{% block a %}{{self.b()}}{% endblock %}{% block b %}B{% endblock %}", "BB"},
      {"{% if false %}{% block a %}A{% endblock %}{% endif %}{{self['a']()}}", "A"},
      {"{% block a %}{{super is undefined}}{% endblock %}|{{self.a.super is undefined}}", "True|True"},
      {"{% autoescape true %}{% block a %}{{'<x>'}}{% endblock %}|{{self.a()}}{% endautoescape %}", "<x>|<x>"}
    };
    for (size_t i = 0u; i < sizeof(cases) / sizeof(cases[0]); ++i) {
      tstr_clear(output);
      info("block source=%s", cases[i].source);
      check_equal(test_native_render(cases[i].source, NULL, &output, &error), JINJA_CMETA_OK);
      check_equal(output, cases[i].expected);
    }
  }

  it("fails when an executed required block has no override") {
    check_equal(test_native_render("before{% block body required %} {% endblock %}",
        NULL, &output, &error), JINJA_CMETA_ERR_RENDER);
    check_equal(output, "before");
  }

  it("preserves template reference values and distinct block callable identities") {
    static const struct { const char *source, *expected; } cases[] = {
      {"{{self}}|{{[self]}}|{{self == self}}|{{self is callable}}",
          "<TemplateReference None>|[<TemplateReference None>]|True|False"},
      {"{% block a %}A{% endblock %}{% set f=self.a %}{{f == f}}|{{self.a == self.a}}|{{self.a.name}}",
          "ATrue|False|a"},
      {"{% set n=namespace(ref=None) %}{% for x in [1,2] %}{% block a scoped %}{% set n.ref=self %}"
          "{% endblock %}{% endfor %}{% block b %}{{x}}{% endblock %}[{{n.ref.b()}}]", "[2]"},
      {"{% set n=namespace(f=None) %}{% for x in [1,2] %}{% block a scoped %}{% macro f() %}{{x}}"
          "{% endmacro %}{% set n.f=f %}{% endblock %}{% endfor %}{{n.f()}}", "2"}
    };
    for (size_t i = 0u; i < sizeof(cases) / sizeof(cases[0]); ++i) {
      tstr_clear(output);
      info("reference source=%s", cases[i].source);
      check_equal(test_native_render(cases[i].source, NULL, &output, &error), JINJA_CMETA_OK);
      check_equal(output, cases[i].expected);
    }
  }

  it("retains scoped block context in loop metadata and recursive loop calls") {
    static const struct { const char *source, *expected; } cases[] = {
      {"{% for x in [1,2] %}{% block a scoped %}{{loop.index}}{% endblock %}{% endfor %}", "12"},
      {"{% for x in [1,2] %}{% macro f() %}{% block a scoped %}{{loop.index}}{% endblock %}"
          "{% endmacro %}{{f()}}{% endfor %}", "12"},
      {"{% set ns=namespace() %}{% with x='A' %}{% block a scoped %}{% for y in [1] recursive %}"
          "{% set ns.rec=loop %}{% block b %}{{x}}{% endblock %}{% endfor %}{% endblock %}"
          "{% endwith %}|{{ns.rec([2])}}", "A|A"},
      {"{% set ns=namespace() %}{% with x=2 %}{% block a scoped %}{% for y in [1,2,3] if y<=x %}"
          "{% set ns.l=loop %}{% break %}{% endfor %}{% endblock %}{% endwith %}{{ns.l.length}}", "2"}
    };
    for (size_t i = 0u; i < sizeof(cases) / sizeof(cases[0]); ++i) {
      tstr_clear(output);
      info("scoped loop source=%s", cases[i].source);
      check_equal(test_native_render(cases[i].source, NULL, &output, &error), JINJA_CMETA_OK);
      check_equal(output, cases[i].expected);
    }
  }

  it("admits comment separated text beyond the initial whole tree workspace") {
    enum { SEGMENTS = 300 };
    static const char segment[] = "a{#c#}";
    char source[SEGMENTS * (sizeof(segment) - 1u) + 1u];
    for (size_t i = 0u; i < SEGMENTS; ++i)
      memcpy(source + i * (sizeof(segment) - 1u), segment, sizeof(segment) - 1u);
    source[sizeof(source) - 1u] = '\0';
    JINJA_CMETA_TEMPLATE *public_template = jinja_cmeta_compile(vstr_from_cstr(source), NULL, &error);
    int public_admitted = public_template != NULL;
    jinja_cmeta_release(public_template);
    check_equal(public_admitted, 1);
    check_equal(test_native_render(source, NULL, &output, &error), JINJA_CMETA_OK);
    check_equal(tstr_len(output), (size_t)SEGMENTS);
    for (size_t i = 0u; i < tstr_len(output); ++i) check_equal(output[i], 'a');
  }

  it("compiles a macro after a large comment separated prefix") {
    enum { SEGMENTS = 300 };
    static const char segment[] = "a{#c#}";
    static const char tail[] = "{% macro f(v=7) %}{{v}}{% endmacro %}{{f()}}";
    char source[SEGMENTS * (sizeof(segment) - 1u) + sizeof(tail)];
    for (size_t i = 0u; i < SEGMENTS; ++i)
      memcpy(source + i * (sizeof(segment) - 1u), segment, sizeof(segment) - 1u);
    memcpy(source + SEGMENTS * (sizeof(segment) - 1u), tail, sizeof(tail));
    check_equal(test_native_render(source, NULL, &output, &error), JINJA_CMETA_OK);
    check_equal(tstr_len(output), (size_t)SEGMENTS + 1u);
    if (tstr_len(output) == (size_t)SEGMENTS + 1u) check_equal(output[SEGMENTS], '7');
  }

  it("preserves captured missing raw") {
    check_equal(test_native_render("{% set ns=namespace() %}{% for x in [1] %}{% macro f() %}{{x}}|{{x is defined}}|{{x is undefined}}|{{x|default('D')}}|{{x|default('D',true)}}|{{not x}}{% endmacro %}{% set ns.f=f %}{% endfor %}{{ns.f()}}", NULL, &output, &error), JINJA_CMETA_OK);
    check_equal(output, "missing|True|False|missing|missing|False");
  }

  it("preserves captured missing types") {
    check_equal(test_native_render("{% set ns=namespace() %}{% for x in [1] %}{% macro f() %}{{x is number}}|{{x is string}}|{{x is iterable}}|{{x is sequence}}|{{x is mapping}}|{{x is callable}}|{{x is none}}{% endmacro %}{% set ns.f=f %}{% endfor %}{{ns.f()}}", NULL, &output, &error), JINJA_CMETA_OK);
    check_equal(output, "False|False|False|False|False|False|False");
  }

  it("preserves captured missing repr") {
    check_equal(test_native_render("{% set ns=namespace() %}{% for x in [1] %}{% macro f() %}{{x|string}}|{{[x]}}|{{{'k':x}}}|{{''~x}}{% endmacro %}{% set ns.f=f %}{% endfor %}{{ns.f()}}", NULL, &output, &error), JINJA_CMETA_OK);
    check_equal(output, "missing|[missing]|{'k': missing}|missing");
  }

  it("preserves captured missing identity") {
    check_equal(test_native_render("{% set ns=namespace() %}{% for x in [1] %}{% macro f() %}{{x==x}}|{{x==none}}|{{x==missing}}|{{x in [x]}}|{{{x:'v'}[x]}}{% endmacro %}{% set ns.f=f %}{% endfor %}{{ns.f()}}", NULL, &output, &error), JINJA_CMETA_OK);
    check_equal(output, "True|False|False|True|v");
  }

  it("preserves captured missing assignment") {
    check_equal(test_native_render("{% set ns=namespace() %}{% for x in [1] %}{% macro f() %}{% set y=x %}{{y is undefined}}|{{y}}|{{[x][0]}}{% endmacro %}{% set ns.f=f %}{% endfor %}{{ns.f()}}", NULL, &output, &error), JINJA_CMETA_OK);
    check_equal(output, "True||missing");
  }

  it("preserves captured missing lookup") {
    check_equal(test_native_render("{% set ns=namespace() %}{% for x in [1] %}{% macro f() %}{{x.absent is undefined}}|{{x['absent'] is undefined}}|{{x[0] is undefined}}{% endmacro %}{% set ns.f=f %}{% endfor %}{{ns.f()}}", NULL, &output, &error), JINJA_CMETA_OK);
    check_equal(output, "True|True|True");
  }

  it("preserves captured missing macro_arg") {
    check_equal(test_native_render("{% set ns=namespace() %}{% for x in [1] %}{% macro f() %}{% macro g(a=9) %}{{a}}{% endmacro %}{{g(x)}}|{{g(*[x])}}|{{g(**{'a':x})}}{% endmacro %}{% set ns.f=f %}{% endfor %}{{ns.f()}}", NULL, &output, &error), JINJA_CMETA_OK);
    check_equal(output, "9|9|9");
  }

  it("preserves captured missing iteration") {
    check_equal(test_native_render("{% set ns=namespace() %}{% for x in [1] %}{% macro f() %}{% for y in [x] %}{{y}}:{{loop.previtem is undefined}}{% endfor %}{% endmacro %}{% set ns.f=f %}{% endfor %}{{ns.f()}}", NULL, &output, &error), JINJA_CMETA_OK);
    check_equal(output, "missing:True");
  }

  it("preserves captured missing length_error") {
    check_equal(test_native_render("{% set ns=namespace() %}{% for x in [1] %}{% macro f() %}{{x|length}}{% endmacro %}{% set ns.f=f %}{% endfor %}{{ns.f()}}", NULL, &output, &error), JINJA_CMETA_ERR_RENDER);
    check_equal(output, "");
  }

  it("preserves captured missing iter_error") {
    check_equal(test_native_render("{% set ns=namespace() %}{% for x in [1] %}{% macro f() %}{{x|list}}{% endmacro %}{% set ns.f=f %}{% endfor %}{{ns.f()}}", NULL, &output, &error), JINJA_CMETA_ERR_RENDER);
    check_equal(output, "");
  }

  it("preserves captured missing order_error") {
    check_equal(test_native_render("{% set ns=namespace() %}{% for x in [1] %}{% macro f() %}{{x<1}}{% endmacro %}{% set ns.f=f %}{% endfor %}{{ns.f()}}", NULL, &output, &error), JINJA_CMETA_ERR_RENDER);
    check_equal(output, "");
  }

  it("preserves missing propagation defaults") {
    check_equal(test_native_render("{% set ns=namespace() %}{% for x in [1] %}{% macro f() %}{% macro g(a=x,b=a,c=d,d=2) %}{{a}}|{{b}}|{{c is undefined}}|{{d}}{% endmacro %}{{g()}}{% endmacro %}{% set ns.f=f %}{% endfor %}{{ns.f()}}", NULL, &output, &error), JINJA_CMETA_OK);
    check_equal(output, "missing|missing|True|2");
  }

  it("preserves missing propagation neighbor") {
    check_equal(test_native_render("{% set ns=namespace() %}{% for x in [1] %}{% macro f() %}{% for y in [x,7] %}{{y}}:{{loop.previtem is undefined}}:{{loop.nextitem is undefined}}:{{loop.last}};{% endfor %}{% endmacro %}{% set ns.f=f %}{% endfor %}{{ns.f()}}", NULL, &output, &error), JINJA_CMETA_OK);
    check_equal(output, "missing:True:False:False;7:False:True:True;");
  }

  it("preserves missing propagation peek_missing") {
    check_equal(test_native_render("{% set ns=namespace() %}{% for x in [1] %}{% macro f() %}{% for y in [1,x,3] %}{{y}}:{{loop.nextitem is undefined}}:{{loop.last}};{% endfor %}{% endmacro %}{% set ns.f=f %}{% endfor %}{{ns.f()}}", NULL, &output, &error), JINJA_CMETA_OK);
    check_equal(output, "1:True:False;3:True:True;");
  }

  it("preserves missing propagation first_missing") {
    check_equal(test_native_render("{% set ns=namespace() %}{% for x in [1] %}{% macro f() %}{{[x]|first is undefined}}|{{[x]|last is undefined}}{% endmacro %}{% set ns.f=f %}{% endfor %}{{ns.f()}}", NULL, &output, &error), JINJA_CMETA_OK);
    check_equal(output, "False|False");
  }

  it("preserves missing propagation namespace") {
    check_equal(test_native_render("{% set ns=namespace() %}{% for x in [1] %}{% macro f() %}{% set n=namespace(v=x) %}{{n.v}}|{% set z=n.v %}{{z is undefined}}{% endmacro %}{% set ns.f=f %}{% endfor %}{{ns.f()}}", NULL, &output, &error), JINJA_CMETA_OK);
    check_equal(output, "missing|True");
  }

  it("preserves missing propagation guard_alias") {
    check_equal(test_native_render("{% set ns=namespace() %}{% for x in [1] %}{% macro f() %}{% with y=x %}{{y}}|{{y is undefined}}{% endwith %}{% endmacro %}{% set ns.f=f %}{% endfor %}{{ns.f()}}", NULL, &output, &error), JINJA_CMETA_OK);
    check_equal(output, "missing|False");
  }

  it("preserves missing propagation missing_loop_length") {
    check_equal(test_native_render("{% set ns=namespace() %}{% for x in [1] %}{% macro f() %}{% for y in [x] %}{{loop.length}}:{{loop.last}}:{{loop.previtem is undefined}}{% endfor %}{% endmacro %}{% set ns.f=f %}{% endfor %}{{ns.f()}}", NULL, &output, &error), JINJA_CMETA_OK);
    check_equal(output, "1:True:True");
  }

  it("preserves missing propagation recursive_default_context") {
    check_equal(test_native_render("{% set ns=namespace() %}{% for x in [1] %}{% macro f() %}{% macro g(a=ns.loop([2]) if ns.loop is defined else '', b=x) %}{{a}}{% for y in [1] recursive %}{{b is undefined}};{% set ns.loop=loop %}{% endfor %}{% endmacro %}{{g()}}|{{g()}}{% endmacro %}{% set ns.f=f %}{% endfor %}{{ns.f()}}", NULL, &output, &error), JINJA_CMETA_OK);
    check_equal(output, "False;|False;False;");
  }

  it("observes sibling loop rebinding after reading captured missing") {
    check_equal(test_native_render("{% set ns=namespace() %}{% for x in [1] %}{% macro f() %}{{x}}{% endmacro %}{% set ns.f=f %}{% endfor %}{{ns.f()}}|{% for x in [9] %}{{ns.f()}}{% endfor %}", NULL, &output, &error), JINJA_CMETA_OK);
    check_equal(output, "missing|9");
  }

  it("bounds captured missing representation bytes") {
    JINJA_CMETA_RENDER_OPTIONS options = JINJA_CMETA_RENDER_OPTIONS_INIT;
    const char *source = "{% set ns=namespace() %}{% for x in [1] %}{% macro f() %}{{x}}{% endmacro %}{% set ns.f=f %}{% endfor %}{{ns.f()}}";
    options.max_string_bytes = sizeof("missing") - 2u;
    check_equal(test_native_render(source, &options, &output, &error), JINJA_CMETA_ERR_CAPACITY);
    check_equal(tstr_len(output), (size_t)0u);
    ++options.max_string_bytes;
    check_equal(test_native_render(source, &options, &output, &error), JINJA_CMETA_OK);
    check_equal(output, "missing");
  }

  it("separates missing lookahead logical_index") {
    check_equal(test_native_render("{% set ns=namespace() %}{% for x in [1] %}{% macro f() %}{% for y in [1,x,3] %}{{loop.index}}:{{y}}:{{loop.nextitem is undefined}}:{{loop.previtem|default('-')}};{% endfor %}{% endmacro %}{% set ns.f=f %}{% endfor %}{{ns.f()}}", NULL, &output, &error), JINJA_CMETA_OK);
    check_equal(output, "1:1:True:-;2:3:True:1;");
  }

  it("separates missing lookahead repeated_peek") {
    check_equal(test_native_render("{% set ns=namespace() %}{% for x in [1] %}{% macro f() %}{% for y in [1,x,x,4] %}{{y}}:{{loop.nextitem is undefined}}:{{loop.nextitem is undefined}}:{{loop.last}};{% endfor %}{% endmacro %}{% set ns.f=f %}{% endfor %}{{ns.f()}}", NULL, &output, &error), JINJA_CMETA_OK);
    check_equal(output, "1:True:True:False;4:True:True:True;");
  }

  it("separates missing lookahead last_discards") {
    check_equal(test_native_render("{% set ns=namespace() %}{% for x in [1] %}{% macro f() %}{% for y in [1,x,3] %}{{y}}:{{loop.last}};{% endfor %}{% endmacro %}{% set ns.f=f %}{% endfor %}{{ns.f()}}", NULL, &output, &error), JINJA_CMETA_OK);
    check_equal(output, "1:True;3:True;");
  }

  it("separates missing lookahead length_sized") {
    check_equal(test_native_render("{% set ns=namespace() %}{% for x in [1] %}{% macro f() %}{% for y in [1,x,3] %}{{loop.nextitem is undefined}}:{{loop.length}}:{{loop.index}}:{{loop.revindex}}:{{loop.last}};{% endfor %}{% endmacro %}{% set ns.f=f %}{% endfor %}{{ns.f()}}", NULL, &output, &error), JINJA_CMETA_OK);
    check_equal(output, "True:3:1:3:False;True:3:2:2:True;");
  }

  it("separates missing lookahead length_unsized_before") {
    check_equal(test_native_render("{% set ns=namespace() %}{% for x in [1] %}{% macro f() %}{% for y in [1,x,3] if true %}{{loop.length}}:{{loop.nextitem is undefined}}:{{loop.index}}:{{loop.revindex}};{% endfor %}{% endmacro %}{% set ns.f=f %}{% endfor %}{{ns.f()}}", NULL, &output, &error), JINJA_CMETA_OK);
    check_equal(output, "3:True:1:3;3:True:2:2;");
  }

  it("separates missing lookahead length_unsized_after") {
    check_equal(test_native_render("{% set ns=namespace() %}{% for x in [1] %}{% macro f() %}{% for y in [1,x,3] if true %}{{loop.nextitem is undefined}}:{{loop.length}}:{{loop.index}}:{{loop.revindex}};{% endfor %}{% endmacro %}{% set ns.f=f %}{% endfor %}{{ns.f()}}", NULL, &output, &error), JINJA_CMETA_OK);
    check_equal(output, "True:2:1:2;True:2:2:1;");
  }

  it("separates missing lookahead reverse") {
    check_equal(test_native_render("{% set ns=namespace() %}{% for x in [1] %}{% macro f() %}{% for y in [3,x,1]|reverse %}{{y}}:{{loop.nextitem is undefined}}:{{loop.previtem|default('-')}};{% endfor %}{% endmacro %}{% set ns.f=f %}{% endfor %}{{ns.f()}}", NULL, &output, &error), JINJA_CMETA_OK);
    check_equal(output, "1:True:-;3:True:1;");
  }

  it("separates missing lookahead dict_keys") {
    check_equal(test_native_render("{% set ns=namespace() %}{% for x in [1] %}{% macro f() %}{% for y in {1:'a',x:'b',3:'c'} %}{{y}}:{{loop.last}}:{{loop.length}};{% endfor %}{% endmacro %}{% set ns.f=f %}{% endfor %}{{ns.f()}}", NULL, &output, &error), JINJA_CMETA_OK);
    check_equal(output, "1:True:3;3:True:3;");
  }

  it("separates missing lookahead self_after_peek") {
    check_equal(test_native_render("{% set ns=namespace() %}{% for x in [1] %}{% macro f() %}{% for y in [1,x,3,4] %}{{loop.nextitem is undefined}}:{{loop|list}}:{{loop.index}}:{{loop.previtem}};{% endfor %}{% endmacro %}{% set ns.f=f %}{% endfor %}{{ns.f()}}", NULL, &output, &error), JINJA_CMETA_OK);
    check_equal(output, "True:[(3, <LoopContext 3/4>), (4, <LoopContext 3/4>)]:3:3;");
  }

  it("reads loop neighbors through macro parameters and item attributes") {
    check_equal(test_native_render("{% macro f(l) %}{{l['index']}}:{{l|attr('length')}}:{{l.previtem|default('-')}}:{{l.nextitem|default('-')}};{% endmacro %}{% for x in [10,20] %}{{f(loop)}}{% endfor %}", NULL, &output, &error), JINJA_CMETA_OK);
    check_equal(output, "1:2:-:20;2:2:10:-;");
  }

  it("keeps escaped loop aliases live across iterations and scope exit") {
    check_equal(test_native_render("{% set ns=namespace() %}{% for x in [1,2] %}{% if loop.first %}{% set ns.l=loop %}{% endif %}{{ns.l.index}};{% endfor %}{{ns.l.index}}:{{ns.l.previtem}}", NULL, &output, &error), JINJA_CMETA_OK);
    check_equal(output, "1;2;2:1");
  }

  it("distinguishes loop objects from iteration element scalar types") {
    check_equal(test_native_render("{% for x in [0] %}{% set l=loop %}{{l is number}}|{{l is string}}|{{l is callable}}|{{l is iterable}}|{{l is sequence}}|{{l is defined}}|{{not l}}|{{l==x}}{% endfor %}", NULL, &output, &error), JINJA_CMETA_OK);
    check_equal(output, "False|False|True|True|False|True|False|False");
  }

  it("renders live loop values through strings and containers") {
    check_equal(test_native_render("{% for x in [1,2] %}{% set l=loop %}{{l}}|{{[l]}}|{{l|string}};{% endfor %}", NULL, &output, &error), JINJA_CMETA_OK);
    check_equal(output, "<LoopContext 1/2>|[<LoopContext 1/2>]|<LoopContext 1/2>;<LoopContext 2/2>|[<LoopContext 2/2>]|<LoopContext 2/2>;");
  }

  it("shares filtered loop lookahead between aliases and metadata") {
    check_equal(test_native_render("{% macro f(l) %}{{l.nextitem|default('-')}}:{{l.length}}:{{l.last}}{% endmacro %}{% for x in [1,2,3] if x!=2 %}{{f(loop)}};{% endfor %}", NULL, &output, &error), JINJA_CMETA_OK);
    check_equal(output, "3:2:False;-:2:True;");
  }

  it("supports loop value methods through retained aliases") {
    check_equal(test_native_render("{% set ns=namespace() %}{% for x in [1,1,2] %}{% if loop.first %}{% set ns.c=loop.cycle %}{% set ns.d=loop.changed %}{% endif %}{{ns.c('a','b')}}:{{ns.d(x)}}:{{ns.c==loop.cycle}};{% endfor %}", NULL, &output, &error), JINJA_CMETA_OK);
    check_equal(output, "a:True:True;b:False:True;a:True:True;");
  }

  it("supports loop value recursive through retained aliases") {
    check_equal(test_native_render("{% macro f(l) %}{{l(iterable=[0])}}{% endmacro %}{% for x in [1] recursive %}{{loop.depth}}:{{x}};{% if x %}{{f(loop)}}{% endif %}{% endfor %}", NULL, &output, &error), JINJA_CMETA_OK);
    check_equal(output, "1:1;2:0;");
  }

  it("supports loop value length through retained aliases") {
    check_equal(test_native_render("{% for x in [0,1] %}{% set l=loop %}{{l|length}}:{% if l %}yes{% endif %}:{{l[0] is undefined}};{% endfor %}", NULL, &output, &error), JINJA_CMETA_OK);
    check_equal(output, "2:yes:True;2:yes:True;");
  }

  it("preserves loop receiver identity semantics") {
    check_equal(test_native_render("{% set ns=namespace() %}{% for x in [1] %}{% set ns.l=loop %}{% set ns.c=loop.cycle %}{% for y in [2] %}{{ns.l==loop}}:{{ns.c==loop.cycle}}:{{ns.l==ns.l}};{% endfor %}{% endfor %}", NULL, &output, &error), JINJA_CMETA_OK);
    check_equal(output, "False:False:True;");
  }

  it("preserves loop receiver method_arg semantics") {
    check_equal(test_native_render("{% macro f(c,d) %}{{c(*['a','b'])}}:{{d(*[1])}}:{{d(1)}}{% endmacro %}{% for x in [0,0] %}{{f(loop.cycle,loop.changed)}};{% endfor %}", NULL, &output, &error), JINJA_CMETA_OK);
    check_equal(output, "a:True:False;b:False:False;");
  }

  it("preserves loop receiver key semantics") {
    check_equal(test_native_render("{% for x in [1] %}{% set m={loop: 'ok'} %}{{m[loop]}}:{{loop in m}}{% endfor %}", NULL, &output, &error), JINJA_CMETA_OK);
    check_equal(output, "ok:True");
  }

  it("rejects invalid generic loop invocations without rendering partial results") {
    static const char *const calls[] = {"l()", "l([0])", "c()", "c(a=1)", "d(a=1)", "l(1,2)", "c(*(1/0))"};
    for (size_t i = 0u; i < sizeof(calls) / sizeof(calls[0]); ++i) {
      tstr source = tstr_new();
      check_not_null(source);
      tstr next = tstr_cat_fmt(source, "{%% for x in [1] %%}{%% set l=loop %%}{%% set c=l.cycle %%}{%% set d=l.changed %%}{{%s}}{%% endfor %%}", calls[i]);
      if (next == NULL) tstr_free(source);
      check_not_null(next);
      source = next;
      JINJA_CMETA_STATUS status = test_native_render(source, NULL, &output, &error);
      tstr_free(source);
      check_equal(status, JINJA_CMETA_ERR_RENDER);
      check_equal(tstr_len(output), (size_t)0u);
    }
  }

  it("retains original loop receivers for nested_elements") {
    check_equal(test_native_render("{% for x in [1,2] %}{% for l in [loop] %}{{l.index}}:{{loop.index}};{% endfor %}{% endfor %}", NULL, &output, &error), JINJA_CMETA_OK);
    check_equal(output, "1:1;2:1;");
  }

  it("retains original loop receivers for nested_methods") {
    check_equal(test_native_render("{% for x in [1,2] %}{% for c in [loop.cycle] %}{{c('a','b')}};{% endfor %}{% endfor %}", NULL, &output, &error), JINJA_CMETA_OK);
    check_equal(output, "a;b;");
  }

  it("preserves receiver identity through object_neighbor") {
    check_equal(test_native_render("{% for x in [1,2] %}{% set outer=loop %}{% for l in [outer,outer] %}{% if loop.first %}{{loop.nextitem.index}}{% else %}{{loop.previtem.index}}{% endif %};{% endfor %}{% endfor %}", NULL, &output, &error), JINJA_CMETA_OK);
    check_equal(output, "1;1;2;2;");
  }

  it("preserves receiver identity through method_neighbor") {
    check_equal(test_native_render("{% for x in [1,2] %}{% for c in [loop.cycle,loop.cycle] %}{% if loop.first %}{{loop.nextitem('a','b')}}{% else %}{{loop.previtem('a','b')}}{% endif %};{% endfor %}{% endfor %}", NULL, &output, &error), JINJA_CMETA_OK);
    check_equal(output, "a;a;b;b;");
  }

  it("bounds repeated invocation of a retained root recursive loop") {
    JINJA_CMETA_RENDER_OPTIONS options = JINJA_CMETA_RENDER_OPTIONS_INIT;
    options.max_render_depth = 4u;
    const char *source = "{% set ns=namespace(n=0) %}{% for x in [1] recursive %}{% if ns.n==0 %}{% set ns.l=loop %}{% endif %}{% set ns.n=ns.n+1 %}{% if ns.n<8 %}{{ns.l([0])}}{% endif %}{% endfor %}{{ns.n}}";
    check_equal(test_native_render(source,
        &options, &output, &error), JINJA_CMETA_ERR_CAPACITY);
    check_equal(tstr_len(output), (size_t)0u);
    options.max_render_depth = 8u;
    check_equal(test_native_render(source, &options, &output, &error), JINJA_CMETA_OK);
    check_equal(output, "8");
  }

  it("shares loop consumption with first") {
    check_equal(test_native_render("{% for x in [1,2,3] %}{{x}}:{% set p=loop|first %}{{p[0] if p is defined else '-'}}:{{loop.index}};{% endfor %}", NULL, &output, &error), JINJA_CMETA_OK);
    check_equal(output, "1:2:2;3:-:3;");
  }

  it("shares loop consumption with list") {
    check_equal(test_native_render("{% for x in [1,2,3] %}{% set l=loop %}{{l|list}}|{{l.index}}|{{x}};{% endfor %}", NULL, &output, &error), JINJA_CMETA_OK);
    check_equal(output, "[(2, <LoopContext 3/3>), (3, <LoopContext 3/3>)]|3|1;");
  }

  it("shares loop consumption with for") {
    check_equal(test_native_render("{% for x in [1,2,3] %}{{x}}[{% for y,l in loop %}{{y}}:{{l.index}}:{{loop.index}};{% endfor %}]{% endfor %}", NULL, &output, &error), JINJA_CMETA_OK);
    check_equal(output, "1[2:2:1;3:3:2;]");
  }

  it("shares loop consumption with member") {
    check_equal(test_native_render("{% for x in [1,2,3,4] %}{{x}}:{{(3,loop) in loop}}:{{loop.index}};{% endfor %}", NULL, &output, &error), JINJA_CMETA_OK);
    check_equal(output, "1:True:3;4:False:4;");
  }

  it("shares loop consumption with expand") {
    check_equal(test_native_render("{% macro f() %}{{varargs}}{% endmacro %}{% for x in [1,2,3] %}{{f(*loop)}}|{{loop.index}};{% endfor %}", NULL, &output, &error), JINJA_CMETA_OK);
    check_equal(output, "((2, <LoopContext 3/3>), (3, <LoopContext 3/3>))|3;");
  }

  it("shares loop consumption with filtered") {
    check_equal(test_native_render("{% for x in [1,2,3,4] if x!=2 %}{{x}}:{% set p=loop|first %}{{p[0] if p is defined else '-'}}:{{loop.index}}:{{loop.nextitem|default('-')}};{% endfor %}", NULL, &output, &error), JINJA_CMETA_OK);
    check_equal(output, "1:3:2:4;4:-:3:-;");
  }

  it("handles loop iteration unpack") {
    check_equal(test_native_render("{% for x in [1,2,3] %}{% set a,b=loop %}{{a}}|{{b}}|{{loop.index}};{% endfor %}", NULL, &output, &error), JINJA_CMETA_OK);
    check_equal(output, "(2, <LoopContext 3/3>)|(3, <LoopContext 3/3>)|3;");
  }

  it("handles loop iteration reverse") {
    check_equal(test_native_render("{% for x in [1,2,3] %}{{loop|reverse}}|{{loop.index}};{% endfor %}", NULL, &output, &error), JINJA_CMETA_OK);
    check_equal(output, "[(3, <LoopContext 3/3>), (2, <LoopContext 3/3>)]|3;");
  }

  it("handles loop iteration lookahead") {
    check_equal(test_native_render("{% for x in [1,2,3,4] %}{% set outer=loop %}{% for y,l in outer %}{{y}}:{{loop.length}}:{{l.index}}:{{loop.nextitem[0] if loop.nextitem is defined else '-'}};{% endfor %}{% endfor %}", NULL, &output, &error), JINJA_CMETA_OK);
    check_equal(output, "2:4:2:3;3:4:3:4;4:4:4:-;");
  }

  it("handles loop iteration aliases") {
    check_equal(test_native_render("{% for x in [1,2,3,4] %}{% set a=loop %}{% set b=loop %}{{(a|first)[0]}}:{{(b|first)[0]}}:{{a.index}};{% break %}{% endfor %}", NULL, &output, &error), JINJA_CMETA_OK);
    check_equal(output, "2:3:3;");
  }

  it("handles loop iteration break_escape") {
    check_equal(test_native_render("{% set ns=namespace() %}{% for x in [1,2,3] %}{% set ns.l=loop %}{% break %}{% endfor %}{{ns.l|list}}|{{ns.l.index}}", NULL, &output, &error), JINJA_CMETA_OK);
    check_equal(output, "[(2, <LoopContext 3/3>), (3, <LoopContext 3/3>)]|3");
  }

  it("handles loop iteration continue") {
    check_equal(test_native_render("{% for x in [1,2,3,4] %}{{x}}:{% set p=loop|first %}{{p[0]}};{% continue %}{% endfor %}", NULL, &output, &error), JINJA_CMETA_OK);
    check_equal(output, "1:2;3:4;");
  }

  it("handles loop iteration items") {
    check_equal(test_native_render("{% for x in [1,2] %}{{loop|items|list}}{% endfor %}", NULL, &output, &error), JINJA_CMETA_ERR_RENDER);
    check_equal(output, "");
  }

  it("defers invalid loop items validation until consumption") {
    check_equal(test_native_render("{% for x in [1] %}{% set it=loop|items %}{{it is iterable}}:{{it is mapping}}:{% if it %}yes{% endif %}{% endfor %}", NULL, &output, &error), JINJA_CMETA_OK);
    check_equal(output, "True:False:yes");
  }

  it("handles loop iteration last") {
    check_equal(test_native_render("{% for x in [1,2] %}{{loop|last}}{% endfor %}", NULL, &output, &error), JINJA_CMETA_ERR_RENDER);
    check_equal(output, "");
  }

  it("handles loop iteration unpack_short") {
    check_equal(test_native_render("{% for x in [1,2] %}{% set a,b=loop %}{% endfor %}", NULL, &output, &error), JINJA_CMETA_ERR_RENDER);
    check_equal(output, "");
  }

  it("handles loop iteration unpack_long") {
    check_equal(test_native_render("{% for x in [1,2,3,4] %}{% set a,b=loop %}{% endfor %}", NULL, &output, &error), JINJA_CMETA_ERR_RENDER);
    check_equal(output, "");
  }

  it("handles loop iteration filtered_list") {
    check_equal(test_native_render("{% for x in [1,2,3,4] if x!=2 %}{{loop|list}}|{{loop.index}}|{{loop.length}};{% endfor %}", NULL, &output, &error), JINJA_CMETA_OK);
    check_equal(output, "[(3, <LoopContext 3/3>), (4, <LoopContext 3/3>)]|3|3;");
  }

  it("handles loop iteration empty_first") {
    check_equal(test_native_render("{% for x in [1] %}{{loop|first is undefined}}|{{loop|list}}|{{loop.index}}{% endfor %}", NULL, &output, &error), JINJA_CMETA_OK);
    check_equal(output, "True|[]|1");
  }

  it("keeps loop representation length separate from exhaustion") {
    check_equal(test_native_render("{% for x in [1,2,3] %}{% set outer=loop %}{% for y,l in outer %}{{loop}}:{{loop.revindex}}:{{outer.index}}:{{loop.last}};{% endfor %}{% endfor %}", NULL, &output, &error), JINJA_CMETA_OK);
    check_equal(output, "<LoopContext 1/3>:3:2:False;<LoopContext 2/3>:2:3:True;");
  }

  it("bounds loop consumption workspace before output") {
    JINJA_CMETA_RENDER_OPTIONS options = JINJA_CMETA_RENDER_OPTIONS_INIT;
    const char *source = "{% for x in range(64) %}{{loop|list|length}}{% endfor %}";
    options.max_nodes = 16u;
    check_equal(test_native_render(source, &options, &output, &error), JINJA_CMETA_ERR_CAPACITY);
    check_equal(tstr_len(output), (size_t)0u);
    options.max_nodes = 256u;
    check_equal(test_native_render(source, &options, &output, &error), JINJA_CMETA_OK);
    check_equal(output, "63");
  }

  it("stops retained loop tuple output when the callback fails") {
    templ = jinja_cmeta_compile(vstr_from_cstr(
        "{% for x in [1,2,3] %}{{loop|list}}tail{% endfor %}"), NULL, &error);
    check_not_null(templ);
    const JINJA_CMETA_RENDERER renderer = {test_native_reject_repr};
    vstr root = vstr_from_cstr("");
    size_t calls = 0u;
    check_equal(jinja_cmeta_render(templ, jinja_cmeta_vstr_data(), &root,
        NULL, &renderer, &calls, &error), JINJA_CMETA_ERR_RENDER);
    check_equal(calls, (size_t)2u);
    check_equal(error.status, JINJA_CMETA_ERR_RENDER);
  }

  it("exposes macro signature and discovered capabilities") {
    check_equal(test_native_render("{% macro f(a,b=2) %}{{varargs}}{{kwargs}}{{caller()}}{% endmacro %}{{f.name}}|{{f.arguments}}|{{f.catch_kwargs}}|{{f.catch_varargs}}|{{f.caller}}|{{f.explicit_caller}}",
        NULL, &output, &error), JINJA_CMETA_OK);
    check_equal(output, "f|('a', 'b')|True|True|True|False");
  }

  it("looks up macro attributes without inventing absent members") {
    check_equal(test_native_render("{% macro f(caller=none) %}{% endmacro %}{{f['name']}}|{{(f|attr('arguments'))[0]}}|{{f.explicit_caller}}|{{f.caller}}|{{f.catch_kwargs}}|{{f.catch_varargs}}|{{f['absent'] is undefined}}|{{f[0] is undefined}}",
        NULL, &output, &error), JINJA_CMETA_OK);
    check_equal(output, "f|caller|True|False|False|False|True|True");
  }

  it("renders macro identity through output strings and containers") {
    check_equal(test_native_render("{% macro f() %}{% endmacro %}{{f}}|{{f|string}}|{{[f]}}|{{''~f}}",
        NULL, &output, &error), JINJA_CMETA_OK);
    check_equal(output, "<Macro 'f'>|<Macro 'f'>|[<Macro 'f'>]|<Macro 'f'>");
  }

  it("exposes anonymous caller metadata and representation") {
    check_equal(test_native_render("{% macro f() %}{{caller.name is none}}|{{caller.arguments}}|{{caller}}{% endmacro %}{% call(x) f() %}{% endcall %}",
        NULL, &output, &error), JINJA_CMETA_OK);
    check_equal(output, "True|('x',)|<Macro anonymous>");
  }

  it("escapes macro representations in the active output context") {
    check_equal(test_native_render("{% macro f() %}{% endmacro %}{% autoescape true %}{{f}}|{{f|string}}{% endautoescape %}",
        NULL, &output, &error), JINJA_CMETA_OK);
    check_equal(output, "&lt;Macro &#39;f&#39;&gt;|&lt;Macro &#39;f&#39;&gt;");
  }

  it("handles empty macro parameter tuples") {
    check_equal(test_native_render("{% macro f() %}{% endmacro %}{{f.arguments}}|{{f.arguments|length}}|{{f.arguments|list}}",
        NULL, &output, &error), JINJA_CMETA_OK);
    check_equal(output, "()|0|[]");
  }

  it("preserves Unicode macro and parameter names") {
    check_equal(test_native_render("{% macro 名(值) %}{% endmacro %}{{名}}|{{名.name}}|{{名.arguments}}",
        NULL, &output, &error), JINJA_CMETA_OK);
    check_equal(output, "<Macro '名'>|名|('值',)");
  }

  it("retains parameter metadata after rebinding the macro") {
    check_equal(test_native_render("{% macro f(a,b) %}{% endmacro %}{% set args=f.arguments %}{% set f=none %}{{args}}|{{args[-1]}}|{{args|join(':')}}",
        NULL, &output, &error), JINJA_CMETA_OK);
    check_equal(output, "('a', 'b')|b|a:b");
  }

  it("bounds macro metadata snapshots and retained representation bytes") {
    enum { PARAMETER_COUNT = 10 };
    JINJA_CMETA_RENDER_OPTIONS options = JINJA_CMETA_RENDER_OPTIONS_INIT;
    options.max_nodes = 2u;
    templ = jinja_cmeta_compile(vstr_from_cstr(
        "{% macro f(a,b,c,d,e,f,g,h,i,j) %}{% endmacro %}{{f.arguments}}"), NULL, &error);
    check_not_null(templ);
    const JINJA_CMETA_RENDERER renderer = {test_native_write};
    vstr root = vstr_from_cstr("");
    const char *expected = "('a', 'b', 'c', 'd', 'e', 'f', 'g', 'h', 'i', 'j')";
    check_equal(jinja_cmeta_render(templ, jinja_cmeta_vstr_data(), &root,
        &options, &renderer, &output, &error), JINJA_CMETA_OK);
    check_equal(output, expected);
    tstr_clear(output);
    JINJA_CMETA_RUNTIME_CONFIG *config = jinja_cmeta_runtime_config_create(&error);
    check_not_null(config);
    check_equal(jinja_cmeta_runtime_config_set_limit(config, JINJA_CMETA_RESOURCE_VALUES,
        PARAMETER_COUNT - 1u, &error), JINJA_CMETA_OK);
    check_equal(jinja_cmeta_render_ex(templ, jinja_cmeta_vstr_data(), &root,
        &options, config, &renderer, &output, &error), JINJA_CMETA_ERR_CAPACITY);
    check_equal(tstr_len(output), (size_t)0u);
    check_equal(jinja_cmeta_runtime_config_set_limit(config, JINJA_CMETA_RESOURCE_VALUES,
        PARAMETER_COUNT, &error), JINJA_CMETA_OK);
    check_equal(jinja_cmeta_render_ex(templ, jinja_cmeta_vstr_data(), &root,
        &options, config, &renderer, &output, &error), JINJA_CMETA_OK);
    check_equal(output, expected);
    tstr_clear(output);
    jinja_cmeta_runtime_config_destroy(config);
    options = (JINJA_CMETA_RENDER_OPTIONS)JINJA_CMETA_RENDER_OPTIONS_INIT;
    options.max_string_bytes = sizeof("<Macro 'f'>") - 2u;
    check_equal(test_native_render("{% macro f() %}{% endmacro %}{{f|string}}",
        &options, &output, &error), JINJA_CMETA_ERR_CAPACITY);
    check_equal(tstr_len(output), (size_t)0u);
    options.max_string_bytes += 1u;
    check_equal(test_native_render("{% macro f() %}{% endmacro %}{{f|string}}",
        &options, &output, &error), JINJA_CMETA_OK);
    check_equal(output, "<Macro 'f'>");
  }

  it("stops macro representation immediately when the output callback fails") {
    templ = jinja_cmeta_compile(vstr_from_cstr(
        "{% macro f() %}{% endmacro %}{{f}}tail"), NULL, &error);
    check_not_null(templ);
    const JINJA_CMETA_RENDERER renderer = {test_native_reject_repr};
    vstr root = vstr_from_cstr("");
    size_t calls = 0u;
    check_equal(jinja_cmeta_render(templ, jinja_cmeta_vstr_data(), &root,
        NULL, &renderer, &calls, &error), JINJA_CMETA_ERR_RENDER);
    check_equal(calls, (size_t)2u);
    check_equal(error.status, JINJA_CMETA_ERR_RENDER);
  }

  it("retains independent escaped inner macro activations") {
    check_equal(test_native_render("{% set ns=namespace() %}{% macro outer(x) %}"
        "{% macro inner() %}{{x}}{% endmacro %}{% set ns.f=inner %}{% endmacro %}"
        "{{outer(1)}}{% set f=ns.f %}{{outer(2)}}{{f()}}|{{ns.f()}}",
        NULL, &output, &error), JINJA_CMETA_OK);
    check_equal(output, "1|2");
  }

  it("binds variadic arguments and evaluates explicit caller defaults") {
    check_equal(test_native_render("{% macro f(a=1) %}{{a}}|{{varargs}}|{{kwargs}}{% endmacro %}"
        "{{f(*[2,3,4],**{'z':5})}}/{% macro g(caller='x') %}{{caller}}{% endmacro %}{{g()}}",
        NULL, &output, &error), JINJA_CMETA_OK);
    check_equal(output, "2|(3, 4)|{'z': 5}/x");
  }

  it("captures loop arguments and loop metadata in macro bodies") {
    check_equal(test_native_render("{% for x in [1,2] %}{% macro f() %}{{x}}:{{loop.index}}"
        ":{{loop.cycle('a','b')}}{% endmacro %}{{f()}};{% endfor %}",
        NULL, &output, &error), JINJA_CMETA_OK);
    check_equal(output, "1:1:a;2:2:b;");
  }

  it("isolates filter lookahead from body assignments during macro calls") {
    check_equal(test_native_render("{% set q=3 %}{% for x in [1,2] if x<q %}"
        "{% macro f() %}{{x}}{% endmacro %}{% set q=0 %}{{f()}}:{{loop.length}};{% endfor %}",
        NULL, &output, &error), JINJA_CMETA_OK);
    check_equal(output, "1:2;2:2;");
  }

  it("uses definition escaping and caller result safety without leaking capture locals") {
    check_equal(test_native_render("{% set x='root' %}{% macro f(v) %}{{v}}{% set a %}"
        "{% set x='inner' %}{{x}}{% endset %}{{a}}|{{x}}{% endmacro %}"
        "{% autoescape true %}{{f('<b>')}}{% endautoescape %}",
        NULL, &output, &error), JINJA_CMETA_OK);
    check_equal(output, "<b>inner|root");
  }

  it("executes lexical macros defaults recursive calls and caller bodies") {
    static const struct { const char *source; const char *expected; } cases[] = {
      {"{% macro f(a=b,b=2) %}{{a}}:{{b}}{% endmacro %}{{f()}}|{{f(b=3)}}", ":2|3:3"},
      {"{% set x='outer' %}{% macro f() %}{{x}}{% endmacro %}"
       "{% with x='caller' %}{{f()}}{% endwith %}{% set x='later' %}|{{f()}}", "outer|later"},
      {"{% macro f(n) %}{{n}}{% if n %}{{f(n-1)}}{% endif %}{% endmacro %}{{f(2)}}", "210"},
      {"{% macro wrap() %}[{{caller('Ada')}}]{% endmacro %}"
       "{% call(name) wrap() %}Hi {{name}}{% endcall %}", "[Hi Ada]"}
    };
    const JINJA_CMETA_RENDERER renderer = {test_native_write};
    vstr root = vstr_from_cstr("");
    for (size_t i = 0u; i < sizeof(cases) / sizeof(cases[0]); ++i) {
      info("source: %s", cases[i].source);
      templ = jinja_cmeta_compile(vstr_from_cstr(cases[i].source), NULL, &error);
      check_not_null(templ);
      check_equal(jinja_cmeta_render(templ, jinja_cmeta_vstr_data(), &root,
          NULL, &renderer, &output, &error), JINJA_CMETA_OK);
      check_equal(output, cases[i].expected);
      jinja_cmeta_release(templ);
      templ = NULL;
      tstr_free(output);
      output = tstr_new();
    }
  }

  it("preserves positional parameter duplicates for kwargs and normalizes absent caller") {
    check_equal(test_native_render("{% macro f(a) %}{{a}}|{{kwargs}}{% endmacro %}{{f(1,a=2)}}/"
        "{% macro g() %}{{caller is undefined}}{% endmacro %}{{g(caller=none)}}",
        NULL, &output, &error), JINJA_CMETA_OK);
    check_equal(output, "1|{'a': 2}/True");
  }

  it("passes a call block caller through a kwargs-only macro") {
    check_equal(test_native_render("{% macro f() %}{{kwargs.caller()}}{% endmacro %}"
        "{% call f() %}X{% endcall %}", NULL, &output, &error), JINJA_CMETA_OK);
    check_equal(output, "X");
  }

  it("orders injected caller after named keywords and before expanded keywords") {
    check_equal(test_native_render("{% macro f(caller=none) %}{{caller}}|{{kwargs.caller()}}|{{kwargs|list}}"
        "{% endmacro %}{% call f(7,a=1,**{'b':2}) %}ok{% endcall %}",
        NULL, &output, &error), JINJA_CMETA_OK);
    check_equal(output, "7|ok|['a', 'caller', 'b']");
  }

  it("rejects bad macro bindings after eager argument evaluation") {
    static const char *const sources[] = {
      "{% macro f(a) %}{{a}}{% endmacro %}{{f(1,a=2)}}",
      "{% macro f() %}x{% endmacro %}{{f(1)}}",
      "{% macro f() %}x{% endmacro %}{{f(z=1)}}",
      "{% macro f() %}x{% endmacro %}{{f(*(1/0))}}",
      "{% macro f(a=1/0) %}{{a}}{% endmacro %}{{f()}}",
      "{% macro f() %}{{caller()}}{% endmacro %}{{f()}}"
    };
    for (size_t i = 0u; i < sizeof(sources) / sizeof(sources[0]); ++i) {
      check_equal(test_native_render(sources[i], NULL, &output, &error), JINJA_CMETA_ERR_RENDER);
      check_equal(tstr_len(output), (size_t)0u);
    }
  }

  it("bounds recursive macro calls and retained activation resources") {
    JINJA_CMETA_RENDER_OPTIONS options = JINJA_CMETA_RENDER_OPTIONS_INIT;
    options.max_render_depth = 1u;
    check_equal(test_native_render("{% macro f() %}{{f()}}{% endmacro %}{{f()}}",
        &options, &output, &error), JINJA_CMETA_ERR_CAPACITY);
    check_equal(tstr_len(output), (size_t)0u);
    options = (JINJA_CMETA_RENDER_OPTIONS)JINJA_CMETA_RENDER_OPTIONS_INIT;
    options.max_nodes = 1u;
    check_equal(test_native_render("{% macro f() %}{% endmacro %}{{f()}}",
        &options, &output, &error), JINJA_CMETA_ERR_CAPACITY);
    check_equal(tstr_len(output), (size_t)0u);
  }

  it("executes recursive loops through lexical cells and restores the caller frame") {
    check_equal(test_native_render("{% for x in [1] recursive %}{{x}}"
        "{% if x %}{{loop([0])}}{% endif %}{% endfor %}",
        NULL, &output, &error), JINJA_CMETA_OK);
    check_equal(output, "10");
  }

  it("reuses one recursive loop activation across iterations for escaped macros") {
    check_equal(test_native_render("{% set ns=namespace() %}{% for x in [1,2] recursive %}"
        "{% if x==1 %}{% macro f() %}{{x}}{% endmacro %}{% set ns.f=f %}"
        "{% else %}{{ns.f()}}{% endif %}{% endfor %}",
        NULL, &output, &error), JINJA_CMETA_OK);
    check_equal(output, "2");
  }

  it("shares recursive body cells with else writes after continue") {
    check_equal(test_native_render("{% set ns=namespace() %}{% for x in [1] recursive %}"
        "{% macro f() %}{{x}}{% endmacro %}{% set ns.f=f %}{% continue %}"
        "{% else %}{% set x=9 %}{{ns.f()}}{% endfor %}", NULL, &output, &error), JINJA_CMETA_OK);
    check_equal(output, "9");
  }
}
