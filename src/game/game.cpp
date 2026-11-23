#include "game.h"

#include "game_update.h"
#include "game_options.h"

#include "cards/bang_cards.h"
#include "cards/filter_enums.h"
#include "cards/game_enums.h"
#include "cards/game_events.h"

#include "effects/base/requests.h"
#include "effects/base/death.h"
#include "effects/base/damage.h"
#include "effects/base/bang.h"
#include "effects/base/heal.h"

#include "play_verify.h"
#include "possible_to_play.h"
#include "give_card.h"

#include "net/lobby.h"
#include "net/manager.h"

#include <array>
#include <unordered_set>

namespace banggame {

    static game_updates::preload_assets make_preload_assets_update(game_ptr table) {
        std::unordered_set<std::string> images;
        for (const card_data &card : table->m_cards_storage | rv::values) {
            if (!card.image.empty()) {
                if (card.image.contains('/')) {
                    images.emplace(card.image);
                } else {
                    images.emplace(std::format("{}/{}", card.deck, card.image));
                }
            }
            switch (card.deck) {
            case card_deck_type::none:
            case card_deck_type::locomotive:
            case card_deck_type::legends:
                break;
            default:
                images.emplace(std::format("backface/{}", card.deck));
            }
        }

        std::unordered_set<sound_id> sounds = bang_cards.expansions
            | rv::values
            | rv::filter([&](const expansion_data &data) {
                return !data.expansion || table->m_options.expansions.contains(data.expansion);
            })
            | rv::for_each(&expansion_data::sounds)
            | rn::to<std::unordered_set>();
        
        return {
            .images {images | rn::to<std::vector>()},
            .sounds {sounds | rn::to<std::vector>()}
        };
    }

    static std::optional<game_updates::timer_status> get_request_timer_status(const request_base &req) {
        if (auto *timer = dynamic_cast<const request_timer *>(&req); timer && timer->enabled()) {
            return game_updates::timer_status{
                .timer_id = timer->get_timer_id(),
                .duration = timer->get_duration()
            };
        }
        return std::nullopt;
    }

    static unknown_card_list make_unknown_cards(player_ptr owner, const playable_cards_list &cards) {
        unknown_card_list result;
        card_set visited_cards;

        auto add_card = [&](card_ptr target_card) {
            if (!visited_cards.contains(target_card)) {
                visited_cards.add(target_card);

                if (!target_card->visibility.matches(owner)) {
                    result.emplace_back(target_card, *target_card);
                }
            }
        };

        for (const playable_card_info &info : cards) {
            add_card(info.card);
            for (const modifier_entry &entry : info.modifiers) {
                add_card(entry.card);
            }
        }

        return result;
    }

    static game_updates::player_distances make_player_distances(player_ptr owner) {
        game_updates::player_distances result{};
        if (owner) {
            for (player_ptr target : owner->m_game->m_players) {
                if (target != owner) {
                    if (int distance_mod = target->get_distance_mod()) {
                        result.distance_mods.emplace_back(target, distance_mod);
                    }
                }
            }
            result.range_mod = owner->get_range_mod();
            result.weapon_range = owner->get_weapon_range();
        }
        return result;
    }
    
    static player_list get_request_target_set_players(player_ptr origin) {
        if (origin) {
            if (auto req = origin->m_game->top_request<interface_target_set_players>(target_is{origin})) {
                return origin->m_game->m_players
                    | rv::filter([&](player_ptr p){ return req->in_target_set(p); })
                    | rn::to<std::vector>();
            }
        }
        return {};
    }

    static card_list get_request_target_set_cards(player_ptr origin) {
        if (origin) {
            if (auto req = origin->m_game->top_request<interface_target_set_cards>(target_is{origin})) {
                return get_all_targetable_cards(origin)
                    | rv::filter([&](card_ptr c){ return req->in_target_set(c); })
                    | rn::to<std::vector>();
            }
        }
        return {};
    }

    static game_updates::request_status make_request_update(const request_base &req, player_ptr owner = nullptr) {
        auto respond_cards = generate_playable_cards_list(owner, effect_list_type::responses);
        auto unknown_cards = make_unknown_cards(owner, respond_cards);
        return game_updates::request_status {
            .origin_card = req.origin_card,
            .origin = req.origin,
            .target = req.target,
            .status_text = req.status_text(owner),
            .respond_cards = std::move(respond_cards),
            .unknown_cards = std::move(unknown_cards),
            .highlight_cards = req.get_highlights(owner),
            .target_set_players = get_request_target_set_players(owner),
            .target_set_cards = get_request_target_set_cards(owner),
            .distances = make_player_distances(owner),
            .timer = get_request_timer_status(req)
        };
    }

