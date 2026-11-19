#include "select_cubes_repeat.h"

#include "game/possible_to_play.h"

#include "utils/random_element.h"

namespace banggame {

    scored_target<card_list> targeting_select_cubes_repeat::random_target(card_ptr origin_card, player_ptr origin, const effect_holder &effect, const effect_context &ctx) const {
        auto [num_repeats, score] = random_max_element(
            rn::iota_view{0, count_cubes(origin) / ncubes + 1},
            [&](int n) {
                if (effect.on_prompt(origin_card, origin, n, {}, ctx)) return -2;
                return effect.evaluate(origin_card, origin, n, ctx);
            },
            origin->m_game->bot_rng
        );

        auto selected_cubes = sample_elements(get_all_cubes(origin), ncubes * num_repeats, origin->m_game->bot_rng);
        
        if (effect.on_prompt(origin_card, origin, {}, ctx)) score -= 2;
        else score += effect.evaluate(origin_card, origin, ctx);

        return { std::move(selected_cubes), score };
    }

    game_string targeting_select_cubes_repeat::get_error(card_ptr origin_card, player_ptr origin, const effect_holder &effect, const effect_context &ctx, const card_list &target_cards) const {
        if (target_cards.size() % ncubes != 0) {
            return "ERROR_INVALID_TARGETS";
        }
        for (card_ptr c : target_cards) {
            if (c->owner != origin) {
                return {"ERROR_TARGET_NOT_SELF", origin_card};
            }
        }
        return effect.get_error(origin_card, origin, target_cards.size() / ncubes, ctx);
    }

}