#include "search_glob.h"
#include <tinytest.h>

#include <stdint.h>
#include <string.h>

static int glob_test(const char *pattern, const char *path,
                     salts_search_glob_scope scope, bool folder,
                     bool fold, int expected) {
  salts_search_glob_program p = {0};
  unsigned char scratch[1024];
  bool matched = !expected;
  salts_search_glob_status rc;
  if (salts_search_glob_compile(&p, pattern, strlen(pattern), scope, fold,
                                256u) != SALTS_SEARCH_GLOB_OK)
    return 0;
  rc = salts_search_glob_match(&p, path, strlen(path), folder, scratch,
                               sizeof(scratch), &matched);
  salts_search_glob_destroy(&p);
  return rc == SALTS_SEARCH_GLOB_OK && matched == !!expected;
}

/* Simple independent recursive oracle for a subset: ASCII literals, '*' and '?'.
 * Small inputs only; the implementation under test is the bounded NFA. */
static bool oracle_simple(const unsigned char *pattern,
                          const unsigned char *text) {
  if (*pattern == '\0')
    return *text == '\0';
  if (*pattern == '*')
    return oracle_simple(pattern + 1, text) ||
           (*text != '\0' && *text != '/' && oracle_simple(pattern, text + 1));
  if (*pattern == '?')
    return *text != '\0' && *text != '/' && oracle_simple(pattern + 1, text + 1);
  return *pattern == *text && *text != '\0' &&
         oracle_simple(pattern + 1, text + 1);
}

static uint32_t seed = UINT32_C(0x51A3C11);
static uint32_t rand_next(void) {
  uint32_t x = seed;
  x ^= x << 13;
  x ^= x >> 17;
  x ^= x << 5;
  return seed = x;
}