    static game_updates::status_ready make_status_ready_update(player_ptr owner) {
        auto play_cards = generate_playable_cards_list(owner);
        auto unknown_cards = make_unknown_cards(owner, play_cards);
        return {
            .play_cards = std::move(play_cards),
            .unknown_cards = std::move(unknown_cards),
            .distances = make_player_distances(owner)
        };
    }
    
    ticks game::get_total_update_time() const {
        game_duration result{0};
        for (player_ptr p : m_players) {
            game_duration player_result{0};
            for (const auto &[target, content, duration] : m_updates) {
                if (duration > game_duration{0} && target.matches(p)) {
                    player_result += duration;
                }
            }
            if (player_result > result) {
                result = player_result;
            }
        }
        return std::chrono::duration_cast<ticks>(transform_duration(result));
    }

    bool game::contains_user(int user_id) const {
        return find_player_by_userid(user_id) != nullptr;
    }

    void game::handle_game_action(int user_id, const json::json &action) {
        if (is_waiting()) {
            throw lobby_error("ERROR_GAME_STATE_WAITING");
        }
        player_ptr origin = find_player_by_userid(user_id);
        if (!origin) {
            throw lobby_error("ERROR_USER_NOT_CONTROLLING_PLAYER");
        }
        game_table::handle_game_action(origin, action);
    }

    void game::get_pending_updates(std::span<const int> user_ids, consumer_callback<int, update_content> callback) {
        while (!m_updates.empty()) {
            const game_update_record &update = m_updates.front();
            
            for (int user_id : user_ids) {
                if (update.target.matches(find_player_by_userid(user_id))) {
                    callback(user_id, update.content);
                }
            }

            m_updates.pop_front();
        }
    }

    void game::get_spectator_join_updates(consumer_callback<update_content> callback) {
        auto yield_update = [&](const game_update &update) {
            callback(serialize_update(update));
        };

        yield_update(make_preload_assets_update(this));

        yield_update(game_updates::player_add{ m_players });

        for (player_ptr p : m_players) {
            yield_update(game_updates::player_flags{ p, p->m_player_flags });
        }

        yield_update(game_updates::player_order{ m_players, 0ms });

        auto add_cards = [&](pocket_type pocket, player_ptr owner = nullptr) {
            auto &range = get_pocket(pocket, owner);
            if (!range.empty()) {
                yield_update(game_updates::add_cards{ range, pocket, owner });
            }
            for (card_ptr c : range) {
                if (c->get_visibility() == card_visibility::shown) {
                    yield_update(game_updates::show_card{ c, *c, 0ms });
                }
                for (const auto &[token, count] : c->tokens) {
                    if (count > 0) {
                        yield_update(game_updates::add_tokens{ token, count, token_positions::card{c} });
                    }
                }
                if (c->flags.check(card_flag::inactive)) {
                    yield_update(game_updates::tap_card{ c, true, 0ms });
                }
            }
        };

        add_cards(pocket_type::button_row);
        add_cards(pocket_type::main_deck);
        add_cards(pocket_type::shop_deck);

        add_cards(pocket_type::discard_pile);
        add_cards(pocket_type::selection);
        add_cards(pocket_type::shop_selection);
        add_cards(pocket_type::hidden_deck);

        if (train_position != 0) {
            yield_update(game_updates::move_train{ train_position, 0ms });
        }

        add_cards(pocket_type::stations);
        add_cards(pocket_type::train_deck);
        add_cards(pocket_type::train);

        add_cards(pocket_type::scenario_deck);
        add_cards(pocket_type::scenario_card);
        add_cards(pocket_type::wws_scenario_deck);
        add_cards(pocket_type::wws_scenario_card);

        add_cards(pocket_type::feats_deck);
        add_cards(pocket_type::feats_discard);
        add_cards(pocket_type::feats);
        
        for (const auto &[token, count] : tokens) {
            if (count > 0) {
                yield_update(game_updates::add_tokens{ token, count, token_positions::table{} });
            }
        }

        for (player_ptr p : m_players) {
            if (p->check_player_flags(player_flag::role_revealed)) {
                yield_update(game_updates::player_show_role{ p, p->m_role, 0ms });
            }

            if (!p->check_player_flags(player_flag::removed)) {
                add_cards(pocket_type::player_character, p);

                add_cards(pocket_type::player_table, p);
                add_cards(pocket_type::player_hand, p);

                yield_update(game_updates::player_hp{ p, p->m_hp, 0ms });

                for (const auto &[token, count] : p->tokens) {
                    if (count > 0) {
                        yield_update(game_updates::add_tokens{ token, count, token_positions::player{p} });
                    }
                }
            }
        }

        if (m_playing) {
            yield_update(game_updates::switch_turn{ m_playing });
        }
        if (!is_waiting()) {
            if (auto req = top_request()) {
                yield_update(make_request_update(*req));
            }
        }

        yield_update(game_updates::game_flags{ m_game_flags });
    }

