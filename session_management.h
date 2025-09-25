#pragma once
#include <unordered_map>
#include <string>
#include <memory>
#include <mutex>
#include <deque>
#include <chrono>
#include <iostream>
#include <random>

using namespace std;
using namespace std::chrono;

class SimpleHash
{
public:
    static string hash_password(const string &password, const string &salt)
    {
        static hash<string> hasher;
        return to_string(hasher(password + salt));
    }

    static string generate_salt()
    {
        static random_device rd;
        static mt19937 gen(rd());
        static uniform_int_distribution<> dis(1000, 9999);
        return to_string(dis(gen));
    }
};

class UserDatabase
{
private:
    struct UserRecord
    {
        string username;
        string password_hash;
        string salt;
        bool is_market_maker = false;
        bool is_admin = false;
        bool is_active = true;
        string email;
        int64_t created_time;
        int failed_login_attempts = 0;
        int64_t last_failed_login = 0;
    };

    unordered_map<string, UserRecord> users_;
    mutex db_mutex_;

public:
    bool create_user(const string &username, const string &password,
                     bool is_market_maker, bool is_admin, const string &email)
    {
        lock_guard<mutex> lock(db_mutex_);
        auto it = users_.find(username);
        if (it != users_.end())
            return false;
        UserRecord user;
        user.username = username;
        user.salt = SimpleHash::generate_salt();
        user.password_hash = SimpleHash::hash_password(password, user.salt);
        user.is_market_maker = is_market_maker;
        user.is_admin = is_admin;
        user.email = email;
        user.created_time = duration_cast<seconds>(system_clock::now().time_since_epoch()).count();
        users_[username] = user;
        return true;
    }

    bool authenticate_user(const string &username, const string &password,
                           bool &is_market_maker, bool &is_admin)
    {
        lock_guard<mutex> lock(db_mutex_);
        auto it = users_.find(username);
        if (it == users_.end())
            return false;
        auto &user = it->second;
        int64_t now = duration_cast<seconds>(system_clock::now().time_since_epoch()).count();
        if (user.failed_login_attempts >= 5 && now - user.last_failed_login <= 300)
            return false;
        if (!user.is_active)
            return false;
        if (SimpleHash::hash_password(password, user.salt) != user.password_hash)
        {
            user.failed_login_attempts++;
            user.last_failed_login = now;
            return false;
        }

        user.failed_login_attempts = 0;

        is_market_maker = user.is_market_maker;
        is_admin = user.is_admin;
        return true;
    }

    bool is_user_active(const string &username)
    {
        lock_guard<mutex> lock(db_mutex_);
        auto it = users_.find(username);
        return (it != users_.end() && it->second.is_active);
    }

    void deactivate_user(const string &username)
    {
        lock_guard<mutex> lock(db_mutex_);
        auto it = users_.find(username);
        if (it != users_.end())
            it->second.is_active = false;
    }
};

// ip_manager.h - Minimal IPManager starter
#pragma once
#include <unordered_map>
#include <vector>
#include <string>
#include <mutex>
#include <chrono>

using namespace std;

class IPManager
{
private:
    unordered_map<string, vector<uint32_t>> ip_to_sessions_;

    unordered_map<string, int> failed_attempts_per_ip_;

    unordered_map<string, int64_t> ip_blacklist_;

    mutable mutex ip_mutex_;

    static constexpr int MAX_SESSIONS_PER_IP = 5;
    static constexpr int MAX_FAILED_ATTEMPTS = 10;
    static constexpr int BAN_DURATION_SECONDS = 3600; // 1 hour

public:
    IPManager() = default;

    bool is_ip_allowed(const string &ip)
    {
        lock_guard<mutex> lock(ip_mutex_);
        int64_t now = duration_cast<seconds>(system_clock::now().time_since_epoch()).count();
        auto it = ip_blacklist_.find(ip);
        if (it != ip_blacklist_.end())
        {
            if (it->second > now)
            {
                return false;
            }
            else
            {
                ip_blacklist_.erase(it);
            }
        }
        return true;
    }

    bool can_create_session(const string &ip)
    {
        lock_guard<mutex> lock(ip_mutex_);

        // 1. Find sessions for this IP
        // 2. Check if under MAX_SESSIONS_PER_IP limit
        auto it = ip_to_sessions_.find(ip);
        if (it != ip_to_sessions_.end() && it->second.size() >= MAX_SESSIONS_PER_IP)
        {
            return false;
        }

        return true;
    }

