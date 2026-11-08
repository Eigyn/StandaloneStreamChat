#pragma once

#include "Compat.hpp"

#include <initializer_list>
#include <string>
#include <string_view>

// Null-safe reads over rapidjson, for the loosely-specified JSON that YouTube
// and Kick return: every lookup tolerates missing or wrongly-typed members.
namespace StandaloneStreamChat::Chat::Json {
    using Value = rapidjson::Value;

    /// Member `key` of an object; null if absent or `value` isn't an object.
    inline Value const* Get(Value const* value, std::string_view key) {
        if (!value || !value->IsObject())
            return nullptr;
        auto it = value->FindMember(rapidjson::Value(rapidjson::StringRef(key.data(), key.size())));
        return it == value->MemberEnd() ? nullptr : &it->value;
    }

    /// Path(v, {"a", "b"}) is v.a.b, or null if any step is missing.
    inline Value const* Path(Value const* value, std::initializer_list<std::string_view> keys) {
        for (auto key : keys)
            value = Get(value, key);
        return value;
    }

    inline bool IsArray(Value const* value) { return value && value->IsArray(); }

    /// Strings as-is, numbers and booleans as text, anything else empty.
    inline std::string Scalar(Value const* value) {
        if (!value)
            return {};
        if (value->IsString())
            return {value->GetString(), value->GetStringLength()};
        if (value->IsInt64())
            return std::to_string(value->GetInt64());
        if (value->IsUint64())
            return std::to_string(value->GetUint64());
        if (value->IsNumber())
            return std::to_string(value->GetDouble());
        if (value->IsBool())
            return value->GetBool() ? "true" : "false";
        return {};
    }

    /// The first non-empty Scalar among `keys` of `value`.
    inline std::string FirstScalar(Value const* value, std::initializer_list<std::string_view> keys) {
        for (auto key : keys) {
            auto text = Scalar(Get(value, key));
            if (!text.empty())
                return text;
        }
        return {};
    }

    /// `text` as a quoted, escaped JSON string literal.
    inline std::string Quote(std::string_view text) {
        rapidjson::StringBuffer buffer;
        rapidjson::Writer<rapidjson::StringBuffer> writer(buffer);
        writer.String(text.data(), static_cast<rapidjson::SizeType>(text.size()));
        return {buffer.GetString(), buffer.GetSize()};
    }
}
