#include "search_ignore.h"
#include <tinytest.h>

#include <stdint.h>
#include <string.h>

enum { RULE_CAPACITY = 32, PATTERN_CAPACITY = 128,
       FILE_CAPACITY = 1024, PATH_CAPACITY = 256 };

typedef struct ignore_fixture {
  salts_search_ignore_rule rules[RULE_CAPACITY];
  salts_search_ignore_set set;
  unsigned char scratch[2u * (PATTERN_CAPACITY + 1u)];
} ignore_fixture;

static void init_fixture(ignore_fixture *f) {
  memset(f, 0, sizeof(*f));
  check_equal(salts_search_ignore_init(&f->set, f->rules, RULE_CAPACITY,
                   PATTERN_CAPACITY, FILE_CAPACITY, PATH_CAPACITY, false),
              SALTS_SEARCH_IGNORE_OK);
}

static void add_file(ignore_fixture *f, salts_search_ignore_source source,
                     const char *dir, const char *text) {
  size_t errline = 999;
  check_equal(salts_search_ignore_add_rules(&f->set, source, dir, strlen(dir),
                   text, strlen(text), &errline), SALTS_SEARCH_IGNORE_OK);
  check_equal(errline, 0u);
}

static void check_result(ignore_fixture *f, const char *path, bool directory,
                         salts_search_ignore_decision want) {
  salts_search_ignore_decision found = SALTS_SEARCH_IGNORE_INCLUDE;
  size_t rule = 0;
  check_equal(salts_search_ignore_evaluate(&f->set, path, strlen(path),
       directory, f->scratch, sizeof(f->scratch), &found, &rule),
       SALTS_SEARCH_IGNORE_OK);
  check_equal(found, want);
  check_true((rule == SIZE_MAX) == (want == SALTS_SEARCH_IGNORE_NONE));
}

