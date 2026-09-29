#include "legacy_typed.h"

#include <stdint.h>
#include <string.h>

int main(void) {
    LegacyMessage_t message;
    LegacyMessage_init(&message);
    message.id = (char *)"legacy";
    message.generation = UINT64_C(7);
    if (strcmp(message.id, "legacy") != 0 || message.generation != UINT64_C(7))
        return 1;
    /*
     * This fixture intentionally validates native/text typed generation only.
     * The schema preserves a published variable-first declaration order and is
     * not admitted as a canonical TBE binary format plan.
     */
    message.id = NULL;
    LegacyMessage_clear(&message);
    return 0;
}
