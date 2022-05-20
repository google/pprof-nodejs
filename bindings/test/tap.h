/**
 * @file tap.h
 * @author Stephen Belanger
 * @date Apr 13, 2022
 * @brief C and C++ API for TAP testing
 * 
 * @todo TODO directives
 * @todo YAML blocks?
 */

#ifndef _INCLUDE_TAP_H_
#define _INCLUDE_TAP_H_

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stdbool.h>

/**
 * @brief This is the TAP document every function will interact with
 */
typedef struct tap_s {
  FILE* out;               /**< FILE where TAP document will be written */
  int plan_count;          /**< Number of expected checks */
  int count;               /**< Number of checks made so far */
  int failures;            /**< Number of failures so far */
  int skip_count;          /**< Number of future checks to skip */
  int skipped;             /**< Number of checks skipped so far */
  int indent;              /**< Indentation level for sub-tests */
  const char* skip_reason; /**< Reason to report for future skipped checks */
  void* data;              /**< Free pointer slot to pass data into sub-tests */
} tap_t;

/**
 * @brief Sub-test function signature
 * 
 * @param t TAP document
 */
typedef void (*tap_test_fn)(tap_t* t);

/**
 * @private
 * @brief Print indentation level for sub-tests
 * @note Internal use only!
 * 
 * @param t TAP document
 */
static inline void _tap_print_indent(tap_t* t) {
  for (int i = 0; i < t->indent; i++) {
    fprintf(t->out, " ");
  }
}

/**
 * @private
 * @brief This is separated to prevent sub-tests from re-printing TAP version.
 * @note Internal use only!
 *
 * @param t TAP document
 */
static inline tap_t _tap_init(FILE* out, int indent) {
  return {
    out, // out
    0, // plan_count
    0, // count
    0, // failures
    0, // skip_count
    0, // skipped
    indent,
    NULL, // skip_reason
    NULL // data
  };
}

/**
 * @brief Creates a TAP document writing to the given FILE.
 *
 * ```c
 * tap_t t = tap(stdout);
 * // or...
 * tap_t t = tap(fopen("out.tap", "w"));
 * ```
 *
 * ```tap
 * TAP version 13
 * ```
 *
 * @param out FILE to which the TAP document will be written
 * @return tap_t
 */
static inline tap_t tap(FILE* out) {
  fprintf(out, "TAP version 13\n");
  return _tap_init(out, 0);
}

/**
 * @brief Enable given pragma key
 *
 * ```c
 * tap_on(&t, "strict");
 * ```
 * 
 * ```tap
 * pragma +strict
 * ```
 *
 * @param t TAP document
 * @param pragma Key to enable
 */
static inline void tap_on(tap_t* t, const char* pragma) {
  _tap_print_indent(t);
  fprintf(t->out, "pragma +%s\n", pragma);
}

/**
 * @brief Disable given pragma key
 *
 * ```c
 * tap_off(&t, "strict");
 * ```
 *
 * ```tap
 * pragma -strict
 * ```
 *
 * @param t TAP document
 * @param pragma Key to disable
 */
static inline void tap_off(tap_t* t, const char* pragma) {
  _tap_print_indent(t);
  fprintf(t->out, "pragma -%s\n", pragma);
}

/**
 * @brief Bail out of the test
 *
 * ```c
 * tap_bail_out(&t, "Oh no!");
 * ```
 * 
 * ```tap
 * Bail out! Oh No!
 * ```
 *
 * @param t TAP document
 * @param reason Reason to record to TAP document for bailing out
 */
static inline void tap_bail_out(tap_t* t, const char* reason) {
  fprintf(t->out, "Bail out! %s\n", reason);
  exit(1);
}

/**
 * @brief Inform the TAP document to expect a set number of test completions
 * when ended.
 * 
 * Setting the plan multiple times is invalid. If not specified it will
 * be set automatically from the total test count when the test is ended.
 *
 * ```c
 * tap_plan(&t, 123);
 * ```
 *
 * ```tap
 * 1..123
 * ```
 *
 * @param t TAP document
 * @param n number of expected checks
 */