spec("search A3 byte glob NFA (memory only)") {
  it("accepts exact path and single-component wildcard without crossing /") {
    check_true(glob_test("src/*.c", "src/search.c", SALTS_SEARCH_GLOB_PATH, false, false, 1));
    check_true(glob_test("src/*.c", "src/a/b.c", SALTS_SEARCH_GLOB_PATH, false, false, 0));
    check_true(glob_test("a?c", "abc", SALTS_SEARCH_GLOB_PATH, false, false, 1));
    check_true(glob_test("a?c", "a/c", SALTS_SEARCH_GLOB_PATH, false, false, 0));
    check_true(glob_test("*.h", "path/test.h", SALTS_SEARCH_GLOB_PATH, false, false, 0));
    check_true(glob_test("*.h", "path/test.h", SALTS_SEARCH_GLOB_COMPONENT, false, false, 1));
  }

  it("recognizes **/ as zero-or-more entire directory prefixes") {
    check_true(glob_test("**/foo", "foo", SALTS_SEARCH_GLOB_PATH, false, false, 1));
    check_true(glob_test("**/foo", "a/b/foo", SALTS_SEARCH_GLOB_PATH, false, false, 1));
    check_true(glob_test("**/foo", "a/b/foobar", SALTS_SEARCH_GLOB_PATH, false, false, 0));
    check_true(glob_test("src/**/file.c", "src/file.c", SALTS_SEARCH_GLOB_PATH, false, false, 1));
    check_true(glob_test("src/**/file.c", "src/one/two/file.c", SALTS_SEARCH_GLOB_PATH, false, false, 1));
    check_true(glob_test("src/**/file.c", "src/one/file.h", SALTS_SEARCH_GLOB_PATH, false, false, 0));
    check_true(glob_test("a/**", "a/x/y", SALTS_SEARCH_GLOB_PATH, false, false, 1));
    check_true(glob_test("a/**", "a", SALTS_SEARCH_GLOB_PATH, true, false, 0));
    check_true(glob_test("**", "a/b/c", SALTS_SEARCH_GLOB_PATH, false, false, 1));
  }

  it("supports bracket range, negation, escape and ASCII-only folding") {
    check_true(glob_test("file[0-9].c", "file7.c", SALTS_SEARCH_GLOB_PATH, false, false, 1));
    check_true(glob_test("file[!a].c", "fileb.c", SALTS_SEARCH_GLOB_PATH, false, false, 1));
    check_true(glob_test("file[!a].c", "filea.c", SALTS_SEARCH_GLOB_PATH, false, false, 0));
    check_true(glob_test("file[a-c].c", "fileB.c", SALTS_SEARCH_GLOB_PATH, false, true, 1));
    check_true(glob_test("file[a-c].c", "fileB.c", SALTS_SEARCH_GLOB_PATH, false, false, 0));
    check_true(glob_test("foo\\*bar", "foo*bar", SALTS_SEARCH_GLOB_PATH, false, false, 1));
    check_true(glob_test("A*[0-9]", "aXYZ7", SALTS_SEARCH_GLOB_PATH, false, true, 1));
    check_true(glob_test("a**b", "axxb", SALTS_SEARCH_GLOB_PATH, false, false, 1));
  }

  it("uses explicit directory-only and relative path capabilities") {
    check_true(glob_test("build/", "build", SALTS_SEARCH_GLOB_PATH, true, false, 1));
    check_true(glob_test("build/", "build", SALTS_SEARCH_GLOB_PATH, false, false, 0));
    check_true(glob_test("cache/", "work/cache/file", SALTS_SEARCH_GLOB_COMPONENT, false, false, 0));
    check_true(glob_test("cache", "work/cache/file", SALTS_SEARCH_GLOB_COMPONENT, false, false, 1));
    check_true(glob_test("/src/*.h", "src/a.h", SALTS_SEARCH_GLOB_PATH, false, false, 1));
  }

  it("rejects syntax errors, escaped trailing backslash and oversized pattern") {
    const char *bad[] = {"[", "abc[", "foo\\", "[z-a]", "[]", "/"};
    size_t i;
    for (i = 0; i < sizeof(bad) / sizeof(bad[0]); ++i) {
      salts_search_glob_program program = {0};
      check_equal(salts_search_glob_compile(&program, bad[i], strlen(bad[i]),
                    SALTS_SEARCH_GLOB_PATH, false, 64u), SALTS_SEARCH_GLOB_BAD_PATTERN);
      check_null(program.tokens);
    }
    {
      salts_search_glob_program program = {0};
      check_equal(salts_search_glob_compile(&program, "**/a", 4u,
                    SALTS_SEARCH_GLOB_COMPONENT, false, 64u), SALTS_SEARCH_GLOB_BAD_PATTERN);
      check_equal(salts_search_glob_compile(&program, "hello", 5u,
                    SALTS_SEARCH_GLOB_PATH, false, 4u), SALTS_SEARCH_GLOB_LIMIT);
    }
  }

  it("enforces bounded scratch and invalid normalized paths with no fallback") {
    salts_search_glob_program p = {0};
    unsigned char scratch[128] = {0};
    bool result = true;
    check_equal(salts_search_glob_compile(&p, "**/foo", 6u,
                  SALTS_SEARCH_GLOB_PATH, false, 16u), SALTS_SEARCH_GLOB_OK);
    check_equal(salts_search_glob_match(&p, "a/foo", 5u, false, scratch, 1u, &result),
                SALTS_SEARCH_GLOB_LIMIT);
    check_true(!result);
    check_equal(salts_search_glob_match(&p, "a//foo", 6u, false, scratch,
                  sizeof(scratch), &result), SALTS_SEARCH_GLOB_INVALID);
    check_equal(salts_search_glob_match(&p, "a/../foo", 8u, false, scratch,
                  sizeof(scratch), &result), SALTS_SEARCH_GLOB_INVALID);
    check_equal(salts_search_glob_match(&p, "/foo", 4u, false, scratch,
                  sizeof(scratch), &result), SALTS_SEARCH_GLOB_INVALID);
    check_equal(salts_search_glob_match(&p, "foo", 3u, false, scratch,
                  sizeof(scratch), &result), SALTS_SEARCH_GLOB_OK);
    check_true(result);
    salts_search_glob_destroy(&p);
    salts_search_glob_destroy(&p);
  }

  it("matches independently against small reference patterns and random paths") {
    const char *patterns[] = {"*", "?", "a*", "*b", "a?b", "*a*b", "a**b", "abc", "a*b*", "a?*b"};
    size_t p, n;
    int all_match = 1;
    for (p = 0; p < sizeof(patterns) / sizeof(patterns[0]) && all_match; ++p) {
      salts_search_glob_program program = {0};
      unsigned char scratch[512];
      if (salts_search_glob_compile(&program, patterns[p], strlen(patterns[p]),
                        SALTS_SEARCH_GLOB_PATH, false, 32u) != SALTS_SEARCH_GLOB_OK) {
        all_match = 0;
        break;
      }
      for (n = 0; n < 10000u; ++n) {
        char name[20];
        size_t i, len = 1u + rand_next() % 12u;
        bool got = false;
        for (i = 0; i < len; ++i) name[i] = "abc/"[rand_next() % 4u];
        name[len] = 0;
        if (name[0] == '/' || name[len - 1u] == '/' || strstr(name, "//") != NULL)
          continue;
        if (salts_search_glob_match(&program, name, len, false, scratch,
                sizeof(scratch), &got) != SALTS_SEARCH_GLOB_OK ||
            got != oracle_simple((const unsigned char *)patterns[p],
                                 (const unsigned char *)name)) {
          all_match = 0;
          break;
        }
      }
      salts_search_glob_destroy(&program);
    }
    check_true(all_match);
  }
}
