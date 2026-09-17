#include <string.h>

#include "saki_theme.h"
#include "unity.h"

TEST_CASE("theme registry has explicit fallback and complete default pack", "[saki][theme]")
{
    const saki_theme_pack_t *classic = saki_theme_find("classic");
    const saki_theme_pack_t *stage = saki_theme_default();

    TEST_ASSERT_EQUAL_UINT32(2, saki_theme_pack_count());
    TEST_ASSERT_NOT_NULL(classic);
    TEST_ASSERT_EQUAL_STRING("saki_stage", stage->id);
    TEST_ASSERT_EQUAL_PTR(classic, saki_theme_next(stage));
    TEST_ASSERT_EQUAL_PTR(stage, saki_theme_next(classic));
    TEST_ASSERT_NULL(saki_theme_pack_at(saki_theme_pack_count()));
    TEST_ASSERT_NULL(saki_theme_find("missing"));
    for (int state = SAKI_AGENT_IDLE; state <= SAKI_AGENT_CANCELLED; ++state) {
        TEST_ASSERT_NULL(saki_theme_icon(classic, state));
        TEST_ASSERT_NOT_NULL(saki_theme_icon(stage, state));
    }
}

TEST_CASE("theme copy is stable bounded and switchable off", "[saki][theme]")
{
    const saki_theme_pack_t *stage = saki_theme_default();
    const char *first = saki_theme_copy(stage, SAKI_THEME_COPY_LIGHT, SAKI_AGENT_WORKING,
        "0123456789abcdef0123456789abcdef", "run-1", "shell");
    const char *again = saki_theme_copy(stage, SAKI_THEME_COPY_LIGHT, SAKI_AGENT_WORKING,
        "0123456789abcdef0123456789abcdef", "run-1", "shell");

    TEST_ASSERT_NOT_NULL(first);
    TEST_ASSERT_EQUAL_PTR(first, again);
    TEST_ASSERT_LESS_OR_EQUAL_UINT32(96, strlen(first));
    TEST_ASSERT_NULL(saki_theme_copy(stage, SAKI_THEME_COPY_OFF, SAKI_AGENT_WORKING,
        "task", "run", "shell"));
    TEST_ASSERT_NULL(saki_theme_copy(saki_theme_find("classic"), SAKI_THEME_COPY_LIGHT,
        SAKI_AGENT_WORKING, "task", "run", "shell"));
}