    void game::get_rejoin_updates(int user_id, consumer_callback<update_content> callback) {
        auto yield_update = [&](const game_update &update) {
            callback(serialize_update(update));
        };

        player_ptr target = find_player_by_userid(user_id);

        if (target) {
            yield_update(game_updates::player_add{ target });

            if (!target->check_player_flags(player_flag::role_revealed)) {
                yield_update(game_updates::player_show_role{ target, target->m_role, 0ms });
            }

            for (card_ptr c : target->m_characters) {
                if (c->get_visibility() == card_visibility::show_owner) {
                    yield_update(game_updates::show_card{ c, *c, 0ms });
                }
            }

            if (!check_flags(game_flag::hands_shown)) {
                for (player_ptr p : m_players) {
                    for (card_ptr c : p->m_hand) {
                        if (c->visibility.matches(target)) {
                            yield_update(game_updates::show_card{ c, *c, 0ms });
                        }
                    }
                }
            }

            for (card_ptr c : m_selection) {
                if (c->visibility.matches(target)) {
                    yield_update(game_updates::show_card{ c, *c, 0ms });
                }
            }

            if (!is_game_over() && !is_waiting()) {
                if (auto req = top_request()) {
                    yield_update(make_request_update(*req, target));
                } else if (target == m_playing) {
                    yield_update(make_status_ready_update(target));
                }
            }
        }

        yield_update(game_updates::clear_logs{});
        
        for (const auto &[upd_target, content] : m_saved_log) {
            if (upd_target.matches(target)) {
                callback(content);
            }
        }
    }

    static bool matches_expansions(const expansion_list &lhs, const expansion_set &rhs) {
        for (ruleset_ptr ruleset : lhs) {
            if (!rhs.contains(ruleset)) {
                return false;
            }
        }
        return true;
    }

    void game::rejoin_user(int old_user_id, int new_user_id) {
        player_ptr target = find_player_by_userid(old_user_id);
        if (!target) {
            throw lobby_error("ERROR_CANNOT_FIND_PLAYER");
        }
        update_player_userid(target, new_user_id);
        add_update(game_updates::player_add{ target });

        if (target->is_bot()) {
            commit_bot_rejoin(target);
        }
    }