static inline void tap_plan(tap_t* t, int n) {
  if (t->plan_count != 0) {
    tap_bail_out(t, "setting the plan multiple times is invalid");
    return;
  }
  t->plan_count = n;
  _tap_print_indent(t);
  fprintf(t->out, "1..%d\n", n);
}

/**
 * @brief Add a comment line to the tap document. Useful for separating groups
 * of tests too small to warrant splitting out to a fully separate test block.
 *
 * ```c
 * tap_comment(&t, "my section");
 * ```
 *
 * ```tap
 * # my section
 * ```
 *
 * @param t TAP document
 * @param comment Comment to write to the TAP document
 */
static inline void tap_comment(tap_t* t, const char* comment) {
  _tap_print_indent(t);
  fprintf(t->out, "# %s\n", comment);
}

/**
 * @brief Skips the next n tests.
 *
 * ```c
 * tap_skip_n(&t, 1, "unimplemented");
 * tap_pass(&t, "not done yet");
 * // or...
 * tap_skip_n(&t, 2);
 * tap_pass(&t, "just skipping");
 * tap_pass(&t, "also skipping");
 * ```
 *
 * ```tap
 * ok 1 - not done yet # SKIP unimplemented
 * ok 2 - just skipping # SKIP
 * ok 3 - also skipping # SKIP
 * ```
 *
 * @param t TAP document
 * @param n Number of checks to skip
 * @param reason Optional reason for skipping
 */
static inline void tap_skip_n(tap_t* t, int n, const char* reason) {
  if (t->skip_count > 0) {
    tap_bail_out(t, "only one skip task may be active");
    return;
  }
  t->skip_count = n;
  t->skip_reason = reason;
}

/**
 * @brief Skips the next test.
 *
 * ```c
 * tap_skip(&t, "unimplemented");
 * tap_pass(&t, "not done yet");
 * // or...
 * tap_skip(&t);
 * tap_pass(&t, "just skipping");
 * ```
 * 
 * ```tap
 * ok 1 - not done yet # SKIP unimplemented
 * ok 2 - just skipping # SKIP
 * ```
 *
 * @param t TAP document
 * @param reason Optional reason for skipping
 */
static inline void tap_skip(tap_t* t, const char* reason, ...) {
  tap_skip_n(t, 1, reason);
}

/**
 * @internal
 * Hack to make reason optional
 */
