#pragma once

#include <atomic>
#include <cstdint>
#include <filesystem>
#include <span>

namespace SimpleBSStreamChat::Songs {
    /// Writes every file of the zip held in `data` under `folder` (created as
    /// needed), inflating with the system zlib. Handles stored and deflated
    /// entries, which is all BeatSaver serves. Rejects encrypted and ZIP64
    /// archives, entry paths that would leave `folder`, and archives that
    /// would unpack to more than `maxBytes`; checks every file's CRC.
    /// Throws std::runtime_error, leaving whatever was written so far.
    void ExtractZip(std::span<uint8_t const> data, std::filesystem::path const& folder, uint64_t maxBytes, std::atomic<bool> const& stopping);
}
