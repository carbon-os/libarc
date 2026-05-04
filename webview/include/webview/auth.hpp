#pragma once
#include <string>

namespace webview {

class AuthChallenge {
public:
    std::string host;
    std::string realm;
    bool        is_proxy = false;

    void respond(std::string user, std::string password) {
        user_     = std::move(user);
        password_ = std::move(password);
        action_   = Action::Respond;
    }
    void cancel() { action_ = Action::Cancel; }

    enum class Action { None, Respond, Cancel };
    Action             action()   const { return action_; }
    const std::string& user()     const { return user_; }
    const std::string& password() const { return password_; }

private:
    Action      action_   = Action::None;
    std::string user_;
    std::string password_;
};

} // namespace webview