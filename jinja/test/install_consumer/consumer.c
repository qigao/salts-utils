#include <jinja_cmeta.h>

#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

int main(void) {
  static const char source[] =
      "{{ true }}|{{ '<&>' }}|{{ '<&>' | e }}|"
      "{% for x in [1, 2] %}{{ x }}{% else %}empty{% endfor %}\n";
  static const char expected[] = "True|<&>|&lt;&amp;&gt;|12\r\n";
  bool root = true;
  JINJA_CMETA_ERROR error = JINJA_CMETA_ERROR_INIT;
  JINJA_CMETA_COMPILE_OPTIONS options = JINJA_CMETA_COMPILE_OPTIONS_INIT;
  options.keep_trailing_newline = 1;
  options.newline_sequence = vstr_from_cstr("\r\n");
  JINJA_CMETA_TEMPLATE *templ = jinja_cmeta_compile(vstr_from_cstr(source), &options, &error);
  char *output = NULL;
  JINJA_CMETA_STATUS status;
  int result;
  if (templ == NULL) return EXIT_FAILURE;
  status = jinja_cmeta_render_string(templ, &cmeta_data_bool, &root, NULL, &output, &error);
  result = status == JINJA_CMETA_OK && output != NULL && strcmp(output, expected) == 0
               ? EXIT_SUCCESS : EXIT_FAILURE;
  free(output);
  jinja_cmeta_release(templ);
  return result;
}
