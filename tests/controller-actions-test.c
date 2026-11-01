/* Host tests for the client action state machine.
 *
 * The interesting failures here are all timing ones: a kick retried on every loop pass, a ban that is never
 * repeated after an agent restart, a ban the user removed that keeps the client out. Each of those is one test
 * below, driven by a made-up clock so a two-hour timeout costs nothing to check.
 */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../controller-actions.h"

static unsigned int checks;
#define CHECK(x) do { checks++; if (!(x)) { fprintf(stderr, "actions failure line %d\n", __LINE__); exit(1); } } while (0)

static const uint8_t phone[6] = {0xaa, 0xbb, 0xcc, 0xdd, 0xee, 0xff};
static const uint8_t laptop[6] = {0x11, 0x22, 0x33, 0x44, 0x55, 0x66};

static struct action_table table;
static struct action_due due[ACTION_MAX];

/* Number of transmissions one tick yields, and the first one's shape. */
static unsigned int tick(long now)
{
    return actions_tick(&table, now, due, ACTION_MAX);
}

static int mac_is(const uint8_t a[6], const uint8_t b[6])
{
    return !memcmp(a, b, 6);
}

static void reset(void)
{
    actions_clear(&table);
}

static void ban_forever(void)
{
    struct action_intent intent;
    char json[256];

    reset();
    memset(&intent, 0, sizeof(intent));
    memcpy(intent.mac, phone, 6);
    intent.kind = ACTION_KIND_BAN;
    CHECK(actions_configure(&table, 1000, &intent, 1) == 1);

    /* Sent once, then nothing until the refresh interval: a loop that retries every second would put one
     * frame per second on the wire for a rule that only has to survive an agent restart. */
    CHECK(tick(1000) == 1);
    CHECK(due[0].kind == ACTION_KIND_BAN && due[0].allow == 0 && mac_is(due[0].mac, phone));
    CHECK(tick(1000) == 0);
    CHECK(tick(1001) == 0);
    CHECK(tick(1000 + ACTION_BAN_REFRESH - 1) == 0);
    CHECK(tick(1000 + ACTION_BAN_REFRESH) == 1);
    CHECK(due[0].allow == 0);

    /* No expiry: it is still there an hour later, and state.json says so with left = -1. */
    CHECK(tick(1000 + 3600) == 1);
    CHECK(actions_json(&table, 1000 + 3600, json, sizeof(json)) > 0);
    CHECK(strstr(json, "\"left\":-1") != NULL);
}

static void ban_expires(void)
{
    struct action_intent intent;
    char json[256];

    reset();
    memset(&intent, 0, sizeof(intent));
    memcpy(intent.mac, phone, 6);
    intent.kind = ACTION_KIND_BAN;
    intent.minutes = 1;
    CHECK(actions_configure(&table, 500, &intent, 1) == 1);
    CHECK(tick(500) == 1);
    CHECK(actions_json(&table, 500, json, sizeof(json)) > 0);
    CHECK(strstr(json, "\"left\":60") != NULL);
    CHECK(strstr(json, "\"action\":\"ban\"") != NULL);

    /* The minute runs out: one "allow" goes out and the entry is gone, so a client that was banned is not kept
     * out for ever by our own bookkeeping. */
    CHECK(tick(559) == 0);
    CHECK(tick(560) == 1);
    CHECK(due[0].allow == 1 && due[0].kind == ACTION_KIND_BAN);
    CHECK(table.count == 0);
    CHECK(tick(561) == 0);
}

static void ban_removed(void)
{
    struct action_intent intent;

    reset();
    memset(&intent, 0, sizeof(intent));
    memcpy(intent.mac, phone, 6);
    intent.kind = ACTION_KIND_BAN;
    CHECK(actions_configure(&table, 0, &intent, 1) == 1);
    CHECK(tick(0) == 1);
    CHECK(due[0].allow == 0);

    /* Config reload without the rule: the agent still has the block, so the undo has to be sent before we may
     * forget it. Two reloads before the tick must not produce two undos, and no unban when there is no ban. */
    CHECK(actions_configure(&table, 10, NULL, 0) == 1);
    CHECK(actions_configure(&table, 11, NULL, 0) == 1);
    CHECK(tick(11) == 1);
    CHECK(due[0].allow == 1 && mac_is(due[0].mac, phone));
    CHECK(table.count == 0);
    CHECK(actions_configure(&table, 12, NULL, 0) == 0);
    CHECK(tick(12) == 0);
}

static void kick_retries_then_gives_up(void)
{
    struct action_intent intent;
    char json[256];

    reset();
    memset(&intent, 0, sizeof(intent));
    memcpy(intent.mac, phone, 6);
    intent.kind = ACTION_KIND_KICK;
    CHECK(actions_configure(&table, 0, &intent, 1) == 1);

    CHECK(tick(0) == 1);
    CHECK(due[0].kind == ACTION_KIND_KICK && due[0].allow == 0);
    CHECK(tick(1) == 0);                                  /* debounce */
    CHECK(tick(ACTION_KICK_RETRY) == 1);                  /* second try */
    CHECK(tick(ACTION_KICK_RETRY * 2) == 1);              /* third */
    CHECK(tick(ACTION_KICK_RETRY * 3) == 0);              /* no more requests */
    CHECK(table.count == 1);
    CHECK(actions_json(&table, ACTION_KICK_RETRY * 3, json, sizeof(json)) > 0);
    CHECK(strstr(json, "\"state\":\"sent\"") != NULL);

    /* Time is up with no answer: the entry stays (the page has to show that it did not work) but nothing else
     * is sent, and it says failed. */
    CHECK(tick(ACTION_KICK_RETRY * ACTION_KICK_TRIES + ACTION_KICK_RETRY) == 0);
    CHECK(actions_json(&table, ACTION_KICK_RETRY * ACTION_KICK_TRIES + ACTION_KICK_RETRY, json, sizeof(json)) > 0);
    CHECK(strstr(json, "\"state\":\"failed\"") != NULL);
    CHECK(tick(100000) == 0);
}

