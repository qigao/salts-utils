#include "search_policy.h"
#include <tinytest.h>

#include <stdint.h>
#include <string.h>

enum { TEST_SLOTS = 20, TEST_PATTERN = 96, TEST_PATH = 192 };

typedef struct policy_fixture {
  salts_search_override_rule overrides[TEST_SLOTS];
  salts_search_override_set override_set;
  salts_search_ignore_rule ignores[TEST_SLOTS];
  salts_search_ignore_set ignore_set;
  unsigned char scratch[2 * (TEST_PATTERN + 1)];
  salts_search_policy_options options;
} policy_fixture;

static void fixture_init(policy_fixture *f) {
  memset(f, 0, sizeof(*f));
  check_equal(salts_search_override_init(&f->override_set, f->overrides,
      TEST_SLOTS, TEST_PATTERN, TEST_PATH, false), SALTS_SEARCH_POLICY_OK);
  check_equal(salts_search_ignore_init(&f->ignore_set, f->ignores,
      TEST_SLOTS, TEST_PATTERN, 512u, TEST_PATH, false),
      SALTS_SEARCH_IGNORE_OK);
  f->options.max_path_bytes = TEST_PATH;
  f->options.include_hidden = false;
}

static void fixture_dispose(policy_fixture *f) {
  salts_search_override_destroy(&f->override_set);
  salts_search_ignore_destroy(&f->ignore_set);
}

static void add_override(policy_fixture *f, const char *pattern) {
  check_equal(salts_search_override_add(&f->override_set, pattern,
       strlen(pattern)), SALTS_SEARCH_POLICY_OK);
}

static void add_ignore(policy_fixture *f, const char *scope,
                       const char *pattern) {
  size_t bad = SIZE_MAX;
  check_equal(salts_search_ignore_add_rules(&f->ignore_set,
      SALTS_SEARCH_IGNORE_GITIGNORE, scope, strlen(scope),
      pattern, strlen(pattern), &bad), SALTS_SEARCH_IGNORE_OK);
  check_equal(bad, 0u);
}

static void verify(policy_fixture *f, const char *path, bool is_dir,
                   salts_search_policy_decision decision,
                   salts_search_policy_reason reason) {
  salts_search_policy_result actual = {0};
  check_equal(salts_search_policy_evaluate(&f->override_set, &f->ignore_set,
     &f->options, path, strlen(path), is_dir, f->scratch,
     sizeof(f->scratch), &actual), SALTS_SEARCH_POLICY_OK);
  check_equal(actual.decision, decision);
  check_equal(actual.reason, reason);
}

