#include "harness.h"
#include "thalovant/thalovant.h"
#include <stdint.h>
#include <math.h>
#include "fixtures/request-helpers-node.h"

/* Reference cases from Python 0.6.3 / Node's coordinated parity candidate. */
static void patterns(void)
{
    const thalovant_speakable_slot slots[] = {{"item_name","the time"}};
    for (size_t i = 0; i < sizeof(REQUEST_PATTERNS)/sizeof(REQUEST_PATTERNS[0]); i++) {
        char output[256];
        CHECK(thalovant_speakable(REQUEST_PATTERNS[i],slots,1,output,sizeof(output)) >= 0);
        CHECK_STR_EQ(output,REQUEST_SPOKEN[i]);
    }
    char tiny[5];
    CHECK_INT_EQ(thalovant_speakable("{x}",(thalovant_speakable_slot[]){{"x","too long"}},1,tiny,sizeof(tiny)),THALOVANT_ERR_NOMEM);
    char empty[4];
    CHECK_INT_EQ(thalovant_speakable("{x}",(thalovant_speakable_slot[]){{"x",""}},1,empty,sizeof(empty)),0);
    CHECK_STR_EQ(empty,"");
}
static void location_and_hints(void)
{
    thalovant_location location = {" Montréal "," QC "," ca "," America/Toronto ",true,45.5,-73.5};
    char place[512],output[2048];
    CHECK(thalovant_build_location(&location,place,sizeof(place)) > 0);
    CHECK_STR_EQ(place,"{\"city\":\"Montréal\",\"region\":\"QC\",\"country_code\":\"CA\",\"timezone\":{\"code\":\"America/Toronto\"},\"coordinate\":{\"latitude\":45.5,\"longitude\":-73.5}}");
    thalovant_ask_request request = {"weather",NULL,"s",NULL,"r"};
    thalovant_ask_hints hints = {" fr-ca ","[\" \",\" intent \" ]",place};
    CHECK(thalovant_ask_build_frame_with_hints(&request,&hints,output,sizeof(output)) > 0);
    CHECK(strstr(output,"\"stt_lang\":\"fr-ca\"") != NULL);
    CHECK(strstr(output,"\"pipeline\":[\"intent\"]") != NULL);
    CHECK(strstr(output,"\"location\":{\"city\":\"Montréal\"") != NULL);
    hints.pipeline_json="[\" a\\u0000b \" ]";
    CHECK(thalovant_ask_build_frame_with_hints(&request,&hints,output,sizeof(output)) > 0);
    CHECK(strstr(output,"\"pipeline\":[\"a\\u0000b\"]") != NULL);
    hints.pipeline_json="[1]";
    CHECK_INT_EQ(thalovant_ask_build_frame_with_hints(&request,&hints,output,sizeof(output)),THALOVANT_ERR_INVALID);
    hints.pipeline_json="{}";
    CHECK_INT_EQ(thalovant_ask_build_frame_with_hints(&request,&hints,output,sizeof(output)),THALOVANT_ERR_INVALID);
    location.latitude=NAN;
    CHECK(thalovant_build_location(&location,place,sizeof(place)) > 0);CHECK(strstr(place,"coordinate") == NULL);
    location.latitude=0;location.longitude=0;
    CHECK(thalovant_build_location(&location,place,sizeof(place)) > 0);CHECK(strstr(place,"coordinate") == NULL);
    location.city=" ";CHECK_INT_EQ(thalovant_build_location(&location,place,sizeof(place)),0);
}
static void large_location_hint(void)
{
    char location[2400], payload[3000], expected[3400], output[3400];
    const char *prefix = "{\"city\":\"";
    size_t pos = strlen(prefix);
    memcpy(location, prefix, pos);
    memset(location + pos, 'a', 2200); pos += 2200;
    memcpy(location + pos, "\"}", 3);
    thalovant_ask_request request = {"weather", NULL, "s", NULL, "r"};
    thalovant_ask_hints hints = {NULL, NULL, location};
    CHECK(thalovant_ask_build_payload_with_hints(&request, &hints, payload, sizeof(payload)) > 0);
    thalovant_hive_message message = {"bus", payload, NULL, NULL, NULL, NULL, NULL, NULL};
    int expected_len = thalovant_wire_serialize(&message, expected, sizeof(expected));
    CHECK(expected_len > 0);
    CHECK_INT_EQ(thalovant_ask_build_frame_with_hints(&request, &hints, output, sizeof(output)), expected_len);
    CHECK_STR_EQ(output, expected);
    CHECK_INT_EQ(thalovant_ask_build_frame_with_hints(&request, &hints, output, (size_t)expected_len + 1), expected_len);
    CHECK_INT_EQ(thalovant_ask_build_frame_with_hints(&request, &hints, output, (size_t)expected_len), THALOVANT_ERR_NOMEM);
}
static void audio(void)
{
    const char *frame="{\"msg_type\":\"bus\",\"payload\":{\"type\":\"mycroft.audio.queue\",\"data\":{\"binary_data\":\"00 ff\\n10\",\"lang\":\"fr\"},\"context\":{\"request_id\":\"r\",\"lang\":\"en\"}}}";
    thalovant_ask_event event;uint8_t bytes[10];char scratch[64],lang[16];
    CHECK_INT_EQ(thalovant_ask_classify(frame,strlen(frame),"r",&event),THALOVANT_OK);
    CHECK_INT_EQ(event.kind,THALOVANT_ASK_AUDIO);CHECK(!event.is_failure);
    CHECK_INT_EQ(thalovant_ask_audio_decode(frame,strlen(frame),"r",scratch,sizeof(scratch),bytes,sizeof(bytes)),3);
    CHECK(bytes[0]==0 && bytes[1]==255 && bytes[2]==16);
    CHECK_INT_EQ(thalovant_ask_event_language(frame,strlen(frame),"r",lang,sizeof(lang)),2);CHECK_STR_EQ(lang,"fr");
    CHECK_INT_EQ(thalovant_ask_audio_decode(frame,strlen(frame),"wrong",scratch,sizeof(scratch),bytes,sizeof(bytes)),THALOVANT_ERR_MISSING);
    CHECK_INT_EQ(thalovant_ask_audio_decode(frame,strlen(frame),"r",scratch,sizeof(scratch),bytes,1),THALOVANT_ERR_NOMEM);
    const char *bad[] = {"0","0 0","gg","https://example.com","00\\u00a0ff"};
    for(size_t i=0;i<sizeof(bad)/sizeof(bad[0]);i++) {
        char invalid[512];(void)snprintf(invalid,sizeof(invalid),"{\"msg_type\":\"bus\",\"payload\":{\"type\":\"mycroft.audio.queue\",\"data\":{\"binary_data\":\"%s\"}}}",bad[i]);
        CHECK_INT_EQ(thalovant_ask_audio_decode(invalid,strlen(invalid),NULL,scratch,sizeof(scratch),bytes,sizeof(bytes)),THALOVANT_ERR_INVALID);
    }
    thalovant_audio_budget budget={0,0};
    for(int i=0;i<4;i++) CHECK(thalovant_audio_budget_accept(&budget,THALOVANT_AUDIO_CLIP_MAX*2u));
    CHECK(!thalovant_audio_budget_accept(&budget,2));CHECK(!thalovant_audio_budget_accept(&budget,SIZE_MAX));CHECK_INT_EQ(budget.dropped,2);
    budget.dropped = SIZE_MAX;
    CHECK(!thalovant_audio_budget_accept(&budget,SIZE_MAX));CHECK(budget.dropped == SIZE_MAX);
}
void tlv_test_request_helpers(void) { patterns();location_and_hints();large_location_hint();audio(); }