    void game::start_game(std::span<int> user_ids) {
        add_players(user_ids);
        m_tracking.init_tracking(this);

        add_listener<event_type::on_turn_switch>(nullptr, [this](player_ptr origin) {
            if (origin == m_first_player) {
                ++m_round_count;
            }
        });

        add_listener<event_type::on_turn_start>(nullptr, [this](player_ptr origin) {
            request_ai_advice(origin);
        });

        // bots react with emotes, like players do
        add_listener<event_type::on_hit>(nullptr, [this](card_ptr origin_card, player_ptr origin, player_ptr target, int damage, effect_flags flags) {
            if (origin && origin != target) queue_bot_emote(target, "angry", origin, 40);
        });
        add_listener<event_type::on_missed>(nullptr, [this](card_ptr missed_card, effect_flags flags, shared_request_bang req) {
            if (req) queue_bot_emote(req->target, "happy", nullptr, 25);
        });
        add_listener<event_type::on_heal>(nullptr, [this](card_ptr origin_card, player_ptr origin, player_ptr target, int amount) {
            if (origin && origin != target) queue_bot_emote(target, m_emote_rng() % 2 ? "beer" : "thumbsup", origin, 40);
        });
        add_listener<event_type::on_player_eliminated>(nullptr, [this](player_ptr origin, player_ptr target, death_type type) {
            if (type == death_type::death) {
                if (origin && origin != target) queue_bot_emote(origin, m_emote_rng() % 2 ? "laugh" : "trophy", nullptr, 60);
                queue_bot_emote(target, "skull", nullptr, 50);
            }
        });

        for (ruleset_ptr ruleset : m_options.expansions) {
            ruleset->on_apply(this);
        }

        add_update(game_updates::player_add{ m_players });
        
        auto add_cards = [&](auto &&cards, pocket_type pocket, card_list *add_to_list = nullptr) {
            if (!add_to_list && pocket != pocket_type::none) add_to_list = &get_pocket(pocket);

            int count = 0;
            for (const card_data &c : cards) {
                if (!matches_expansions(c.expansion, m_options.expansions)) {
                    continue;
                }

                card_ptr new_card = add_card(c);
                new_card->pocket = pocket;
                
                if (add_to_list) {
                    add_to_list->push_back(new_card);
                }
                ++count;
            }
            return count;
        };

        if (add_cards(bang_cards.button_row, pocket_type::button_row)) {
            add_update(game_updates::add_cards{ m_button_row, pocket_type::button_row });
            for (card_ptr c : m_button_row) {
                c->set_visibility(card_visibility::shown, nullptr, true);
            }
        }

        if (add_cards(bang_cards.hidden, pocket_type::hidden_deck)) {
            add_update(game_updates::add_cards{ m_hidden_deck, pocket_type::hidden_deck });
            for (card_ptr c : m_hidden_deck) {
                c->set_visibility(card_visibility::shown, nullptr, true);
            }
        }

        if (add_cards(bang_cards.deck, pocket_type::main_deck)) {
            shuffle_cards_and_ids(m_deck);
            add_update(game_updates::add_cards{ m_deck, pocket_type::main_deck });
        }

        if (add_cards(bang_cards.goldrush, pocket_type::shop_deck)) {
            shuffle_cards_and_ids(m_shop_deck);
            add_update(game_updates::add_cards{ m_shop_deck, pocket_type::shop_deck });
        }

        if (add_cards(bang_cards.train, pocket_type::train_deck)) {
            shuffle_cards_and_ids(m_train_deck);
            add_update(game_updates::add_cards{ m_train_deck, pocket_type::train_deck });
        }

        if (add_cards(bang_cards.feats, pocket_type::feats_deck)) {
            shuffle_cards_and_ids(m_feats_deck);
            add_update(game_updates::add_cards{ m_feats_deck, pocket_type::feats_deck });
        }
        
        player_role roles[] = {
            player_role::sheriff,
            player_role::outlaw,
            player_role::outlaw,
            player_role::renegade,
            player_role::deputy,
            player_role::outlaw,
            player_role::deputy,
            player_role::renegade
        };

        player_role roles_3players[] = {
            player_role::deputy_3p,
            player_role::outlaw_3p,
            player_role::renegade_3p
        };

        auto role_ptr = m_players.size() > 3 ? roles : roles_3players;

        rn::shuffle(role_ptr, role_ptr + m_players.size(), rng);

        // owner_role option: give the lobby owner the chosen role by swapping with whoever was dealt it
        if (m_options.owner_role > 0) {
            player_role wanted = m_players.size() > 3
                ? std::array{ player_role::sheriff, player_role::deputy, player_role::outlaw, player_role::renegade }[m_options.owner_role - 1]
                : std::array{ player_role::unknown, player_role::deputy_3p, player_role::outlaw_3p, player_role::renegade_3p }[m_options.owner_role - 1];
            auto owner_it = rn::find(m_players, m_owner_user_id, &player::user_id);
            if (wanted != player_role::unknown && owner_it != m_players.end()) {
                size_t owner_index = owner_it - m_players.begin();
                for (size_t i = 0; i < m_players.size(); ++i) {
                    if (role_ptr[i] == wanted) {
                        std::swap(role_ptr[i], role_ptr[owner_index]);
                        break;
                    }
                }
            }
        }

        for (player_ptr p : m_players) {
            p->set_role(*role_ptr++, true);
        }

        m_first_player = *rn::find(m_players,
            m_players.size() > 3 ? player_role::sheriff : player_role::deputy_3p, &player::m_role);

        auto is_last_scenario_card = [](card_ptr c) {
            return c->has_tag(tag_type::last_scenario_card);
        };

        if (add_cards(bang_cards.highnoon, pocket_type::scenario_deck) + add_cards(bang_cards.fistfulofcards, pocket_type::scenario_deck)) {
            auto range = rn::stable_partition(m_scenario_deck, is_last_scenario_card);
            shuffle_cards_and_ids({ range.begin(), range.end() });
            if (range.size() > m_options.scenario_deck_size) {
                m_scenario_deck.erase(range.begin() + m_options.scenario_deck_size, range.end());
            }
            add_update(game_updates::add_cards{ m_scenario_deck, pocket_type::scenario_deck });
        }

        if (add_cards(bang_cards.wildwestshow, pocket_type::wws_scenario_deck)) {
            auto range = rn::stable_partition(m_wws_scenario_deck, is_last_scenario_card);
            shuffle_cards_and_ids({ range.begin(), range.end() });
            add_update(game_updates::add_cards{ m_wws_scenario_deck, pocket_type::wws_scenario_deck });
        }

        add_cards(bang_cards.stations, pocket_type::none, &m_stations_deck);
        add_cards(bang_cards.locomotive, pocket_type::none, &m_locomotive);

        add_cards(bang_cards.legends, pocket_type::none, &m_legends);

        if (add_cards(bang_cards.characters | rv::filter([&](const card_data &c) {
            return !m_options.only_base_characters
                || c.expansion.empty()
                || c.has_tag(tag_type::force_allow);
        }), pocket_type::none, &m_characters)) {
            shuffle_cards_and_ids(m_characters);
        }
        
        add_update(make_preload_assets_update(this));

        if (!call_event(event_type::on_assign_characters{ m_first_player })) {
            {
                // "All characters": every player gets an equal share of all the characters (characters / players)
                // to choose from, instead of everyone picking in turn from one shared pool.
                auto character_it = m_characters.begin();
                int max_character_choice = m_characters.size() / num_alive();
                int choice = m_options.all_characters ? max_character_choice : m_options.character_choice;
                for (player_ptr p : range_alive_players(m_first_player)) {
                    if (choice > 1) {
                        card_list characters;
                        const int count = std::min(max_character_choice, choice);
                        characters.reserve(count);
                        for (int i=0; i<count; ++i) {
                            characters.push_back(*character_it++);
                        }

                        add_cards_to(std::move(characters), pocket_type::player_hand, p, card_visibility::show_owner);
                        queue_request<request_characterchoice>(p);
                    } else {
                        card_ptr target_card = *character_it++;
                        add_log("LOG_CHARACTER_CHOICE", p, target_card);
                        p->set_character(target_card);
                        p->set_hp(p->get_character_max_hp(), true);
                    }
                }
            }
        }

        queue_action([this] {
            add_log("LOG_GAME_START");
            play_sound(sound_id::gamestart);

            auto players = range_alive_players(m_first_player);
            auto initial_cards = players
                | rv::transform([this](const_player_ptr p) {
                    return call_event(event_type::count_initial_cards{ p }).value_or(p->m_hp);
                })
                | rn::to<std::vector>();

            int cycles = rn::max(initial_cards);
            for (int i=0; i<cycles; ++i) {
                for (const auto &[p, ncards] : rv::zip(players, initial_cards)) {
                    if (p->m_hand.size() < ncards) {
                        p->draw_card();
                    }
                }
            }

            call_event(event_type::on_game_setup{ m_first_player });
        });

        queue_action([this]{
            start_next_turn();
        });

        commit_updates();
    }

