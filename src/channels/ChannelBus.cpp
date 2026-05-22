#include <channels/ChannelBus.hpp>
#include <algorithm>

namespace agentcpp::channels {

namespace {
std::int64_t nowMs() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
}
} // namespace

ChannelBus& ChannelBus::instance() {
    static ChannelBus inst;
    return inst;
}

std::uint64_t ChannelBus::publish(const std::string& channel,
                                  const std::string& sender,
                                  const std::string& text) {
    std::lock_guard<std::mutex> lk(mu_);
    Channel& c = channels_[channel];
    Message m;
    m.id       = c.next_id++;
    m.epoch_ms = nowMs();
    m.sender   = sender;
    m.text     = text;
    c.messages.push_back(std::move(m));
    if (c.messages.size() > ring_size_) {
        // Drop the oldest to maintain ring buffer size.
        std::size_t over = c.messages.size() - ring_size_;
        c.messages.erase(c.messages.begin(), c.messages.begin() + over);
    }
    return c.messages.back().id;
}

std::vector<Message> ChannelBus::read(const std::string& channel,
                                      std::uint64_t since_id) const {
    std::lock_guard<std::mutex> lk(mu_);
    auto it = channels_.find(channel);
    if (it == channels_.end()) return {};
    const auto& msgs = it->second.messages;
    std::vector<Message> out;
    out.reserve(msgs.size());
    for (const auto& m : msgs) if (m.id > since_id) out.push_back(m);
    return out;
}

std::vector<ChannelBus::ChannelInfo> ChannelBus::list() const {
    std::lock_guard<std::mutex> lk(mu_);
    std::vector<ChannelInfo> out;
    out.reserve(channels_.size());
    for (const auto& [name, c] : channels_) {
        ChannelInfo info;
        info.name           = name;
        info.message_count  = c.messages.size();
        info.latest_id      = c.messages.empty() ? 0 : c.messages.back().id;
        out.push_back(std::move(info));
    }
    std::sort(out.begin(), out.end(),
              [](const ChannelInfo& a, const ChannelInfo& b) { return a.name < b.name; });
    return out;
}

} // namespace agentcpp::channels
