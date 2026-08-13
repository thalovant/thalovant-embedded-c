#include <stdio.h>

int tlv_test_checks = 0;
int tlv_test_failures = 0;

void tlv_test_json(void);
void tlv_test_codec(void);
void tlv_test_crypto(void);
void tlv_test_identity(void);
void tlv_test_topics(void);

int main(void)
{
    tlv_test_json();
    tlv_test_codec();
    tlv_test_crypto();
    tlv_test_identity();
    tlv_test_topics();
    printf("%d checks, %d failures\n", tlv_test_checks, tlv_test_failures);
    return tlv_test_failures == 0 ? 0 : 1;
}
