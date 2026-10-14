/* Deliberately compile the UNMODIFIED vendored Monocypher implementation
 * through DataBind's private symbol namespace. The source is compiled only
 * once; this translation unit replaces direct monocypher.c compilation.
 */
#include "databind_monocypher_namespace.h"
#include "monocypher.c"