spec("search A3 memory-only hierarchical ignore rules") {
  it("applies last-match-wins and negation from one rules file") {
    ignore_fixture f;
    init_fixture(&f);
    add_file(&f, SALTS_SEARCH_IGNORE_GITIGNORE, "",
             "# comment\n*.log\n!important.log\nimportant.log\n!important.log\n");
    check_result(&f, "src/error.log", false, SALTS_SEARCH_IGNORE_EXCLUDE);
    check_result(&f, "important.log", false, SALTS_SEARCH_IGNORE_INCLUDE);
    check_result(&f, "src/test.c", false, SALTS_SEARCH_IGNORE_NONE);
    salts_search_ignore_destroy(&f.set);
  }

  it("prevents a negated file from reappearing inside an excluded parent") {
    ignore_fixture f;
    init_fixture(&f);
    add_file(&f, SALTS_SEARCH_IGNORE_GITIGNORE, "",
             "build/\n!build/keep.txt\n");
    check_result(&f, "build", true, SALTS_SEARCH_IGNORE_EXCLUDE);
    check_result(&f, "build/keep.txt", false, SALTS_SEARCH_IGNORE_EXCLUDE);
    /* A rule that re-includes the parent directory allows a child exception. */
    add_file(&f, SALTS_SEARCH_IGNORE_GITIGNORE, "", "!build/\n");
    check_result(&f, "build", true, SALTS_SEARCH_IGNORE_INCLUDE);
    check_result(&f, "build/keep.txt", false, SALTS_SEARCH_IGNORE_INCLUDE);
    check_result(&f, "build/other.txt", false, SALTS_SEARCH_IGNORE_NONE);
    salts_search_ignore_destroy(&f.set);
  }

  it("uses nested scopes and higher directory depth within the same source") {
    ignore_fixture f;
    init_fixture(&f);
    add_file(&f, SALTS_SEARCH_IGNORE_GITIGNORE, "src", "*.tmp\n");
    add_file(&f, SALTS_SEARCH_IGNORE_GITIGNORE, "", "!*.tmp\n");
    check_result(&f, "src/a.tmp", false, SALTS_SEARCH_IGNORE_EXCLUDE);
    check_result(&f, "other/a.tmp", false, SALTS_SEARCH_IGNORE_INCLUDE);
    add_file(&f, SALTS_SEARCH_IGNORE_GITIGNORE, "src/nested", "!*.tmp\n");
    check_result(&f, "src/nested/a.tmp", false, SALTS_SEARCH_IGNORE_INCLUDE);
    check_result(&f, "src/nested/a.txt", false, SALTS_SEARCH_IGNORE_NONE);
    check_result(&f, "srcx/a.tmp", false, SALTS_SEARCH_IGNORE_INCLUDE);
    salts_search_ignore_destroy(&f.set);
  }

  it("prioritizes rgignore over ignore over gitignore over info over global") {
    ignore_fixture f;
    init_fixture(&f);
    add_file(&f, SALTS_SEARCH_IGNORE_GIT_GLOBAL, "", "*.o\n*.tmp\n");
    add_file(&f, SALTS_SEARCH_IGNORE_GIT_INFO, "", "!*.o\n");
    add_file(&f, SALTS_SEARCH_IGNORE_GITIGNORE, "src", "*.o\n");
    add_file(&f, SALTS_SEARCH_IGNORE_IGNORE, "", "!*.o\n");
    check_result(&f, "src/a.o", false, SALTS_SEARCH_IGNORE_INCLUDE);
    check_result(&f, "src/a.tmp", false, SALTS_SEARCH_IGNORE_EXCLUDE);
    add_file(&f, SALTS_SEARCH_IGNORE_RGIGNORE, "", "*.o\n");
    check_result(&f, "src/a.o", false, SALTS_SEARCH_IGNORE_EXCLUDE);
    salts_search_ignore_destroy(&f.set);
  }

  it("respects anchored, slash-relative, component and recursive patterns") {
    ignore_fixture f;
    init_fixture(&f);
    add_file(&f, SALTS_SEARCH_IGNORE_GITIGNORE, "src",
             "/root.log\nsub/deep.tmp\n**/test.data\n*.bak\n");
    check_result(&f, "src/root.log", false, SALTS_SEARCH_IGNORE_EXCLUDE);
    check_result(&f, "src/x/root.log", false, SALTS_SEARCH_IGNORE_NONE);
    check_result(&f, "src/sub/deep.tmp", false, SALTS_SEARCH_IGNORE_EXCLUDE);
    check_result(&f, "src/x/sub/deep.tmp", false, SALTS_SEARCH_IGNORE_NONE);
    check_result(&f, "src/test.data", false, SALTS_SEARCH_IGNORE_EXCLUDE);
    check_result(&f, "src/one/two/test.data", false, SALTS_SEARCH_IGNORE_EXCLUDE);
    check_result(&f, "src/x/y.bak", false, SALTS_SEARCH_IGNORE_EXCLUDE);
    check_result(&f, "another/y.bak", false, SALTS_SEARCH_IGNORE_NONE);
    salts_search_ignore_destroy(&f.set);
  }

  it("parses escaped leading markers and escaped trailing spaces") {
    ignore_fixture f;
    init_fixture(&f);
    add_file(&f, SALTS_SEARCH_IGNORE_GITIGNORE, "",
             "\\#hash\n\\!important\nwith\\  \nplain   \n#ignored\r\n\r\n");
    check_result(&f, "#hash", false, SALTS_SEARCH_IGNORE_EXCLUDE);
    check_result(&f, "!important", false, SALTS_SEARCH_IGNORE_EXCLUDE);
    check_result(&f, "with ", false, SALTS_SEARCH_IGNORE_EXCLUDE);
    check_result(&f, "with", false, SALTS_SEARCH_IGNORE_NONE);
    check_result(&f, "plain", false, SALTS_SEARCH_IGNORE_EXCLUDE);
    check_result(&f, "plain ", false, SALTS_SEARCH_IGNORE_NONE);
    salts_search_ignore_destroy(&f.set);
  }

  it("rolls back an entire new file on malformed patterns or too many rules") {
    ignore_fixture f;
    size_t badline = 0;
    size_t original;
    init_fixture(&f);
    add_file(&f, SALTS_SEARCH_IGNORE_GITIGNORE, "", "*.ok\n");
    original = f.set.count;
    check_equal(salts_search_ignore_add_rules(&f.set,
         SALTS_SEARCH_IGNORE_GITIGNORE, "src", 3u,
         "*.new\n[bad\n", 11u, &badline), SALTS_SEARCH_IGNORE_BAD_PATTERN);
    check_equal(badline, 2u);
    check_equal(f.set.count, original);
    check_result(&f, "a.ok", false, SALTS_SEARCH_IGNORE_EXCLUDE);
    check_result(&f, "src/a.new", false, SALTS_SEARCH_IGNORE_NONE);
    salts_search_ignore_destroy(&f.set);
  }

  it("allows UTF-8 bytes and ASCII folding without Unicode normalization") {
    ignore_fixture f;
    size_t errline = 0;
    init_fixture(&f);
    add_file(&f, SALTS_SEARCH_IGNORE_IGNORE, "", "\xE4\xB8\xAD\xE6\x96\x87/*.TMP\n");
    check_result(&f, "\xE4\xB8\xAD\xE6\x96\x87/a.TMP", false,
                 SALTS_SEARCH_IGNORE_EXCLUDE);
    check_result(&f, "\xE4\xB8\xAD\xE6\x96\x87/a.tmp", false,
                 SALTS_SEARCH_IGNORE_NONE);
    salts_search_ignore_destroy(&f.set);
    memset(&f, 0, sizeof(f));
    check_equal(salts_search_ignore_init(&f.set, f.rules, RULE_CAPACITY,
        PATTERN_CAPACITY, FILE_CAPACITY, PATH_CAPACITY, true),
        SALTS_SEARCH_IGNORE_OK);
    add_file(&f, SALTS_SEARCH_IGNORE_IGNORE, "", "HELLO/*.TMP\n");
    check_result(&f, "hello/a.tmp", false, SALTS_SEARCH_IGNORE_EXCLUDE);
    check_equal(errline, 0u);
    salts_search_ignore_destroy(&f.set);
  }

  it("uses explicit Git ignore source rank regardless of append order") {
    ignore_fixture f;
    init_fixture(&f);
    add_file(&f, SALTS_SEARCH_IGNORE_RGIGNORE, "", "!*.obj\n");
    add_file(&f, SALTS_SEARCH_IGNORE_IGNORE, "src/nested", "*.obj\n");
    add_file(&f, SALTS_SEARCH_IGNORE_GITIGNORE, "src/nested", "*.obj\n");
    check_result(&f, "src/nested/test.obj", false, SALTS_SEARCH_IGNORE_INCLUDE);
    check_result(&f, "src/nested/other.txt", false, SALTS_SEARCH_IGNORE_NONE);
    salts_search_ignore_destroy(&f.set);
  }

  it("enforces a fixed rule budget and rolls back after capacity exhaustion") {
    salts_search_ignore_rule slots[2] = {0};
    salts_search_ignore_set set = {0};
    size_t line = 0;
    unsigned char scratch[40];
    salts_search_ignore_decision decision = SALTS_SEARCH_IGNORE_NONE;
    size_t winner = SIZE_MAX;
    check_equal(salts_search_ignore_init(&set, slots, 2u, 16u,
               80u, 80u, false), SALTS_SEARCH_IGNORE_OK);
    check_equal(salts_search_ignore_add_rules(&set,
               SALTS_SEARCH_IGNORE_GITIGNORE, "", 0u,
               "*.bak\n", 6u, &line), SALTS_SEARCH_IGNORE_OK);
    check_equal(set.count, 1u);
    check_equal(salts_search_ignore_add_rules(&set,
               SALTS_SEARCH_IGNORE_GITIGNORE, "", 0u,
               "*.tmp\n*.obj\n", 12u, &line), SALTS_SEARCH_IGNORE_LIMIT);
    check_equal(line, 2u);
    check_equal(set.count, 1u);
    check_equal(salts_search_ignore_evaluate(&set, "file.bak", 8u, false,
               scratch, sizeof(scratch), &decision, &winner),
               SALTS_SEARCH_IGNORE_OK);
    check_equal(decision, SALTS_SEARCH_IGNORE_EXCLUDE);
    salts_search_ignore_destroy(&set);
  }

  it("preserves whitespace handling for escaped final space and final CR") {
    ignore_fixture f;
    init_fixture(&f);
    add_file(&f, SALTS_SEARCH_IGNORE_GITIGNORE, "", "name\\ \n");
    check_result(&f, "name ", false, SALTS_SEARCH_IGNORE_EXCLUDE);
    check_result(&f, "name", false, SALTS_SEARCH_IGNORE_NONE);
    add_file(&f, SALTS_SEARCH_IGNORE_GITIGNORE, "", "name\r");
    check_result(&f, "name\r", false, SALTS_SEARCH_IGNORE_EXCLUDE);
    salts_search_ignore_destroy(&f.set);
  }

  it("rejects invalid scope/paths, NUL rules and missing scratch") {
    ignore_fixture f;
    size_t line = 333u;
    salts_search_ignore_decision decision;
    size_t index;
    const char raw[] = {'x','\0','z'};
    init_fixture(&f);
    check_equal(salts_search_ignore_add_rules(&f.set,
                    SALTS_SEARCH_IGNORE_GITIGNORE, "../escape", 9u,
                    "*.x", 3u, &line), SALTS_SEARCH_IGNORE_INVALID);
    check_equal(salts_search_ignore_add_rules(&f.set,
                    SALTS_SEARCH_IGNORE_GITIGNORE, "", 0u,
                    raw, sizeof(raw), &line), SALTS_SEARCH_IGNORE_BAD_PATTERN);
    check_equal(line, 1u);
    check_equal(salts_search_ignore_add_rules(&f.set,
                    SALTS_SEARCH_IGNORE_GITIGNORE, "", 0u,
                    "!", 1u, &line), SALTS_SEARCH_IGNORE_BAD_PATTERN);
    check_equal(salts_search_ignore_evaluate(&f.set, "a/../b", 6u,
                    false, f.scratch, sizeof(f.scratch), &decision, &index),
                    SALTS_SEARCH_IGNORE_INVALID);
    check_equal(salts_search_ignore_evaluate(&f.set, "a", 1u,
                    false, f.scratch, 1u, &decision, &index),
                    SALTS_SEARCH_IGNORE_INVALID);
    salts_search_ignore_destroy(&f.set);
  }
}
