#ifndef __ARMEDANDDANGEROUS_FLINTLOCK_H__
#define __ARMEDANDDANGEROUS_FLINTLOCK_H__

#include "cards/card_effect.h"

namespace banggame {

    struct effect_flintlock {
        void on_play(card_ptr origin_card, player_ptr origin, int ncubes) const;

        prompt_string on_prompt(card_ptr origin_card, player_ptr origin, player_ptr target, effect_flags flags = {}, const effect_context &ctx = {}) const;
        int evaluate(card_ptr origin_card, player_ptr origin, player_ptr target) const;
        void on_play(card_ptr origin_card, player_ptr origin, player_ptr target, effect_flags flags = {}) const;
    };

    DEFINE_EFFECT(flintlock, effect_flintlock)
}

#endif