/* Compile the same public API checks as C++17 to prevent language coverage
 * drift. This includes only the test body; generated implementation and
 * Schema/DataBind remain separately compiled C libraries. */
#include "test_data_bind_typed_cmeta_public.c"
