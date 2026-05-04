#pragma once

// Shared memory protocol for ui::PixelView.
//
// The producer opens a POSIX shared memory region named "/ui_pv_<channel_id>"
// and maps at least sizeof(PixelChannelHeader) + data_size bytes.
// It fills in all header fields and writes pixel data immediately after the
// header. frame_count must be incremented LAST (after data is fully written)
// so the consumer can use it as a seqlock-style dirty flag.
//
// The consumer (PixelView) polls on its configured interval, re-renders
// whenever the observed frame_count changes.

#include <cstdint>
#include <string>

namespace ui {

struct PixelChannelHeader {
    uint64_t frame_count; // updated last; consumer detects changes here
    uint32_t magic;       // must equal kPixelChannelMagic
    uint32_t version;     // must equal kPixelChannelVersion
    uint32_t width;       // frame width in pixels
    uint32_t height;      // frame height in pixels
    uint32_t format;      // ui::PixelFormat cast to uint32_t
    uint32_t data_size;   // byte size of pixel data that follows this header
};

static_assert(sizeof(PixelChannelHeader) == 32, "PixelChannelHeader must be 32 bytes");

inline constexpr uint32_t kPixelChannelMagic   = 0x55495056; // 'UIPV'
inline constexpr uint32_t kPixelChannelVersion = 1;

inline std::string pixel_channel_shm_name(const std::string& channel_id) {
    return "/ui_pv_" + channel_id;
}

} // namespace ui