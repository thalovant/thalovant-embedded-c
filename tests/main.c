#include <stdio.h>

int tlv_test_checks = 0;
int tlv_test_failures = 0;

void tlv_test_version(void);
void tlv_test_json(void);
void tlv_test_codec(void);
void tlv_test_crypto(void);
void tlv_test_identity(void);
void tlv_test_topics(void);
void tlv_test_wire(void);
void tlv_test_ask(void);
void tlv_test_intents(void);

int main(void)
{
    tlv_test_version();
    tlv_test_json();
    tlv_test_codec();
    tlv_test_crypto();
    tlv_test_identity();
    tlv_test_topics();
    tlv_test_wire();
    tlv_test_ask();
    tlv_test_intents();
    printf("%d checks, %d failures\n", tlv_test_checks, tlv_test_failures);
    return tlv_test_failures == 0 ? 0 : 1;
}