spec("search policy: rg/fd-like override and hidden rules without FS") {
  it("skips hidden files and prunes hidden parent directories") {
    policy_fixture f;
    fixture_init(&f);
    verify(&f, "src/main.c", false, SALTS_SEARCH_POLICY_VISIT,
           SALTS_SEARCH_POLICY_REASON_NONE);
    verify(&f, ".git", true, SALTS_SEARCH_POLICY_PRUNE_DIRECTORY,
           SALTS_SEARCH_POLICY_REASON_HIDDEN);
    verify(&f, "src/.private", true, SALTS_SEARCH_POLICY_PRUNE_DIRECTORY,
           SALTS_SEARCH_POLICY_REASON_HIDDEN);
    verify(&f, "src/.private/main.c", false,
           SALTS_SEARCH_POLICY_PRUNE_DIRECTORY,
           SALTS_SEARCH_POLICY_REASON_HIDDEN);
    f.options.include_hidden = true;
    verify(&f, "src/.private/main.c", false, SALTS_SEARCH_POLICY_VISIT,
           SALTS_SEARCH_POLICY_REASON_NONE);
    fixture_dispose(&f);
  }

  it("explicit -g positive whitelist excludes unmatched FILES not directories") {
    policy_fixture f;
    fixture_init(&f);
    add_override(&f, "*.c");
    verify(&f, "src", true, SALTS_SEARCH_POLICY_VISIT,
           SALTS_SEARCH_POLICY_REASON_NONE);
    verify(&f, "src/main.c", false, SALTS_SEARCH_POLICY_VISIT,
           SALTS_SEARCH_POLICY_REASON_OVERRIDE);
    verify(&f, "src/main.h", false, SALTS_SEARCH_POLICY_SKIP_FILE,
           SALTS_SEARCH_POLICY_REASON_IMPLICIT_OVERRIDE);
    verify(&f, "src/.private.c", false, SALTS_SEARCH_POLICY_VISIT,
           SALTS_SEARCH_POLICY_REASON_OVERRIDE);
    verify(&f, "src/.private", true, SALTS_SEARCH_POLICY_PRUNE_DIRECTORY,
           SALTS_SEARCH_POLICY_REASON_HIDDEN);
    verify(&f, "src/.private/a.c", false,
           SALTS_SEARCH_POLICY_PRUNE_DIRECTORY,
           SALTS_SEARCH_POLICY_REASON_HIDDEN);
    fixture_dispose(&f);
  }

  it("negative override is exclusion and last matching CLI override wins") {
    policy_fixture f;
    fixture_init(&f);
    add_override(&f, "!*.c");
    verify(&f, "a.c", false, SALTS_SEARCH_POLICY_SKIP_FILE,
           SALTS_SEARCH_POLICY_REASON_OVERRIDE);
    verify(&f, "a.h", false, SALTS_SEARCH_POLICY_VISIT,
           SALTS_SEARCH_POLICY_REASON_NONE);
    add_override(&f, "*.c");
    verify(&f, "a.c", false, SALTS_SEARCH_POLICY_VISIT,
           SALTS_SEARCH_POLICY_REASON_OVERRIDE);
    verify(&f, "a.txt", false, SALTS_SEARCH_POLICY_SKIP_FILE,
           SALTS_SEARCH_POLICY_REASON_IMPLICIT_OVERRIDE);
    add_override(&f, "!a.c");
    verify(&f, "a.c", false, SALTS_SEARCH_POLICY_SKIP_FILE,
           SALTS_SEARCH_POLICY_REASON_OVERRIDE);
    fixture_dispose(&f);
  }

  it("explicit positive override wins ignore and hidden; negative wins whitelist") {
    policy_fixture f;
    fixture_init(&f);
    add_ignore(&f, "", "*.c\n!a.h\n");
    add_override(&f, "*.c");
    verify(&f, "src/a.c", false, SALTS_SEARCH_POLICY_VISIT,
           SALTS_SEARCH_POLICY_REASON_OVERRIDE);
    verify(&f, "src/a.h", false, SALTS_SEARCH_POLICY_SKIP_FILE,
           SALTS_SEARCH_POLICY_REASON_IMPLICIT_OVERRIDE);
    add_override(&f, "!bad.c");
    verify(&f, "src/bad.c", false, SALTS_SEARCH_POLICY_SKIP_FILE,
           SALTS_SEARCH_POLICY_REASON_OVERRIDE);
    fixture_dispose(&f);
  }

  it("ignore whitelist bypasses hidden policy and ancestor exclusions") {
    policy_fixture f;
    fixture_init(&f);
    add_ignore(&f, "", "!.secret/\n!.secret/*.h\n");
    verify(&f, ".secret", true, SALTS_SEARCH_POLICY_VISIT,
           SALTS_SEARCH_POLICY_REASON_IGNORE);
    verify(&f, ".secret/a.h", false, SALTS_SEARCH_POLICY_VISIT,
           SALTS_SEARCH_POLICY_REASON_IGNORE);
    verify(&f, ".secret/.deep", true, SALTS_SEARCH_POLICY_PRUNE_DIRECTORY,
           SALTS_SEARCH_POLICY_REASON_HIDDEN);
    verify(&f, ".secret/.deep/a.h", false,
           SALTS_SEARCH_POLICY_PRUNE_DIRECTORY,
           SALTS_SEARCH_POLICY_REASON_HIDDEN);
    fixture_dispose(&f);
  }

  it("directory whitelist does not override hidden descendants") {
    policy_fixture f;
    fixture_init(&f);
    add_override(&f, ".secret/");
    verify(&f, ".secret", true, SALTS_SEARCH_POLICY_VISIT,
           SALTS_SEARCH_POLICY_REASON_OVERRIDE);
    verify(&f, ".secret/.deep", true, SALTS_SEARCH_POLICY_PRUNE_DIRECTORY,
           SALTS_SEARCH_POLICY_REASON_HIDDEN);
    verify(&f, ".secret/.deep/a.c", false,
           SALTS_SEARCH_POLICY_PRUNE_DIRECTORY,
           SALTS_SEARCH_POLICY_REASON_HIDDEN);
    fixture_dispose(&f);
  }

  it("excluded parent is pruned even if child override would include it") {
    policy_fixture f;
    fixture_init(&f);
    add_ignore(&f, "", "build/\n");
    add_override(&f, "*.c");
    verify(&f, "build", true, SALTS_SEARCH_POLICY_PRUNE_DIRECTORY,
           SALTS_SEARCH_POLICY_REASON_IGNORE);
    verify(&f, "build/foo.c", false,
           SALTS_SEARCH_POLICY_PRUNE_DIRECTORY,
           SALTS_SEARCH_POLICY_REASON_IGNORE);
    add_override(&f, "build/");
    verify(&f, "build", true, SALTS_SEARCH_POLICY_VISIT,
           SALTS_SEARCH_POLICY_REASON_OVERRIDE);
    verify(&f, "build/foo.c", false, SALTS_SEARCH_POLICY_VISIT,
           SALTS_SEARCH_POLICY_REASON_OVERRIDE);
    fixture_dispose(&f);
  }

  it("directory exclusion overrides prune explicitly without hidden fallback") {
    policy_fixture f;
    fixture_init(&f);
    add_override(&f, "!build/");
    verify(&f, "work/build", true, SALTS_SEARCH_POLICY_PRUNE_DIRECTORY,
           SALTS_SEARCH_POLICY_REASON_OVERRIDE);
    verify(&f, "work/build/a.txt", false,
           SALTS_SEARCH_POLICY_PRUNE_DIRECTORY,
           SALTS_SEARCH_POLICY_REASON_OVERRIDE);
    verify(&f, "work/other/a.txt", false, SALTS_SEARCH_POLICY_VISIT,
           SALTS_SEARCH_POLICY_REASON_NONE);
    fixture_dispose(&f);
  }

  it("rejects unsafe paths, missing capacity and malformed override programs") {
    policy_fixture f;
    salts_search_policy_result out = {0};
    fixture_init(&f);
    check_equal(salts_search_override_add(&f.override_set, "!", 1u),
                SALTS_SEARCH_POLICY_BAD_GLOB);
    check_equal(salts_search_override_add(&f.override_set, "[a", 2u),
                SALTS_SEARCH_POLICY_BAD_GLOB);
    check_equal(f.override_set.count, 0u);
    check_equal(salts_search_policy_evaluate(&f.override_set, &f.ignore_set,
       &f.options, "../x", 4u, false, f.scratch, sizeof(f.scratch), &out),
       SALTS_SEARCH_POLICY_INVALID);
    check_equal(salts_search_policy_evaluate(&f.override_set, &f.ignore_set,
       &f.options, "/x", 2u, false, f.scratch, sizeof(f.scratch), &out),
       SALTS_SEARCH_POLICY_INVALID);
    check_equal(salts_search_policy_evaluate(&f.override_set, &f.ignore_set,
       &f.options, "good", 4u, false, f.scratch, 1u, &out),
       SALTS_SEARCH_POLICY_LIMIT);
    check_equal(salts_search_policy_evaluate(&f.override_set, &f.ignore_set,
       &f.options, "good", 4u, false, f.scratch, sizeof(f.scratch), &out),
       SALTS_SEARCH_POLICY_OK);
    check_equal(out.decision, SALTS_SEARCH_POLICY_VISIT);
    fixture_dispose(&f);
  }
}
