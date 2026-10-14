/* Load the two real shared libraries in both orders in independent
 * sanitizer-instrumented processes. Any duplicate global is fatal under
 * ASan's default ODR check; do not adjust ASAN_OPTIONS to hide it.
 *
 * This is a consumer test, not a second crypto or RPC runtime.
 */
#include <dlfcn.h>
#include <stdio.h>
#include <stdlib.h>

int main(int argc, char **argv) {
  void *first = NULL;
  void *second = NULL;
  void *bind_handle = NULL;
  const char *error;
  if (argc != 4) {
    fprintf(stderr, "usage: %s cnet.so data_bind.so cnet-first|data-first\n", argv[0]);
    return 2;
  }
  if (argv[3][0] == 'c') {
    first = dlopen(argv[1], RTLD_NOW | RTLD_GLOBAL);
    if (!first) goto fail;
    second = dlopen(argv[2], RTLD_NOW | RTLD_GLOBAL);
    if (!second) goto fail;
    bind_handle = second;
  } else if (argv[3][0] == 'd') {
    first = dlopen(argv[2], RTLD_NOW | RTLD_GLOBAL);
    if (!first) goto fail;
    second = dlopen(argv[1], RTLD_NOW | RTLD_GLOBAL);
    if (!second) goto fail;
    bind_handle = first;
  } else {
    return 2;
  }
  /* The consumer has loaded the actual DataBind RPC source library,
   * not merely a vendor-only test archive or a unit-test mock. */
  dlerror();
  if (dlsym(bind_handle, "data_bind_create_from_text") == NULL) {
    error = dlerror();
    fprintf(stderr, "DataBind public API missing: %s\n",
            error != NULL ? error : "unknown");
    return 1;
  }
  if (dlclose(second) != 0 || dlclose(first) != 0) return 1;
  return 0;

fail:
  error = dlerror();
  fprintf(stderr, "native DSO load failed: %s\n",
          error != NULL ? error : "unknown");
  if (second) dlclose(second);
  if (first) dlclose(first);
  return 1;
}
