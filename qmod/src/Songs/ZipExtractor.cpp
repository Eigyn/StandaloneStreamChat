#include "Songs/ZipExtractor.hpp"

#include <zlib.h>

#include <algorithm>
#include <array>
#include <fstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace SimpleBSStreamChat::Songs {
    namespace {
        constexpr uint32_t EndOfCentralDirectorySignature = 0x06054b50;
        constexpr uint32_t CentralHeaderSignature = 0x02014b50;
        constexpr uint32_t LocalHeaderSignature = 0x04034b50;
        constexpr size_t EndOfCentralDirectorySize = 22;
        constexpr size_t CentralHeaderSize = 46;
        constexpr size_t LocalHeaderSize = 30;
        constexpr size_t MaxEntries = 10000;

        [[noreturn]] void Fail(std::string const& what) { throw std::runtime_error("bad map zip: " + what); }

        struct Reader {
            std::span<uint8_t const> data;

            void Need(size_t at, size_t count) const {
                if (at > data.size() || count > data.size() - at)
                    Fail("truncated");
            }
            uint16_t U16(size_t at) const {
                Need(at, 2);
                return static_cast<uint16_t>(data[at] | (data[at + 1] << 8));
            }
            uint32_t U32(size_t at) const {
                Need(at, 4);
                return static_cast<uint32_t>(data[at]) | (static_cast<uint32_t>(data[at + 1]) << 8) | (static_cast<uint32_t>(data[at + 2]) << 16) |
                       (static_cast<uint32_t>(data[at + 3]) << 24);
            }
        };

        struct Entry {
            std::string name;
            uint16_t flags = 0;
            uint16_t method = 0;
            uint32_t crc = 0;
            uint32_t compressedSize = 0;
            uint32_t size = 0;
            uint32_t localOffset = 0;
        };

        // The end-of-central-directory record sits in the last 22 bytes, or
        // further back by up to a 64 KB archive comment.
        size_t FindEndOfCentralDirectory(Reader const& reader) {
            auto size = reader.data.size();
            if (size < EndOfCentralDirectorySize)
                Fail("too small");
            size_t lowest = size > EndOfCentralDirectorySize + 0xFFFF ? size - EndOfCentralDirectorySize - 0xFFFF : 0;
            for (size_t at = size - EndOfCentralDirectorySize + 1; at-- > lowest;) {
                if (reader.U32(at) == EndOfCentralDirectorySignature)
                    return at;
            }
            Fail("no central directory");
        }

        std::vector<Entry> ReadCentralDirectory(Reader const& reader) {
            auto end = FindEndOfCentralDirectory(reader);
            uint16_t count = reader.U16(end + 10);
            uint32_t directorySize = reader.U32(end + 12);
            uint32_t directoryOffset = reader.U32(end + 16);
            if (count == 0xFFFF || directorySize == 0xFFFFFFFF || directoryOffset == 0xFFFFFFFF)
                Fail("ZIP64 is not supported");
            if (count > MaxEntries)
                Fail("too many files");
            reader.Need(directoryOffset, directorySize);

            std::vector<Entry> entries;
            entries.reserve(count);
            size_t at = directoryOffset;
            for (uint16_t i = 0; i < count; i++) {
                if (reader.U32(at) != CentralHeaderSignature)
                    Fail("broken central directory");
                Entry entry;
                entry.flags = reader.U16(at + 8);
                entry.method = reader.U16(at + 10);
                entry.crc = reader.U32(at + 16);
                entry.compressedSize = reader.U32(at + 20);
                entry.size = reader.U32(at + 24);
                uint16_t nameLength = reader.U16(at + 28);
                uint16_t extraLength = reader.U16(at + 30);
                uint16_t commentLength = reader.U16(at + 32);
                entry.localOffset = reader.U32(at + 42);
                reader.Need(at + CentralHeaderSize, nameLength);
                entry.name.assign(reinterpret_cast<char const*>(reader.data.data() + at + CentralHeaderSize), nameLength);
                entries.push_back(std::move(entry));
                at += CentralHeaderSize + nameLength + extraLength + commentLength;
            }
            return entries;
        }

        // The entry's path below the target folder, built from its own parts
        // so nothing can climb out: "..", drive letters and empty or "." parts
        // are refused or dropped. Empty for directory entries.
        std::filesystem::path SafeRelativePath(std::string name) {
            std::replace(name.begin(), name.end(), '\\', '/');
            std::filesystem::path path;
            size_t start = 0;
            while (start < name.size()) {
                auto end = std::min(name.find('/', start), name.size());
                auto part = name.substr(start, end - start);
                if (part == ".." || part.find(':') != std::string::npos)
                    Fail("unsafe path " + name);
                if (!part.empty() && part != ".")
                    path /= part;
                start = end + 1;
            }
            if (!name.empty() && name.back() == '/')
                return {};  // a directory; files create their own folders
            return path;
        }

        void Write(std::ofstream& out, uint8_t const* bytes, size_t count) {
            out.write(reinterpret_cast<char const*>(bytes), static_cast<std::streamsize>(count));
            if (!out)
                throw std::runtime_error("could not write map files (storage full?)");
        }

        // Raw deflate (no zlib header), streamed to the file in 64 KB chunks.
        uLong Inflate(std::span<uint8_t const> compressed, std::ofstream& out, uint32_t expectedSize) {
            z_stream stream{};
            if (inflateInit2(&stream, -MAX_WBITS) != Z_OK)
                Fail("inflateInit2 failed");
            struct Cleanup {
                z_stream& stream;
                ~Cleanup() { inflateEnd(&stream); }
            } cleanup{stream};

            stream.next_in = const_cast<Bytef*>(compressed.data());
            stream.avail_in = static_cast<uInt>(compressed.size());
            std::array<uint8_t, 64 * 1024> buffer;
            uLong crc = crc32(0, Z_NULL, 0);
            uint64_t written = 0;
            int rc;
            do {
                stream.next_out = buffer.data();
                stream.avail_out = static_cast<uInt>(buffer.size());
                rc = inflate(&stream, Z_NO_FLUSH);
                // Z_BUF_ERROR here means the data ended mid-stream.
                if (rc != Z_OK && rc != Z_STREAM_END)
                    Fail("corrupt compressed data");
                size_t produced = buffer.size() - stream.avail_out;
                written += produced;
                if (written > expectedSize)
                    Fail("file larger than recorded");
                crc = crc32(crc, buffer.data(), static_cast<uInt>(produced));
                Write(out, buffer.data(), produced);
            } while (rc != Z_STREAM_END);
            if (written != expectedSize)
                Fail("file shorter than recorded");
            return crc;
        }
    }

    void ExtractZip(std::span<uint8_t const> data, std::filesystem::path const& folder, uint64_t maxBytes, std::atomic<bool> const& stopping) {
        Reader reader{data};
        auto entries = ReadCentralDirectory(reader);

        uint64_t total = 0;
        for (auto const& entry : entries)
            total += entry.size;
        if (total > maxBytes)
            Fail("unpacks to more than " + std::to_string(maxBytes >> 20) + " MB");

        std::filesystem::create_directories(folder);
        for (auto const& entry : entries) {
            if (stopping)
                throw std::runtime_error("cancelled");
            auto relative = SafeRelativePath(entry.name);
            if (relative.empty())
                continue;
            if (entry.flags & 0x1)
                Fail("encrypted");
            if (entry.method != 0 && entry.method != 8)
                Fail("unsupported compression in " + entry.name);

            // The local header repeats the name and has its own extra field;
            // sizes come from the central directory (the local copy can be
            // zero when the entry was streamed).
            if (reader.U32(entry.localOffset) != LocalHeaderSignature)
                Fail("broken local header");
            size_t start = static_cast<size_t>(entry.localOffset) + LocalHeaderSize + reader.U16(entry.localOffset + 26) + reader.U16(entry.localOffset + 28);
            reader.Need(start, entry.compressedSize);
            auto compressed = data.subspan(start, entry.compressedSize);

            auto path = folder / relative;
            std::filesystem::create_directories(path.parent_path());
            std::ofstream out(path, std::ios::binary | std::ios::trunc);
            if (!out)
                throw std::runtime_error("could not create " + relative.string());

            uLong crc;
            if (entry.method == 0) {
                if (entry.compressedSize != entry.size)
                    Fail("stored size mismatch");
                Write(out, compressed.data(), compressed.size());
                crc = crc32(crc32(0, Z_NULL, 0), compressed.data(), static_cast<uInt>(compressed.size()));
            } else {
                crc = Inflate(compressed, out, entry.size);
            }
            if (crc != entry.crc)
                Fail("checksum mismatch in " + entry.name);
        }
    }
}
