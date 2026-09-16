/* Shared native/Unity coverage for main-session selection, independent of LVGL. */
#include <stdio.h>
#include <string.h>
#include "saki_ui_policy.h"
#ifndef SAKI_NATIVE_TEST
#include "unity.h"
#endif

#define CHECK(condition) do { if (!(condition)) return __LINE__; } while (0)
static saki_display_snapshot_t display;

static int selection_contract(void)
{
    saki_session_view_t view = {0};
    memset(&display, 0, sizeof(display));
    display.multi_session = true;
    display.count = 4;
    for (unsigned i = 0; i < display.count; ++i) {
        snprintf(display.items[i].task_id, sizeof(display.items[i].task_id), "session-%u", i);
        snprintf(display.run_ids[i], sizeof(display.run_ids[i]), "run-%u", i);
    }
    CHECK(saki_session_view_update(&view, &display, 0) == 0);
    CHECK(saki_session_view_select(&view, &display, 2, 100) == 2);
    display.items[0].state = SAKI_AGENT_COMPLETED;
    display.revisions[0]++;
    CHECK(saki_session_view_update(&view, &display, 200) == 2);
    /* Priority reorder retains the selected identity, not its old slot. */
    strcpy(display.items[1].task_id, "session-2");
    strcpy(display.items[2].task_id, "session-1");
    CHECK(saki_session_view_update(&view, &display, 300) == 1);
    CHECK(saki_session_view_update(&view, &display, 15099) == 1);
    CHECK(saki_session_view_update(&view, &display, 15100) == 0);
    CHECK(saki_session_view_select(&view, &display, 3, 16000) == 3);
    /* A new run in the latest session interrupts temporary browsing. */
    strcpy(display.run_ids[0], "next-run");
    CHECK(saki_session_view_update(&view, &display, 16001) == 0);
    CHECK(saki_session_view_select(&view, &display, 3, 17000) == 3);
    strcpy(display.items[0].task_id, "new-submission");
    CHECK(saki_session_view_update(&view, &display, 17001) == 0);
    CHECK(saki_session_view_select(&view, &display, 3, 18000) == 3);
    display.count = 2;
    CHECK(saki_session_view_update(&view, &display, 18001) == 0);
    CHECK(saki_session_view_select(&view, &display, 1, 19000) == 1);
    CHECK(saki_session_view_select(&view, &display, 0, 19001) == 0);
    CHECK(view.selected_id[0] == '\0');
    CHECK(saki_session_view_select(&view, &display, -1, 19002) == 0);
    CHECK(saki_session_view_select(&view, &display, 4, 19003) == 0);
    display.count = 0;
    CHECK(saki_session_view_update(&view, &display, 20000) == 0);
    CHECK(view.latest_id[0] == '\0');
    display.count = 1;
    CHECK(saki_session_view_update(&view, &display, 21000) == 0);
    display.multi_session = false;
    CHECK(saki_session_view_update(&view, &display, 22000) == 0);
    CHECK(view.latest_id[0] == '\0');
    return 0;
}

#ifdef SAKI_NATIVE_TEST
int main(void)
{
    int result = selection_contract();
    if (result) fprintf(stderr, "selection contract failed at line %d\n", result);
    return result ? 1 : 0;
}
#else
TEST_CASE("latest submission and temporary session browsing", "[saki][ui-policy]")
{
    TEST_ASSERT_EQUAL(0, selection_contract());
}
#endif
