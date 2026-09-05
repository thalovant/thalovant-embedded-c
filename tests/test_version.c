/*
 * Pins the canonical library version, mirroring the version/user-agent
 * constant pinning tests in the sibling Thalovant SDKs: the expected
 * literal below is rewritten by .github/workflows/auto-release.yml, so a
 * version bump that misses one of the copies fails the suite.
 *
 * The VERSION file check reads from the current directory; `make test`
 * runs the suite from the repository root.
 */
#include <stdio.h>
#include <string.h>

#include "thalovant/thalovant.h"

#include "harness.h"

#define TLV_EXPECTED_VERSION "0.2.0"

void tlv_test_version(void)
{
    FILE *fp;

    CHECK_STR_EQ(THALOVANT_VERSION, TLV_EXPECTED_VERSION);
    CHECK_STR_EQ(THALOVANT_EMBEDDED_C_VERSION, TLV_EXPECTED_VERSION);

    fp = fopen("VERSION", "r");
    CHECK(fp != NULL);
    if (fp != NULL) {
        char contents[32] = {0};
        size_t len = fread(contents, 1, sizeof(contents) - 1, fp);
        fclose(fp);
        while (len > 0 && (contents[len - 1] == '\n' || contents[len - 1] == '\r')) {
            contents[--len] = '\0';
        }
        CHECK_STR_EQ(contents, THALOVANT_VERSION);
    }
}