static void kick_answered(void)
{
    struct action_intent intent;
    char json[256];

    reset();
    memset(&intent, 0, sizeof(intent));
    memcpy(intent.mac, phone, 6);
    intent.kind = ACTION_KIND_KICK;
    CHECK(actions_configure(&table, 0, &intent, 1) == 1);
    CHECK(tick(0) == 1);

    /* The agent reports success: no more retries, and the page says done. */
    actions_observe(&table, phone, 1);
    CHECK(tick(ACTION_KICK_RETRY) == 0);
    CHECK(actions_json(&table, ACTION_KICK_RETRY, json, sizeof(json)) > 0);
    CHECK(strstr(json, "\"state\":\"done\"") != NULL);

    /* A report about a client nobody asked about changes nothing. */
    reset();
    actions_observe(&table, laptop, 1);
    CHECK(table.count == 0);
}

static void kick_refused(void)
{
    struct action_intent intent;
    char json[256];

    reset();
    memset(&intent, 0, sizeof(intent));
    memcpy(intent.mac, phone, 6);
    intent.kind = ACTION_KIND_KICK;
    CHECK(actions_configure(&table, 0, &intent, 1) == 1);
    CHECK(tick(0) == 1);
    actions_observe(&table, phone, 0);                    /* 0x8015 with a non-zero status */
    CHECK(tick(ACTION_KICK_RETRY) == 0);
    CHECK(actions_json(&table, ACTION_KICK_RETRY, json, sizeof(json)) > 0);
    CHECK(strstr(json, "\"state\":\"failed\"") != NULL);
}

static void two_clients_and_limits(void)
{
    struct action_intent intents[2];
    char json[512];

    reset();
    memset(intents, 0, sizeof(intents));
    memcpy(intents[0].mac, phone, 6);
    intents[0].kind = ACTION_KIND_KICK;
    memcpy(intents[1].mac, laptop, 6);
    intents[1].kind = ACTION_KIND_BAN;
    intents[1].minutes = 5;
    CHECK(actions_configure(&table, 0, intents, 2) == 2);
    CHECK(tick(0) == 2);                                  /* both due at once, in table order */
    CHECK(due[0].kind == ACTION_KIND_KICK && due[1].kind == ACTION_KIND_BAN);
    CHECK(actions_json(&table, 0, json, sizeof(json)) > 0);
    CHECK(strstr(json, "\"mac\":\"aa:bb:cc:dd:ee:ff\"") != NULL);
    CHECK(strstr(json, "\"mac\":\"11:22:33:44:55:66\"") != NULL);

    /* The same rule twice is one entry, not two. */
    CHECK(actions_configure(&table, 1, intents, 2) == 2);
    CHECK(tick(1) == 0);

    /* More intents than the table holds is an error, not a silent partial apply. */
    reset();
    {
        struct action_intent many[ACTION_MAX + 1];
        unsigned int i;
        memset(many, 0, sizeof(many));
        for (i = 0; i < ACTION_MAX + 1; i++) {
            many[i].mac[0] = 2; many[i].mac[5] = (uint8_t)(i + 1);
            many[i].kind = ACTION_KIND_BAN;
        }
        CHECK(actions_configure(&table, 0, many, ACTION_MAX) == ACTION_MAX);
        CHECK(actions_configure(&table, 0, many, ACTION_MAX + 1) == -1);
    }
    reset();
    CHECK(actions_configure(&table, 0, NULL, 1) == -1);
    {
        struct action_intent bad;
        memset(&bad, 0, sizeof(bad));
        bad.kind = 7;
        CHECK(actions_configure(&table, 0, &bad, 1) == -1);
    }
}

static void json_is_bounded(void)
{
    struct action_intent intents[ACTION_MAX];
    char small[16];
    unsigned int i;

    reset();
    memset(intents, 0, sizeof(intents));
    for (i = 0; i < ACTION_MAX; i++) {
        intents[i].mac[0] = 2;
        intents[i].mac[5] = (uint8_t)(i + 1);
        intents[i].kind = ACTION_KIND_BAN;
    }
    CHECK(actions_configure(&table, 0, intents, ACTION_MAX) == (int)ACTION_MAX);

    /* A room too small for even one entry is a refusal, never a truncated object the page would have to guess
     * about; the daemon sizes its buffer for the worst case. */
    CHECK(actions_json(&table, 0, small, sizeof(small)) == -1);
    CHECK(actions_json(&table, 0, small, 0) == -1);
    CHECK(actions_json(NULL, 0, small, sizeof(small)) == -1);
}

int main(void)
{
    ban_forever();
    ban_expires();
    ban_removed();
    kick_retries_then_gives_up();
    kick_answered();
    kick_refused();
    two_clients_and_limits();
    json_is_bounded();
    printf("controller-actions: %u assertions passed\n", checks);
    return 0;
}