    ticks game::get_inactivity_timeout() const {
        return std::chrono::duration_cast<ticks>(m_options.inactivity_timer);
    }

    // The inactivity timer ran out: let the bot AI make the move for the human the game is waiting for
    // (the target of the pending request, any human who can answer it, or the player whose turn it is).
    request_state game::on_inactivity() {
        auto try_play = [&](player_ptr p) -> std::optional<request_state> {
            if (!p || p->is_bot() || !p->alive()) return std::nullopt;
            request_state state = request_bot_play(p, true);
            if (std::holds_alternative<request_states::next>(state)) {
                add_log("LOG_PLAYER_INACTIVE", p);
                return state;
            }
            return std::nullopt;
        };
        if (auto req = top_request()) {
            if (auto state = try_play(req->target)) return *state;
            for (player_ptr p : m_players) {
                if (auto state = try_play(p)) return *state;
            }
        } else if (auto state = try_play(m_playing)) {
            return *state;
        }
        return request_states::done{};
    }

    void game::tick() {
        game_table::tick();
        for (bot_emote &emote : m_bot_emotes) {
            emote.delay -= ticks{1};
        }
        for (auto &[user_id, cooldown] : m_bot_emote_cooldown) {
            if (cooldown > ticks{0}) cooldown -= ticks{1};
        }
        for (auto &[bot, state] : m_ai_bots) {
            if (state.pending_id != 0 && (state.wait -= ticks{1}) <= ticks{0}) {
                // no answer in time: play by the rules, a late answer is ignored
                state.pending_id = 0;
                ++state.failures;
            }
        }
    }

