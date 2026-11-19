#include "squaw.h"

#include "effects/base/steal_destroy.h"

#include "game/game_table.h"
#include "game/prompts.h"
#include "game/bot_suggestion.h"

namespace banggame {

    namespace contexts {
        struct squaw_steal{};
    }

    void effect_squaw_pay::add_context(card_ptr origin_card, player_ptr origin, int ncubes, effect_context &ctx) const {
        if (ncubes != 0) {
            ctx.add(contexts::squaw_steal{});
        }
    }

    game_string effect_squaw::get_error(card_ptr origin_card, player_ptr origin, card_ptr target_card, const effect_context &ctx) const {
        if (target_card->owner && ctx.contains<contexts::squaw_steal>()) {
            return effect_steal{}.get_error(origin_card, origin, target_card);
        }
        return {};
    }

    prompt_string effect_squaw::on_prompt(card_ptr origin_card, player_ptr origin, card_ptr target_card, effect_flags flags, const effect_context &ctx) const {
        MAYBE_RETURN(prompts::prompt_target_self_card(origin_card, origin, target_card));
        MAYBE_RETURN(prompts::prompt_target_immunity(origin_card, origin, target_card->owner, flags));
        return {};
    }

    int effect_squaw::evaluate(card_ptr origin_card, player_ptr origin, card_ptr target_card) const {
        return bot_suggestion::evaluate_target_card(origin, target_card);
    }

    void effect_squaw::on_play(card_ptr origin_card, player_ptr origin, card_ptr target_card, effect_flags flags, const effect_context &ctx) const {
        if (target_card->owner) {
            if (ctx.contains<contexts::squaw_steal>()) {
                effect_steal{}.on_play(origin_card, origin, target_card, flags);
            } else {
                effect_destroy{}.on_play(origin_card, origin, target_card, flags);
            }
        }
    }
}
