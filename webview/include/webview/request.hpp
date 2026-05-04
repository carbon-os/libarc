#pragma once
#include <string>
#include <map>
#include <vector>
#include <cstdint>

namespace webview {

enum class ResourceType { Document, Script, Image, Fetch, Other };

struct ResourceResponse {
    int                                  status  = 200;
    std::map<std::string, std::string>   headers;
    std::vector<uint8_t>                 body;
};

class ResourceRequest {
public:
    std::string                        url;
    std::string                        method;
    std::map<std::string, std::string> headers;
    ResourceType                       resource_type = ResourceType::Other;

    void cancel()                      { action_ = Action::Cancel; }
    void redirect(std::string target)  { action_ = Action::Redirect; redirect_url_ = std::move(target); }
    void respond(ResourceResponse res) { action_ = Action::Respond;  response_ = std::move(res); }

    enum class Action { None, Cancel, Redirect, Respond };
    Action                  action()       const { return action_; }
    const std::string&      redirect_url() const { return redirect_url_; }
    const ResourceResponse& response()     const { return response_; }

private:
    Action          action_ = Action::None;
    std::string     redirect_url_;
    ResourceResponse response_;
};

} // namespace webview