    // ---------- AI bots ----------

    static constexpr int max_ai_failures = 3;
    static constexpr size_t max_ai_say_bytes = 480; // 120 characters of Vietnamese can take up to 4 bytes each
    static constexpr std::string_view ai_emotes[] = {
        "happy", "angry", "sheriff", "deputy", "outlaw", "renegade", "trophy",
        "thumbsup", "laugh", "scared", "beer", "handshake", "skull"
    };

    void game::request_ai_advice(player_ptr bot) {
        if (!m_options.ai_bots || !m_ai_available || !bot || !bot->is_bot() || !bot->alive()) return;
        ai_bot_state &state = m_ai_bots[bot];
        if (state.failures >= max_ai_failures || state.pending_id != 0) return;
        state.pending_id = m_next_ai_local_id++;
        state.wait = std::chrono::duration_cast<ticks>(std::chrono::seconds{5});
        m_ai_outbox.emplace_back(state.pending_id, make_ai_request(bot));
    }

    bool game::is_waiting_ai_advice(player_ptr bot) const {
        auto it = m_ai_bots.find(bot);
        return it != m_ai_bots.end() && it->second.pending_id != 0;
    }

    void game::get_ai_requests(consumer_callback<int, std::string_view> callback) {
        for (const auto &[local_id, request] : m_ai_outbox) {
            callback(local_id, request);
        }
        m_ai_outbox.clear();
    }

    static std::string ai_card_names(const card_list &cards) {
        std::string ret = "[";
        for (const_card_ptr c : cards) {
            if (ret.size() > 1) ret += ',';
            ret += std::format("\"{}\"", c->name);
        }
        return ret + "]";
    }

    // Only what this bot may know: its own role and hand, the public table, the log lines it was sent.
    std::string game::make_ai_request(player_ptr bot) const {
        static constexpr std::string_view languages[] = { "en", "fr", "vi" };
        std::string players;
        for (player_ptr p : m_players) {
            if (!players.empty()) players += ',';
            card_ptr character = p->get_character();
            bool role_known = p == bot || p->is_sheriff() || p->check_player_flags(player_flag::role_revealed);
            players += std::format(
                "{{\"id\":{},\"user_id\":{},\"character\":\"{}\",\"hp\":{},\"max_hp\":{},\"alive\":{},\"hand_count\":{},\"table\":{}{}}}",
                p->id, p->user_id, character ? character->name : std::string_view{}, int(p->m_hp), int(p->m_max_hp),
                p->alive() ? "true" : "false", p->m_hand.size(), ai_card_names(p->m_table),
                role_known ? std::format(",\"role\":\"{}\"", enums::to_string(p->m_role)) : std::string{});
        }

        std::vector<std::string_view> log_lines;
        for (const auto &[target, content] : m_saved_log) {
            if (target.matches(bot)) log_lines.push_back(content);
        }
        if (log_lines.size() > 30) log_lines.erase(log_lines.begin(), log_lines.end() - 30);
        std::string log;
        for (std::string_view line : log_lines) {
            if (!log.empty()) log += ',';
            log += line;
        }

        std::string said;
        if (auto it = m_ai_bots.find(bot); it != m_ai_bots.end()) {
            for (const std::string &line : it->second.said) {
                if (!said.empty()) said += ',';
                said += json::to_string(line);
            }
        }

        card_ptr character = bot->get_character();
        return std::format(
            "{{\"language\":\"{}\",\"round\":{},\"self\":{{\"id\":{},\"user_id\":{},\"role\":\"{}\",\"character\":\"{}\",\"hp\":{},\"max_hp\":{},\"hand\":{},\"table\":{}}},"
            "\"players\":[{}],\"log\":[{}],\"said\":[{}]}}",
            languages[std::clamp(m_options.bot_chat_language, 0, 2)], m_round_count,
            bot->id, bot->user_id, enums::to_string(bot->m_role), character ? character->name : std::string_view{},
            int(bot->m_hp), int(bot->m_max_hp), ai_card_names(bot->m_hand), ai_card_names(bot->m_table),
            players, log, said);
    }

