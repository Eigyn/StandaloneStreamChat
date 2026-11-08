#pragma once

// The differences between the two Beat Saber versions this mod builds for:
// 1.45.3 (bs-cordl 4503, beatsaber-hook 9) by default, and 1.40.8 (bs-cordl
// 4008, beatsaber-hook 6) when SSC_BS_1_40 is defined by bs-1.40.8/.

#ifdef SSC_BS_1_40
#include "beatsaber-hook/shared/config/rapidjson-utils.hpp"
#include "beatsaber-hook/shared/utils/typedefs-wrappers.hpp"
#else
#include "beatsaber-hook/shared/rapidjson.hpp"
#include "beatsaber-hook/shared/safeptr.hpp"
#endif

#include "TMPro/TMP_Text.hpp"
#ifndef SSC_BS_1_40
#include "TMPro/TextWrappingModes.hpp"
#endif

#include <type_traits>

namespace StandaloneStreamChat {
    /// Holds a Unity object for the life of the mod without the GC freeing it.
    /// `T` is the pointer type, as with beatsaber-hook 9's safe_ptr.
#ifdef SSC_BS_1_40
    template <typename T>
    using SafeUnityPtr = SafePtrUnity<std::remove_pointer_t<T>>;
#else
    template <typename T>
    using SafeUnityPtr = safe_ptr<T>;
#endif

    /// `object` as a `U`, or null if it's another type. (UnityW::try_cast
    /// returns an optional on bs-cordl 4008 and a possibly-null UnityW on 4503.)
    template <typename U, typename T>
    U* TryCast(UnityW<T> object) {
        auto cast = object.template try_cast<U>();
#ifdef SSC_BS_1_40
        return cast ? static_cast<U*>(*cast) : nullptr;
#else
        return cast ? static_cast<U*>(cast) : nullptr;
#endif
    }

    /// TextMeshPro replaced enableWordWrapping with textWrappingMode after 1.40.
    inline void SetWordWrap(TMPro::TMP_Text* text, bool wrap) {
#ifdef SSC_BS_1_40
        text->set_enableWordWrapping(wrap);
#else
        text->set_textWrappingMode(wrap ? TMPro::TextWrappingModes::Normal : TMPro::TextWrappingModes::NoWrap);
#endif
    }
}