    void add_session(const string &ip, uint32_t session_id)
    {
        lock_guard<mutex> lock(ip_mutex_);

        // 1. Add session_id to the vector for this IP
        if (can_create_session(ip))
        {
            ip_to_sessions_[ip].push_back(session_id);
        }
    }

    void remove_session(const string &ip, uint32_t session_id)
    {
        lock_guard<mutex> lock(ip_mutex_);

        // 1. Find the IP in the map
        // 2. Remove session_id from its vector
        // 3. Clean up empty entries
        auto it = ip_to_sessions_.find(ip);
        if (it == ip_to_sessions_.end())
            return;
        it->second.erase(find(it->second.begin(), it->second.end(), session_id));
        if (it->second.empty())
        {
            ip_to_sessions_.erase(it);
        }
    }

    void record_failed_attempt(const string &ip)
    {
        lock_guard<mutex> lock(ip_mutex_);
        auto it = failed_attempts_per_ip_.find(ip);
        if (it == failed_attempts_per_ip_.end())
        {
            failed_attempts_per_ip_[ip] = 1;
        }
        else
            it->second++;
        if (failed_attempts_per_ip_[ip] >= MAX_FAILED_ATTEMPTS)
        {
            int64_t now = duration_cast<seconds>(system_clock::now().time_since_epoch()).count();
            ip_blacklist_[ip] = now + BAN_DURATION_SECONDS;
        }
    }

    void clear_failed_attempts(const string &ip)
    {
        lock_guard<mutex> lock(ip_mutex_);

        // 1. Remove IP from failed_attempts_per_ip_
        auto it = failed_attempts_per_ip_.find(ip);
        if (it != failed_attempts_per_ip_.end())
        {
            failed_attempts_per_ip_.erase(it);
        }
    }

    int64_t get_current_time_seconds() const
    {
        return chrono::duration_cast<chrono::seconds>(
                   chrono::system_clock::now().time_since_epoch())
            .count();
    }

    size_t get_session_count(const string &ip) const
    {
        lock_guard<mutex> lock(ip_mutex_);
        auto it = ip_to_sessions_.find(ip);
        return (it != ip_to_sessions_.end()) ? it->second.size() : 0;
    }
};

// ============= Session Class =============
class Session
{
private:
    uint32_t session_id_;
    string username_;
    string client_ip_;
    bool is_authenticated_;
    int64_t last_heartbeat_;
    int64_t login_time_;

    bool is_market_maker_;
    bool is_admin_;

    deque<int64_t> message_timestamps_;
    uint32_t messages_per_second_limit_;

    uint64_t total_messages_sent_;
    uint64_t total_orders_placed_;
    uint64_t total_cancellations_;

public:
    Session(uint32_t id, const string &username, const string &client_ip)
        : session_id_(id), username_(username), client_ip_(client_ip),
          is_authenticated_(false), is_market_maker_(false), is_admin_(false),
          messages_per_second_limit_(100), total_messages_sent_(0),
          total_orders_placed_(0), total_cancellations_(0)
    {
        auto now = steady_clock::now();
        last_heartbeat_ = duration_cast<milliseconds>(now.time_since_epoch()).count();
        login_time_ = last_heartbeat_;
    }

    bool authenticate(const string &password, UserDatabase &user_db)
    {
        bool is_mm = false, is_admin = false;

        if (user_db.authenticate_user(username_, password, is_mm, is_admin))
        {
            is_authenticated_ = true;
            is_market_maker_ = is_mm;
            is_admin_ = is_admin;
            update_heartbeat();
            return true;
        }

        return false;
    }

    void update_heartbeat()
    {
        auto now = steady_clock::now();
        last_heartbeat_ = duration_cast<milliseconds>(now.time_since_epoch()).count();
    }

    bool is_active() const
    {
        // 1. Check if (current_time - last_heartbeat_) < timeout_threshold
        // 2. Return false if session has timed out
        // 3. Consider different timeouts for different user types

        auto now = steady_clock::now();
        int64_t current_time = duration_cast<milliseconds>(now.time_since_epoch()).count();

        // 30 second timeout for regular users, 60 seconds for market makers
        int64_t timeout = is_market_maker_ ? 60000 : 30000;
        return (current_time - last_heartbeat_) < timeout;
    }

