#ifndef __BOT_ADVICE_H__
#define __BOT_ADVICE_H__

#include <map>

#include "cards/card_defs.h"

namespace banggame {

    // What the AI helper (ai-bridge) said a bot should think and do, kept until the bot's next turn.
    enum class ai_plan {
        none,
        lie_low,
        attack_sheriff,
        protect_sheriff,
        hunt_outlaws,
        survive
    };

    struct bot_advice {
        // player id -> the role the bot suspects (only used for players whose role the bot doesn't know)
        std::map<int, player_role> suspects;
        ai_plan plan = ai_plan::none;
        // preferred player to attack (player id), 0 = none
        int target_id = 0;
    };

}

#endif