    static std::optional<player_role> parse_ai_role(const json::json &value) {
        if (!value.IsString()) return std::nullopt;
        std::string_view str{ value.GetString(), value.GetStringLength() };
        if (str == "sheriff") return player_role::sheriff;
        if (str == "deputy") return player_role::deputy;
        if (str == "outlaw") return player_role::outlaw;
        if (str == "renegade") return player_role::renegade;
        return std::nullopt;
    }

    static ai_plan parse_ai_plan(const json::json &value) {
        if (!value.IsString()) return ai_plan::none;
        std::string_view str{ value.GetString(), value.GetStringLength() };
        if (str == "lie_low") return ai_plan::lie_low;
        if (str == "attack_sheriff") return ai_plan::attack_sheriff;
        if (str == "protect_sheriff") return ai_plan::protect_sheriff;
        if (str == "hunt_outlaws") return ai_plan::hunt_outlaws;
        if (str == "survive") return ai_plan::survive;
        return ai_plan::none;
    }

    // The helper's answer; every field is checked on its own, anything invalid is ignored.
    void game::apply_ai_advice(int local_id, const json::json &value) {
        auto it = rn::find_if(m_ai_bots, [&](const auto &pair) { return pair.second.pending_id == local_id; });
        if (it == m_ai_bots.end()) return;
        player_ptr bot = it->first;
        ai_bot_state &state = it->second;
        state.pending_id = 0;

        if (!value.IsObject() || value.HasMember("error")) {
            ++state.failures;
            return;
        }
        // a bot stops asking after max_ai_failures failures in a row
        state.failures = 0;

        auto find_player = [&](int id) -> player_ptr {
            auto p = rn::find(m_players, id, &player::id);
            return p != m_players.end() ? *p : nullptr;
        };

        bot_advice advice;
        if (auto m = value.FindMember("suspects"); m != value.MemberEnd() && m->value.IsObject()) {
            for (auto &entry : m->value.GetObject()) {
                int id = 0;
                std::string_view key{ entry.name.GetString(), entry.name.GetStringLength() };
                std::from_chars(key.data(), key.data() + key.size(), id);
                if (player_ptr p = find_player(id); p && p != bot) {
                    if (auto role = parse_ai_role(entry.value)) {
                        advice.suspects[id] = *role;
                    }
                }
            }
        }
        if (auto m = value.FindMember("plan"); m != value.MemberEnd()) {
            advice.plan = parse_ai_plan(m->value);
        }
        if (auto m = value.FindMember("target_id"); m != value.MemberEnd() && m->value.IsInt()) {
            if (player_ptr p = find_player(m->value.GetInt()); p && p != bot && p->alive()) {
                advice.target_id = p->id;
            }
        }
        m_bot_advice[bot] = std::move(advice);

        auto random_delay = [&](int min_ms, int max_ms) {
            return std::chrono::duration_cast<ticks>(std::chrono::milliseconds{min_ms + int(m_emote_rng() % (max_ms - min_ms))});
        };
        if (auto m = value.FindMember("say"); m != value.MemberEnd() && m->value.IsString()) {
            std::string say{ m->value.GetString(), m->value.GetStringLength() };
            if (!say.empty() && say.size() <= max_ai_say_bytes && !say.starts_with("[[")) {
                state.said.push_back(say);
                if (state.said.size() > 3) state.said.erase(state.said.begin());
                queue_bot_chat(bot, std::move(say), random_delay(500, 1500));
            }
        }
        if (auto m = value.FindMember("emote"); m != value.MemberEnd() && m->value.IsString()) {
            std::string_view emote{ m->value.GetString(), m->value.GetStringLength() };
            if (rn::contains(ai_emotes, emote)) {
                player_ptr target = nullptr;
                if (auto t = value.FindMember("emote_target"); t != value.MemberEnd() && t->value.IsInt()) {
                    target = find_player(t->value.GetInt());
                }
                queue_bot_chat(bot, target && target != bot
                    ? std::format("[[emote:{}@{}]]", emote, target->id)
                    : std::format("[[emote:{}]]", emote), random_delay(1500, 3000));
            }
        }
    }

    void game::queue_bot_chat(player_ptr bot, std::string message, ticks delay) {
        m_bot_emotes.push_back(bot_emote{ .user_id = bot->user_id, .message = std::move(message), .delay = delay });
        m_bot_emote_cooldown[bot->user_id] = std::chrono::duration_cast<ticks>(std::chrono::seconds{8});
    }