    bool is_rate_limited()
    {
        // 1. Clean up old timestamps (older than 1 second)
        // 2. Check if current message count exceeds limit
        // 3. Add current timestamp if not rate limited
        // 4. Consider different limits for different user types

        auto now = steady_clock::now();
        int64_t current_time = duration_cast<milliseconds>(now.time_since_epoch()).count();

        while (!message_timestamps_.empty() &&
               (current_time - message_timestamps_.front()) > 1000)
        {
            message_timestamps_.pop_front();
        }

        uint32_t limit = is_market_maker_ ? 200 : messages_per_second_limit_;
        if (message_timestamps_.size() >= limit)
        {
            return true;
        }

        message_timestamps_.push_back(current_time);
        total_messages_sent_++;
        return false;
    }

    void record_order_placed()
    {
        // 1. Increment order counter
        // 2. Consider adding order history tracking
        // 3. Update last activity timestamp

        total_orders_placed_++;
        update_heartbeat();
    }

    void record_cancellation()
    {
        // 1. Increment cancellation counter
        // 2. Consider tracking cancel/order ratio for monitoring

        total_cancellations_++;
    }

    uint32_t get_session_id() const { return session_id_; }
    const string &get_username() const { return username_; }
    const string &get_client_ip() const { return client_ip_; }
    bool is_authenticated() const { return is_authenticated_; }
    bool is_market_maker() const { return is_market_maker_; }
    bool is_admin() const { return is_admin_; }
    int64_t get_login_time() const { return login_time_; }
    uint64_t get_total_messages() const { return total_messages_sent_; }
    uint64_t get_total_orders() const { return total_orders_placed_; }
    uint64_t get_total_cancellations() const { return total_cancellations_; }

    bool can_place_orders() const
    {

        return is_authenticated_ && is_active();
    }

    bool can_cancel_orders() const
    {
        return is_authenticated_ && is_active();
    }

    bool can_access_market_data() const
    {
        return is_authenticated_;
    }
};

// ============= Session Manager Class =============
class SessionManager
{
private:
    unordered_map<uint32_t, unique_ptr<Session>> sessions_;
    unordered_map<string, uint32_t> username_to_session_;
    uint32_t next_session_id_;
    mutable mutex sessions_mutex_;
    UserDatabase user_db_;
    IPManager ip_manager_;

    static constexpr size_t MAX_SESSIONS = 1000;
    static constexpr size_t MAX_SESSIONS_PER_IP = 10;

public:
    SessionManager() : next_session_id_(1) {}

    bool create_user(const string &username, const string &password,
                     bool is_market_maker, bool is_admin, const string &email)
    {
        return user_db_.create_user(username, password, is_market_maker, is_admin, email);
    }

    bool authenticate_session(uint32_t session_id, const string &password)
    {
        Session *session = get_session(session_id);
        if (!session)
            return false;

        const string &client_ip = session->get_client_ip();

        if (!ip_manager_.is_ip_allowed(client_ip))
            return false;

        bool auth_success = session->authenticate(password, user_db_);

        if (auth_success)
        {
            ip_manager_.clear_failed_attempts(client_ip);
        }
        else
        {
            ip_manager_.record_failed_attempt(client_ip);
        }

        return auth_success;
    }

    uint32_t create_session(const string &username, const string &client_ip)
    {
        lock_guard<mutex> lock(sessions_mutex_);

        // 1. Check if max sessions reached globally
        if (sessions_.size() >= MAX_SESSIONS)
        {
            return 0;
        }

        // 2. Check IP restrictions
        if (!ip_manager_.is_ip_allowed(client_ip) || !ip_manager_.can_create_session(client_ip))
        {
            return 0;
        }

        // 3. Check if username already has active session (replace old session)
        auto username_it = username_to_session_.find(username);
        if (username_it != username_to_session_.end())
        {
            uint32_t old_session_id = username_it->second;
            auto old_session_it = sessions_.find(old_session_id);
            if (old_session_it != sessions_.end())
            {
                ip_manager_.remove_session(old_session_it->second->get_client_ip(), old_session_id);
                sessions_.erase(old_session_it);
            }
            username_to_session_.erase(username_it);
        }

        // 4. Create new session with unique ID
        uint32_t session_id = next_session_id_++;
        auto session = make_unique<Session>(session_id, username, client_ip);

        // 5. Add to both maps (id->session and username->id)
        username_to_session_[username] = session_id;
        sessions_[session_id] = std::move(session);

        // 6. Add session to IP tracking
        ip_manager_.add_session(client_ip, session_id);

        return session_id;
    }

