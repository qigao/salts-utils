#include "jinja_cmeta_runtime.h"

#include <type_traits>

using RuntimeStringRender = JINJA_CMETA_STATUS (*)(const JINJA_CMETA_TEMPLATE *,
    const cmeta_data_desc *, const void *, const JINJA_CMETA_RENDER_OPTIONS *,
    const JINJA_CMETA_RUNTIME_CONFIG *, char **, JINJA_CMETA_ERROR *);

static_assert(std::is_same<decltype(&jinja_cmeta_render_string_ex), RuntimeStringRender>::value,
    "runtime string entry must preserve the C value and error contracts");
