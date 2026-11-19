#include "select_cubes.h"

#include "cards/filter_enums.h"

#include "effects/armedanddangerous/ruleset.h"

#include "game/game_table.h"

namespace banggame {

    card_list targeting_select_cubes::get_all_cubes(player_ptr origin, const effect_context &ctx, bool safe_bomb) const {
        card_list result;
        for (card_ptr slot : cube_slots(origin)) {
            int count = slot->num_cubes();
            count -= rn::count(ctx.get_all<contexts::selected_cube>(), slot);
            if (safe_bomb && count > 0) {
                auto value = slot->get_tag_value(tag_type::penalty);
                if (value && *value < 0) --count;
            }
            for (int i=0; i<count; ++i) {
                result.push_back(slot);
            }
        }
        return result;
    }

    bool targeting_select_cubes::is_possible(card_ptr origin_card, player_ptr origin, const effect_holder &effect, const effect_context &ctx) const {
        return get_all_cubes(origin, ctx).size() >= ncubes;
    }

    scored_target<card_list> targeting_select_cubes::random_target(card_ptr origin_card, player_ptr origin, const effect_holder &effect, const effect_context &ctx) const {
        return sample_elements(get_all_cubes(origin, ctx), ncubes, origin->m_game->bot_rng);
    }

    game_string targeting_select_cubes::get_error(card_ptr origin_card, player_ptr origin, const effect_holder &effect, const effect_context &ctx, const card_list &target_cards) const {
        if (target_cards.size() != ncubes) {
            return "ERROR_INVALID_TARGETS";
        }
        for (card_ptr c : target_cards) {
            if (c->owner != origin) {
                return {"ERROR_TARGET_NOT_SELF", origin_card};
            }
        }
        return effect.get_error(origin_card, origin, target_cards.size() / ncubes, ctx);
    }

    prompt_string targeting_select_cubes::on_prompt(card_ptr origin_card, player_ptr origin, const effect_holder &effect, const effect_context &ctx, const card_list &target_cards) const {
        MAYBE_RETURN(origin->m_game->call_event(event_type::get_select_cubes_prompt{ origin, ctx }));
        return effect.on_prompt(origin_card, origin, target_cards.size() / ncubes, {}, ctx);
    }

    void targeting_select_cubes::add_context(card_ptr origin_card, player_ptr origin, const effect_holder &effect, effect_context &ctx, const card_list &target_cards) const {
        for (card_ptr target_card : target_cards) {
            ctx.add(contexts::selected_cube{ target_card });
        }
        effect.add_context(origin_card, origin, target_cards.size() / ncubes, ctx);
    }

    void targeting_select_cubes::on_play(card_ptr origin_card, player_ptr origin, const effect_holder &effect, const effect_context &ctx, const card_list &target_cards) const {
        using cube_count_pair = std::pair<card_ptr, int>;
        std::vector<cube_count_pair> card_cube_count;

        for (card_ptr cube : target_cards) {
            auto it = rn::find(card_cube_count, cube, &cube_count_pair::first);
            if (it == card_cube_count.end()) {
                card_cube_count.emplace_back(cube, 1);
            } else {
                ++it->second;
            }
        }

        for (const auto &[cube, ncubes] : card_cube_count) {
            cube->move_cubes(nullptr, ncubes);
        }

        effect.on_play(origin_card, origin, target_cards.size() / ncubes, {}, ctx);
    }

}