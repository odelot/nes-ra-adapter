#include "test_rcheevos.h"
#include "unity.h"
#include "rc_client.h"

rc_client_t *g_client = NULL;
static void *g_callback_userdata = &g_client; /* dummy data */

unsigned int frame = 0;

typedef struct
{
    rc_client_server_callback_t callback;
    void *callback_data;
} async_callback_data;

static void rc_client_callback_expect_success(int result, const char *error_message, rc_client_t *client, void *callback_userdata)
{
    printf("rc_client_callback_expect_success: %d\n", result);
}

static uint32_t read_memory_init(uint32_t address, uint8_t *buffer, uint32_t num_bytes, rc_client_t *client)
{
    printf("Reading memory at address %p, num_bytes: %d\n", address, num_bytes);
    buffer[0] = 0;
    return num_bytes;
}

// This is the function the rc_client will use to read memory for the emulator. we don't need it yet,
// so just provide a dummy function that returns "no memory read".
static uint32_t read_memory(uint32_t address, uint8_t *buffer, uint32_t num_bytes, rc_client_t *client)
{
    // printf("Reading memory at address %p, num_bytes: %d\n", address, num_bytes);
    if (frame < 96)
    {
        buffer[0] = 99;
    }
    if (frame == 96)
    {
        if (address == 0x05fc)
        {
            buffer[0] = 5;
        }
        else
        {
            buffer[0] = 99;
        }
    }
    if (frame == 99)
    {
        if (address == 0x044b)
        {
            buffer[0] = 20;
        }
        else if (address == 0x05fc)
        {
            buffer[0] = 5;
        }
        else
        {
            buffer[0] = 99;
        }
    }
    if (frame == 98)
    {
        if (address == 0x044b)
        {
            buffer[0] = 20;
        }
        else if (address == 0x05fc)
        {
            buffer[0] = 5;
        }
        else
        {
            buffer[0] = 4;
        }
    }
    if (frame == 99)
    {
        if (address == 0x044b)
        {
            buffer[0] = 20;
        }
        else if (address == 0x05fc)
        {
            buffer[0] = 6;
        }
        else if (address == 0x03f6)
        {
            buffer[0] = 1;
        }
        else
        {
            buffer[0] = 4;
        }
    }
    // if (frame == 98) //simulate first achievement
    // {
    //     if (address == 0x044b) {
    //         buffer[0] = 0;
    //     }
    //     if (address == 0x03f6) {
    //         buffer[0] = 0;
    //     }

    // }
    // if (frame == 99)
    // {
    //     if (address == 0x044b) {
    //         buffer[0] = 1;
    //     }
    //     if (address == 0x03f6) {
    //         buffer[0] = 1;
    //     }

    // }
    return num_bytes;
}

// This is the callback function for the asynchronous HTTP call (which is not provided in this example)
static void http_callback(int status_code, const char *content, size_t content_size, void *userdata, const char *error_message)
{
    // Prepare a data object to pass the HTTP response to the callback
    rc_api_server_response_t server_response;
    memset(&server_response, 0, sizeof(server_response));
    server_response.body = content;
    server_response.body_length = content_size;
    server_response.http_status_code = status_code;

    // handle non-http errors (socket timeout, no internet available, etc)
    if (status_code == 0 && error_message)
    {
        // assume no server content and pass the error through instead
        server_response.body = error_message;
        server_response.body_length = strlen(error_message);
        // Let rc_client know the error was not catastrophic and could be retried. It may decide to retry or just
        // immediately pass the error to the callback. To prevent possible retries, use RC_API_SERVER_RESPONSE_CLIENT_ERROR.
        server_response.http_status_code = RC_API_SERVER_RESPONSE_RETRYABLE_CLIENT_ERROR;
    }

    // Get the rc_client callback and call it
    async_callback_data *async_data = (async_callback_data *)userdata;
    async_data->callback(&server_response, async_data->callback_data);

    // Release the captured rc_client callback data
    free(async_data);
}

uint8_t prefix(const char *pre, const char *str)
{
    return strncmp(pre, str, strlen(pre)) == 0;
}

