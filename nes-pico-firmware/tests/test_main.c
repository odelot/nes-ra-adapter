#include "unity.h"
#include "test_rcheevos.h"
#include "test_search.h"


// Defina setUp e tearDown como funções vazias
void setUp(void)
{
    // Nada a fazer
}

void tearDown(void)
{
    // Nada a fazer
}


void test_handle_response () {
    char response[] = "RESP=FF;0C8;{\"Success\":true,\"User\":\"odelot\",\"AvatarUrl\":\"https://media.retroachievements.org/UserPic/odelot.png\",\"Token\":\"Vw8qLHo8KSbqjue8\",\"Score\":0,\"SoftcoreScore\":27,\"Messages\":0,\"Permissions\":1,\"AccountType\":\"Registered\"}";
    char* response_ptr = response;
    response_ptr += 5;
    char aux[8];
    strncpy(aux, response_ptr, 2);
    aux[2] = '\0';
    uint8_t request_id = (uint8_t)strtol(aux, NULL, 16);    
    response_ptr += 3;
    strncpy(aux, response_ptr, 3);
    aux[3] = '\0';
    response_ptr += 4;
    uint16_t http_code = (uint16_t)strtol(aux, NULL, 16);

    printf("request_id=%d\n", request_id);
    printf("http_code=%d\n", http_code);
    printf("response=%s\n", response_ptr);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_handle_response);
    RUN_TEST(test_rcheevos_client);
    RUN_TEST(test_search_method);
    return UNITY_END();
}