    void game::queue_bot_emote(player_ptr bot, std::string_view emote, player_ptr target, int percent) {
        if (!bot || !bot->is_bot()) return;
        if (static_cast<int>(m_emote_rng() % 100) >= percent) return;
        ticks &cooldown = m_bot_emote_cooldown[bot->user_id];
        if (cooldown > ticks{0}) return;
        cooldown = std::chrono::duration_cast<ticks>(std::chrono::seconds{8});
        m_bot_emotes.push_back(bot_emote{
            .user_id = bot->user_id,
            .message = target && target != bot
                ? std::format("[[emote:{}@{}]]", emote, target->id)
                : std::format("[[emote:{}]]", emote),
            .delay = std::chrono::duration_cast<ticks>(std::chrono::milliseconds{800 + m_emote_rng() % 1200})
        });
    }

    void game::get_bot_chat(consumer_callback<int, std::string_view> callback) {
        std::erase_if(m_bot_emotes, [&](const bot_emote &emote) {
            if (emote.delay > ticks{0}) return false;
            callback(emote.user_id, emote.message);
            return true;
        });
    }

    bool game::is_offline(player_ptr p) const {
        return p && !p->is_bot() && p->alive() && rn::contains(m_offline_users, p->user_id);
    }

    // Waiting for an offline human: the target of the pending request (any offline player if it has no target),
    // or the player in turn.
    ticks game::get_offline_timeout() {
        bool waiting = false;
        if (auto req = top_request()) {
            waiting = req->target ? is_offline(req->target) : rn::any_of(m_players, [&](player_ptr p) { return is_offline(p); });
        } else {
            waiting = is_offline(m_playing);
        }
        if (!waiting) return ticks{0};
        return std::max(std::chrono::duration_cast<ticks>(m_options.bot_play_timer), ticks{1});
    }

    // Like on_inactivity, but only for offline players and without a log line (it would repeat every move).
    request_state game::on_offline() {
        auto try_play = [&](player_ptr p) -> std::optional<request_state> {
            if (!is_offline(p)) return std::nullopt;
            request_state state = request_bot_play(p, true);
            if (std::holds_alternative<request_states::next>(state)) {
                return state;
            }
            return std::nullopt;
        };
        if (auto req = top_request()) {
            if (auto state = try_play(req->target)) return *state;
            for (player_ptr p : m_players) {
                if (auto state = try_play(p)) return *state;
            }
        } else if (auto state = try_play(m_playing)) {
            return *state;
        }
        return request_states::done{};
    }

    request_state game::send_request_status_ready() {
        if (!m_playing) {
            return request_states::done{};
        }
        if (!m_playing->alive()) {
            start_next_turn();
            return request_states::next{};
        }

        auto args = make_status_ready_update(m_playing);
        
        if (m_playing->empty_hand() && rn::all_of(args.play_cards, [](const playable_card_info &args) {
            return args.card->has_tag(tag_type::pass_turn);
        })) {
            m_playing->pass_turn();
            return request_states::next{};
        } else {
            add_update(update_target::includes(m_playing), std::move(args));
            return request_states::done{};
        }
    }

    void game::send_request_update() {
        auto req = top_request();
        auto spectator_target = update_target::excludes();
        for (player_ptr p : m_players) {
            spectator_target.add(p);
            if (!p->is_bot()) {
                add_update(update_target::includes(p), make_request_update(*req, p));
            }
        }
        add_update(std::move(spectator_target), make_request_update(*req));
    }

    void game::get_game_commands(bool enable_cheats, consumer_callback<chat_command> callback) const {
        if (enable_cheats) {
            callback({"give", "GIVE_CARD_DESCRIPTION", [&](game_lobby &lobby, int user_id, std::string_view card_name) {
                player_ptr target = find_player_by_userid(user_id);
                if (!target) {
                    throw lobby_error("ERROR_USER_NOT_CONTROLLING_PLAYER");
                }

                if (pending_requests() || is_waiting() || m_playing != target) {
                    throw lobby_error("ERROR_PLAYER_NOT_IN_TURN");
                }

                if (!give_card(target, card_name)) {
                    throw lobby_error("ERROR_CANNOT_FIND_CARD");
                }
            }});
        }

        callback({"seed", "GET_RNG_SEED_DESCRIPTION", [&](game_lobby &lobby, int user_id) {
            lobby.send_chat_message(user_id, {0,
                "GAME_SEED", {chat_format_arg::string{std::format("{}", rng_seed)}}, lobby_chat_flag::translated
            });
        }});
    }

}