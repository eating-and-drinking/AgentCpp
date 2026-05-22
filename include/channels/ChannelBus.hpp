#pragma once
#include <string>
#include <vector>
#include <unordered_map>
#include <mutex>
#include <cstdint>
#include <chrono>

namespace agentcpp::channels {

// One message in a channel.
struct Message {
    std::uint64_t   id;        // monotonically increasing per-channel
    std::int64_t    epoch_ms;  // wall-clock time when published
    std::string     sender;    // free-form author label (e.g. "main", "task-1")
    std::string     text;
};

// In-process publish/subscribe bus. Channels are auto-created on first use.
// Messages stay in memory for the lifetime of the process. Per-channel ring
// buffer keeps the most recent N messages (default 256).
//
// Singleton — multi-agent code shares one bus. Thread-safe.
class ChannelBus {
public:
    static ChannelBus& instance();

    // Publish a message. Returns the assigned id.
    std::uint64_t publish(const std::string& channel,
                          const std::string& sender,
                          const std::string& text);

    // Return all messages in `channel` with id > since_id, in order.
    std::vector<Message> read(const std::string& channel,
                              std::uint64_t since_id = 0) const;

    // List every known channel + its message count + latest id.
    struct ChannelInfo {
        std::string     name;
        std::size_t     message_count;
        std::uint64_t   latest_id;
    };
    std::vector<ChannelInfo> list() const;

    // Configure the per-channel ring buffer size.
    void setRingSize(std::size_t n) { ring_size_ = n ? n : 1; }

private:
    ChannelBus() = default;

    struct Channel {
        std::vector<Message>    messages;
        std::uint64_t           next_id = 1;
    };

    mutable std::mutex                              mu_;
    std::unordered_map<std::string, Channel>        channels_;
    std::size_t                                     ring_size_ = 256;
};

} // namespace agentcpp::channels