#define tap_skip(t, ...) \
  tap_skip(t, ##__VA_ARGS__, NULL)

/**
 * @brief Check if a given value is truthy.
 *
 * All other check types are sugar around this one.
 *
 * ```c
 * tap_ok(&t, true, "it's true");
 * // or...
 * tap_ok(&t, false);
 * ```
 *
 * ```tap
 * ok 1 - it's true
 * not ok 2
 * ```
 *
 * @param t TAP document
 * @param pass Mark if the checked value is truthy
 * @param description Optional description of what was checked
 */
static inline void tap_ok(tap_t* t, bool pass, const char* description, ...) {
  const char* skip_reason = t->skip_reason;
  int skip_count = t->skip_count;

  if (t->skip_count) {
    t->skip_count--;
    t->skipped++;
    if (!t->skip_count) {
      t->skip_reason = NULL;
    }
  } else {
    t->count++;
    if (!pass) {
      t->failures++;
    }
  }
  // Status information
  _tap_print_indent(t);
  if (!pass) fprintf(t->out, "not ");
  fprintf(t->out, "ok %d", t->count);
  // Optional message
  if (description != NULL && strlen(description)) {
    fprintf(t->out, " - %s", description);
  }
  // Directive
  if (skip_reason != NULL && strlen(skip_reason)) {
    fprintf(t->out, " # SKIP %s", skip_reason);
  } else if (skip_count > 0) {
    fprintf(t->out, " # SKIP");
  }
  fprintf(t->out, "\n");
}

/**
 * @internal
 * Hack to make descriptions optional
 */
#define tap_ok(t, value, ...) \
  tap_ok(t, value, ##__VA_ARGS__, "")

/**
 * @brief Check if the value is falsy.
 *
 * ```c
 * tap_not_ok(&t, true, "it is falsy");
 * // or...
 * tap_not_ok(&t, false);
 * ```
 *
 * ```tap
 * not ok 1 - it is falsy
 * ok 2
 * ```
 *
 * @param t TAP document
 * @param value Value to check if it is falsy
 * @param description Optional description of what was checked
 */
#define tap_not_ok(t, value, ...) \
  tap_ok(t, !((bool)value), __VA_ARGS__)

/**
 * @brief Mark a passed check.
 *
 * ```c
 * tap_pass(&t, "it passed");
 * // or...
 * tap_pass(&t);
 * ```
 *
 * ```tap
 * ok 1 - it passed
 * ok 2
 * ```
 *
 * @param t TAP document
 * @param description Optional description of what was checked
 */
#define tap_pass(t, ...) \
  tap_ok(t, true, __VA_ARGS__)

/**
 * @brief Mark a failed check.
 *
 * ```c
 * tap_fail(&t, "it failed");
 * // or...
 * tap_fail(&t);
 * ```
 *
 * ```tap
 * not ok 1 - it failed
 * not ok 2
 * ```
 *
 * @param t TAP document
 * @param description Optional description of what was checked
 */
#define tap_fail(t, ...) \
  tap_ok(t, false, __VA_ARGS__)

/**
 * @brief Check if values are equal.
 *
 * ```c
 * tap_equal(&t, a, b, "values are equal");
 * // or...
 * tap_equal(&t, a, b);
 * ```
 *
 * ```tap
 * ok 1 - values are equal
 * ok 2
 * ```
 *
 * @param t TAP document
 * @param a First value to compare
 * @param b Second balue to compare
 * @param description Optional description of what was checked
 */
#define tap_equal(t, a, b, ...) \
  tap_ok(t, a == b, __VA_ARGS__)

/**
 * @brief Check if values are not equal.
 *
 * ```c
 * tap_not_equal(&t, a, b, "values are not equal");
 * // or...
 * tap_not_equal(&t, a, b);
 * ```
 *
 * ```tap
 * ok 1 - values are equal
 * ok 2
 * ```
 *
 * @param t TAP document
 * @param a First value to compare
 * @param b Second balue to compare
 * @param description Optional description of what was checked
 */
#define tap_not_equal(t, a, b, ...) \
  tap_ok(t, a != b, __VA_ARGS__)

/**
 * @brief End the test document. Will record the plan range if not already set.
 *
 * ```c
 * tap_t t = tap(stdout);
 * tap_pass(&t, "yay!");
 * tap_end(&t);
 * ```
 *
 * ```tap
 * TAP version 13
 * ok 1 - yay!
 * 1..1
 * ```
 *
 * @param t TAP document
 * @return int Return 1 if unskipped failures or count does not match plan
 */
static inline int tap_end(tap_t* t) {
  if (t->plan_count == 0) {
    tap_plan(t, t->count + t->skipped);
  }
  if (t->failures > 0) return 1;
  if ((t->count + t->skipped) != t->plan_count) return 1;
  return 0;
}

/**
 * @brief Add a named sub-test
 *
 * ```c
 * void sub_test(tap_t* t) {
 *   tap_pass(t, "it passed");
 * }
 *
 * tap_test(&t, "sub-test", sub_test);
 * ```
 *
 * ```tap
 * # Subtest: sub-test
 *     ok 1 - it passed
 *     1..1
 * ok 1 - sub-test
 * ```
 *
 * @param t TAP document
 * @param name Test name
 * @param fn Test function
 * @param ptr Optional pointer to attach to tap_t given to test function
 */
static inline void tap_test(tap_t *t, const char *name, tap_test_fn fn, void *ptr, ...) {
  _tap_print_indent(t);
  fprintf(t->out, "# Subtest: %s\n", name);
  tap_t t2 = _tap_init(t->out, t->indent + 4);
  t2.data = ptr;
  fn(&t2);
  tap_ok(t, tap_end(&t2) == 0, name);
}

/**
 * @internal
 * Hack to make ptr optional
 */
#define tap_test(t, name, fn, ...) \
  tap_test(t, name, fn, ##__VA_ARGS__, NULL)

#if __cplusplus >= 201103L || (defined(_MSC_VER) && _MSC_VER >= 1900)

#include <string>
#include <functional>

/**
 * @brief This is a TAP document
 */
class Tap : private tap_t {
 public:
  /**
   * @brief Construct a new TAP document
   * 
   * @param out FILE stream to write TAP document to. Defaults to stdout.
   */
  Tap(FILE* out = stdout) : tap_t(tap(out)) {}

  /**
   * @brief Enable given pragma key
   *
   * ```cpp
   * t.on(&t, "strict");
   * ```
   *
   * ```tap
   * pragma +strict
   * ```
   *
   * @param pragma Key to enable
   */
  void on(const std::string& pragma) {
    tap_on(this, pragma.c_str());
  }

  /**
   * @brief Disable given pragma key
   *
   * ```cpp
   * t.off(&t, "strict");
   * ```
   *
   * ```tap
   * pragma -strict
   * ```
   *
   * @param pragma Key to disable
   */
  void off(const std::string& pragma) {
    tap_off(this, pragma.c_str());
  }

  /**
   * @brief Bail out of the test
   *
   * ```cpp
   * t.bail_out("Oh no!");
   * ```
   *
   * ```tap
   * Bail out! Oh No!
   * ```
   *
   * @param reason Reason to record to TAP document for bailing out
   */
  void bail_out(const std::string& reason = "") {
    tap_bail_out(this, reason.c_str());
  }

  /**
   * @brief Expect a set number of test completions when ended.
   *
   * ```cpp
   * t.plan(123);
   * ```
   * 
   * ```tap
   * 1..123
   * ```
   *
   * @param n number of expected checks
   */
  void plan(int n) {
    tap_plan(this, n);
  }

  /**
   * @brief Add a comment line to the tap document. Useful for separating
   * groups of tests too small to warrant splitting out to a fully separate
   * test block.
   *
   * ```cpp
   * t.comment("my section");
   * ```
   *
   * ```tap
   * # my section
   * ```
   *
   * @param comment Comment to write to the TAP document
   */
  void comment(const std::string& comment) {
    tap_comment(this, comment.c_str());
  }

  /**
   * @brief Skips the next n tests.
   *
   * ```cpp
   * t.skip(1, "unimplemented");
   * t.pass("not done yet");
   * // or...
   * t.skip(2);
   * t.pass("just skipping");
   * t.pass("also skipping");
   * ```
   *
   * ```tap
   * ok 1 - not done yet # SKIP unimplemented
   * ok 2 - just skipping # SKIP
   * ok 3 - also skipping # SKIP
   * ```
   *
   * @param n Number of checks to skip
   * @param reason Optional reason for skipping
   */
  void skip(int n, const std::string& reason = "") {
    tap_skip_n(this, n, reason.c_str());
  }

  /**
   * @brief Skips the next test.
   *
   * ```cpp
   * t.skip("unimplemented");
   * t.pass("not done yet");
   * // or...
   * t.skip();
   * t.pass("just skipping");
   * ```
   *
   * ```tap
   * ok 1 - not done yet # SKIP unimplemented
   * ok 2 - just skipping # SKIP
   * ```
   *
   * @param reason Optional reason for skipping
   */
  void skip(const std::string& reason = "") {
    skip(1, reason);
  }

  /**
   * @brief Check if a given value is truthy.
   *
   * All other check types are sugar around this one.
   *
   * ```cpp
   * t.ok(true, "it's true");
   * // or...
   * t.ok(false);
   * ```
   *
   * ```tap
   * ok 1 - it's true
   * not ok 2
   * ```
   *
   * @param pass Mark if the checked value is truthy
   * @param description Optional description of what was checked
   */
  template<typename T>
  void ok(T pass, const std::string& description = "") {
    tap_ok(this, pass, description.c_str());
  }

  /**
   * @brief Check if the value is falsy.
   *
   * ```cpp
   * t.not_ok(true, "it is falsy");
   * // or...
   * t.not_ok(false);
   * ```
   *
   * ```tap
   * not ok 1 - it is falsy
   * ok 2
   * ```
   *
   * @param value Value to check if it is falsy
   * @param description Optional description of what was checked
   */
  template<typename T>
  void not_ok(T value, const std::string& description = "") {
    ok(!value, description);
  }

  /**
   * @brief Mark a passed check.
   *
   * ```cpp
   * t.pass("it passed");
   * // or...
   * t.pass();
   * ```
   *
   * ```tap
   * ok 1 - it passed
   * ok 2
   * ```
   *
   * @param description Optional description of what was checked
   */
  void pass(const std::string& description = "") {
    ok(true, description);
  }

  /**
   * @brief Mark a failed check.
   *
   * ```cpp
   * t.fail("it failed");
   * // or...
   * t.fail();
   * ```
   *
   * ```tap
   * not ok 1 - it failed
   * not ok 2
   * ```
   *
   * @param description Optional description of what was checked
   */
  void fail(const std::string& description = "") {
    ok(false, description);
  }

  /**
   * @brief Check if values are equal.
   *
   * ```cpp
   * t.equal(a, b, "values are equal");
   * // or...
   * t.equal(a, b);
   * ```
   *
   * ```tap
   * ok 1 - values are equal
   * ok 2
   * ```
   *
   * @param a First value to compare
   * @param b Second balue to compare
   * @param description Optional description of what was checked
   */
  template<typename A, typename B>
  void equal(A a, B b, const std::string& description = "") {
    ok(a == b, description);
  }

  /**
   * @brief Check if values are not equal.
   *
   * ```cpp
   * t.not_equal(a, b, "values are not equal");
   * // or...
   * t.not_equal(a, b);
   * ```
   *
   * ```tap
   * ok 1 - values are equal
   * ok 2
   * ```
   *
   * @param a First value to compare
   * @param b Second balue to compare
   * @param description Optional description of what was checked
   */
  template<typename A, typename B>
  void not_equal(A a, B b, const std::string& description = "") {
    ok(a != b, description);
  }

  /**
   * @brief End the test document. Will record the plan range if not already set.
   *
   * ```cpp
   * Tap t;
   * t.pass("yay!");
   * t.end();
   * ```
   *
   * ```tap
   * TAP version 13
   * ok 1 - yay!
   * 1..1
   * ```
   *
   * @return int Return 1 if unskipped failures or count does not match plan
   */
  int end() {
    return tap_end(this);
  }

  /**
   * @brief Add a named sub-test
   *
   * ```cpp
   * t.test("sub-test", [](Tap& t) {
   *   tap_pass(t, "it passed");
   * });
   * ```
   *
   * ```tap
   * # Subtest: sub-test
   *     ok 1 - it passed
   *     1..1
   * ok 1 - sub-test
   * ```
   *
   * @param name Test name
   * @param fn Test function
   */
  void test(const std::string& name, std::function<void(Tap&)> fn) {
    struct callback {
      std::function<void(Tap&)> fn;
    };
    callback wrap = {fn};
    tap_test(this, name.c_str(), [](tap_t* t) {
      callback* wrap = static_cast<callback*>(t->data);
      wrap->fn(*static_cast<Tap*>(t));
    }, &wrap);
  }
};

#endif // __cplusplus >= 201103L

#endif // _INCLUDE_TAP_H_
