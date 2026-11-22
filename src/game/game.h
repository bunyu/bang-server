#ifndef __GAME_H__
#define __GAME_H__

#include <map>
#include <random>
#include <string>
#include <vector>

#include "game_table.h"
#include "game_stats.h"

#include "net/game_interface.h"

namespace banggame {

    struct game : game_interface, game_table {
        using game_table::game_table;

        game_tracking m_tracking;

        // user id of the lobby owner (set before start_game), for the owner_role option
        int m_owner_user_id = 0;
        void set_owner_user_id(int user_id) override { m_owner_user_id = user_id; }

        ticks get_inactivity_timeout() const override;
        request_state on_inactivity() override;

        // user ids of offline humans (set by the lobby every tick)
        std::vector<int> m_offline_users;
        void set_offline_users(std::vector<int> user_ids) override { m_offline_users = std::move(user_ids); }
        bool is_offline(player_ptr p) const;
        ticks get_offline_timeout() override;
        request_state on_offline() override;

        game_report get_game_report() const override {
            return m_tracking.get_game_report(this);
        }

        void tick() override;

        // Bot emotes: queued with a short random delay, at most one per bot every few seconds.
        struct bot_emote {
            int user_id;
            std::string message;
            ticks delay;
        };
        std::vector<bot_emote> m_bot_emotes;
        std::map<int, ticks> m_bot_emote_cooldown;
        std::minstd_rand m_emote_rng{std::random_device{}()};
        void queue_bot_emote(player_ptr bot, std::string_view emote, player_ptr target, int percent);
        void get_bot_chat(consumer_callback<int, std::string_view> callback) override;
        
        void get_pending_updates(std::span<const int> user_ids, consumer_callback<int, update_content> callback) override;
        void get_spectator_join_updates(consumer_callback<update_content> callback) override;
        void get_rejoin_updates(int user_id, consumer_callback<update_content> callback) override;

        bool contains_user(int user_id) const override;

        void handle_game_action(int user_id, const json::json &action) override;

        void rejoin_user(int old_user_id, int new_user_id) override;
        void start_game(std::span<int> user_ids) override;

        bool is_game_over() const override {
            return game_table::is_game_over();
        }

        void get_game_commands(bool enable_cheats, consumer_callback<chat_command> callback) const override;

        ticks get_total_update_time() const override;
        void send_request_update() override;
        void send_request_status_clear() override {
            clear_request_status();
        }
        request_state send_request_status_ready() override;
        request_state request_bot_play(player_ptr origin, bool instant) override;
    };

}

#endif