    Session *get_session(uint32_t session_id)
    {
        lock_guard<mutex> lock(sessions_mutex_);
        auto it = sessions_.find(session_id);
        return (it != sessions_.end()) ? it->second.get() : nullptr;
    }

    Session *get_session_by_username(const string &username)
    {
        lock_guard<mutex> lock(sessions_mutex_);
        auto it = username_to_session_.find(username);
        if (it != username_to_session_.end())
        {
            return sessions_[it->second].get();
        }
        return nullptr;
    }

    bool remove_session(uint32_t session_id)
    {
        lock_guard<mutex> lock(sessions_mutex_);

        auto it = sessions_.find(session_id);
        if (it != sessions_.end())
        {
            // Get session info before removing
            const string &username = it->second->get_username();
            const string &client_ip = it->second->get_client_ip();

            // Remove from username mapping
            username_to_session_.erase(username);

            // Remove from IP tracking
            ip_manager_.remove_session(client_ip, session_id);

            // Remove session
            sessions_.erase(it);
            return true;
        }
        return false;
    }

    size_t cleanup_inactive_sessions()
    {

        lock_guard<mutex> lock(sessions_mutex_);

        size_t cleaned_up = 0;
        auto it = sessions_.begin();
        while (it != sessions_.end())
        {
            if (!it->second->is_active())
            {
                // Get session info before removing
                const string &username = it->second->get_username();
                const string &client_ip = it->second->get_client_ip();
                uint32_t session_id = it->second->get_session_id();

                // Remove from username mapping
                username_to_session_.erase(username);

                // Remove from IP tracking
                ip_manager_.remove_session(client_ip, session_id);

                // Remove session
                it = sessions_.erase(it);
                cleaned_up++;
            }
            else
            {
                ++it;
            }
        }

        return cleaned_up;
    }

    size_t active_session_count() const
    {
        lock_guard<mutex> lock(sessions_mutex_);
        return sessions_.size();
    }

    size_t authenticated_session_count() const
    {
        lock_guard<mutex> lock(sessions_mutex_);
        size_t count = 0;
        for (const auto &[id, session] : sessions_)
        {
            if (session->is_authenticated())
            {
                count++;
            }
        }
        return count;
    }

    void print_session_stats() const
    {
        lock_guard<mutex> lock(sessions_mutex_);

        cout << "\n=== SESSION MANAGER STATISTICS ===" << endl;
        cout << "Total Sessions: " << sessions_.size() << endl;
        cout << "Authenticated Sessions: " << authenticated_session_count() << endl;

        size_t mm_count = 0, admin_count = 0;
        for (const auto &[id, session] : sessions_)
        {
            if (session->is_market_maker())
                mm_count++;
            if (session->is_admin())
                admin_count++;
        }

        cout << "Market Maker Sessions: " << mm_count << endl;
        cout << "Admin Sessions: " << admin_count << endl;
        cout << "Regular Trader Sessions: " << (sessions_.size() - mm_count - admin_count) << endl;
    }

    vector<uint32_t> get_all_authenticated_sessions() const
    {

        lock_guard<mutex> lock(sessions_mutex_);
        vector<uint32_t> authenticated_sessions;

        for (const auto &[id, session] : sessions_)
        {
            if (session->is_authenticated())
            {
                authenticated_sessions.push_back(id);
            }
        }

        return authenticated_sessions;
    }

    vector<uint32_t> get_market_maker_sessions() const
    {
        lock_guard<mutex> lock(sessions_mutex_);
        vector<uint32_t> mm_sessions;

        for (const auto &[id, session] : sessions_)
        {
            if (session->is_authenticated() && session->is_market_maker())
            {
                mm_sessions.push_back(id);
            }
        }

        return mm_sessions;
    }

    size_t get_sessions_by_ip(const string &ip) const
    {
        return ip_manager_.get_session_count(ip);
    }
};
