/*
 * Canonical version of the Thalovant embedded C client library.
 *
 * This define is the single source of truth for the library version. The
 * `VERSION` file at the repository root, the pinned literal in
 * `tests/test_version.c`, the `v<version>` references in `README.md`, and
 * `CHANGELOG.md` must move together in a release (see RELEASING.md).
 *
 * The auto-release workflow rewrites the define below textually, so keep
 * it on a single line in exactly this form.
 */
#ifndef THALOVANT_VERSION_H
#define THALOVANT_VERSION_H

#define THALOVANT_VERSION "0.2.0"

#endif /* THALOVANT_VERSION_H */