// This is the HTTP request dispatcher that is provided to the rc_client. Whenever the client
// needs to talk to the server, it will call this function.
static void server_call(const rc_api_request_t *request,
                        rc_client_server_callback_t callback, void *callback_data, rc_client_t *client)
{
    // RetroAchievements may not allow hardcore unlocks if we don't properly identify ourselves.
    char rcheevos_version[64];
    rc_client_get_user_agent_clause(client, rcheevos_version, sizeof(rcheevos_version));
    const char *platform = "NES_RA_ADAPTER/0.1";

    char user_agent[256];
    snprintf(user_agent, sizeof(user_agent), "%s %s", platform, rcheevos_version);
    printf("User agent: %s\n", user_agent);

    // callback must be called with callback_data, regardless of the outcome of the HTTP call.
    // Since we're making the HTTP call asynchronously, we need to capture them and pass it
    // through the async HTTP code.
    async_callback_data *async_data = malloc(sizeof(async_callback_data));
    async_data->callback = callback;
    async_data->callback_data = callback_data;

    // If post data is provided, we need to make a POST request, otherwise, a GET request will suffice.
    printf("Requesting %s\n", request->url);
    if (request->post_data)
    {
        printf("Post data: %s\n", request->post_data);
        if (strcmp(request->post_data, "r=gameid&m=2178cc3772b01c9f3db5b2de328bb992") == 0)
        {
            char response[] = "{\"Success\": true,\"GameID\": 1496}";
            http_callback(200, response, strlen(response), async_data, NULL);
        }
        if (prefix("r=login2", request->post_data))
        {
            char response[] = "{\"Success\": true,\"User\": \"user\",\"AvatarUrl\": \"https://media.retroachievements.org/UserPic/user.png\",\"Token\": \"VX8XLXoXKXbXjXeX\",\"Score\": 0,\"SoftcoreScore\": 2, \"Messages\": 0, \"Permissions\": 1, \"AccountType\": \"Registered\"}";
            http_callback(200, response, strlen(response), async_data, NULL);
        }
        if (prefix("r=patch&u=user&t=VX8XLXoXKXbXjXeX&g=1496", request->post_data))
        {
            const char *response = "{\n"
                                   "  \"Success\": true,\n"
                                   "  \"PatchData\": {\n"
                                   "    \"ID\": 1496,\n"
                                   "    \"Title\": \"R.C. Pro-Am\",\n"
                                   "    \"ImageIcon\": \"/Images/052570.png\",\n"
                                   "    \"RichPresencePatch\": null,\n"
                                   "    \"ConsoleID\": 7,\n"
                                   "    \"ImageIconURL\": \"https://media.retroachievements.org/Images/052570.png\",\n"
                                   "    \"Achievements\": [\n"
                                   "      {\n"
                                   "        \"ID\": 47891,\n"
                                   "        \"MemAddr\": \"0xH044b=20_0xH03f6=1_p0xH05fc=5_0xH05fc=6_0xH05fd<=4_0xH05fe<=4_0xH05ff<=4\",\n"
                                   "        \"Title\": \"Blue Flag\",\n"
                                   "        \"Description\": \"Lap your opponents and win level 21\",\n"
                                   "        \"Points\": 25,\n"
                                   "        \"Flags\": 3,\n"
                                   "        \"BadgeName\": \"348421\",\n"
                                   "        \"Modified\": 1696311616,\n"
                                   "        \"Created\": 1494443867,\n"
                                   "        \"Type\": null\n"
                                   "      },\n"
                                   "      {\n"
                                   "        \"ID\": 47875,\n"
                                   "        \"MemAddr\": \"d0xH044b=0_0xH044b=1_0xH044c=1_0xH03f6=1\",\n"
                                   "        \"Title\": \"First Blood\",\n"
                                   "        \"Description\": \"Win level 1\",\n"
                                   "        \"Points\": 10,\n"
                                   "        \"Author\": \"jossyhadash\",\n"
                                   "        \"Modified\": 1683290778,\n"
                                   "        \"Created\": 1494361538,\n"
                                   "        \"BadgeName\": \"348418\",\n"
                                   "        \"Flags\": 3,\n"
                                   "        \"Type\": \"progression\",\n"
                                   "        \"Rarity\": 72.61,\n"
                                   "        \"RarityHardcore\": 37.71,\n"
                                   "        \"BadgeURL\": \"https://media.retroachievements.org/Badge/348418.png\",\n"
                                   "        \"BadgeLockedURL\": \"https://media.retroachievements.org/Badge/348418_lock.png\"\n"
                                   "      }\n"
                                   "    ],\n"
                                   "    \"Leaderboards\": []\n"
                                   "  },\n"
                                   "  \"Warning\": \"The server does not recognize this client and will not allow hardcore unlocks. Please send a message to RAdmin on the RetroAchievements website for information on how to submit your emulator for hardcore consideration.\"\n"
                                   "}";

            http_callback(200, response, strlen(response), async_data, NULL);
        }
        if (prefix("r=startsession", request->post_data))
        {
            char response[] = "{\"Success\": true,\"Unlocks\": [{\"ID\": 101000001,\"When\": 1738293217}],\"ServerNow\": 1738293217}";
            http_callback(200, response, strlen(response), async_data, NULL);
        }
    }
    //   if (request->post_data)
    //     async_http_post(request->url, request->post_data, user_agent, http_callback, async_data);
    //   else
    //     async_http_get(request->url, user_agent, http_callback, async_data);
}

// rcheevos log message handler
static void log_message(const char *message, const rc_client_t *client)
{
    printf("%s\n", message);
}

// Initialize the RetroAchievements client
rc_client_t* initialize_retroachievements_client(rc_client_t *g_client, rc_client_read_memory_func_t read_memory, rc_client_server_call_t server_call)
{
     // Create the client instance (using a global variable simplifies this example)
    g_client = rc_client_create(read_memory, server_call);

    // Provide a logging function to simplify debugging
    rc_client_enable_logging(g_client, RC_CLIENT_LOG_LEVEL_VERBOSE, log_message);


    // Disable hardcore - if we goof something up in the implementation, we don't want our
    // account disabled for cheating.
    rc_client_set_hardcore_enabled(g_client, 0);
    return g_client;
}

void shutdown_retroachievements_client(rc_client_t *g_client)
{
    if (g_client)
    {
        // Release resources associated to the client instance
        rc_client_destroy(g_client);
        g_client = NULL;
    }
}

void test_rcheevos_client(void)
{

    int ret;
    g_client = initialize_retroachievements_client(g_client, read_memory_init, server_call);
    rc_client_begin_login_with_password(g_client, "user", "pass", rc_client_callback_expect_success, g_callback_userdata);
    const rc_client_user_t *user;
    rc_client_begin_load_game(g_client, "2178cc3772b01c9f3db5b2de328bb992", rc_client_callback_expect_success, g_callback_userdata);
    if (rc_client_is_game_loaded(g_client))
    {
        printf("Game loaded\n");
    }
    rc_client_set_read_memory_function(g_client, read_memory);

    for (int i = 0; i < 100; i++)
    {
        rc_client_do_frame(g_client);
        frame += 1;
    }
    shutdown_retroachievements_client(g_client);
    TEST_ASSERT_EQUAL(0, 0);
}
