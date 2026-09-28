#ifndef DATA_BIND_NATIVE_TEST_ALIGNMENT_H
#define DATA_BIND_NATIVE_TEST_ALIGNMENT_H

/* Portable fundamental alignment carrier for C test workspaces.
 * MSVC's C headers do not provide max_align_t consistently. */
typedef union DataBindNativeTestAlignment {
  long double long_double;
  void *pointer;
  long long integer;
} DataBindNativeTestAlignment;

#endif
