/*
 * Copyright (C) 2021 Igalia S.L
 * Copyright (C) 2021 Metrological Group B.V.
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Library General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Library General Public License for more details.
 *
 * You should have received a copy of the GNU Library General Public License
 * aint with this library; see the file COPYING.LIB.  If not, write to
 * the Free Software Foundation, Inc., 51 Franklin Street, Fifth Floor,
 * Boston, MA 02110-1301, USA.
 */

#include "config.h"
#include "GStreamerEMEUtilities.h"
#include "InitDataRegistry.h"
#include "ISOProtectionSystemSpecificHeaderBox.h"

#include <wtf/text/Base64.h>

#if ENABLE(ENCRYPTED_MEDIA) && USE(GSTREAMER)

GST_DEBUG_CATEGORY_EXTERN(webkit_media_common_encryption_decrypt_debug_category);
#define GST_CAT_DEFAULT webkit_media_common_encryption_decrypt_debug_category

namespace WebCore {

struct GMarkupParseContextUserData {
    bool isParsingPssh { false };
    RefPtr<SharedBuffer> pssh;
};

static void markupStartElement(GMarkupParseContext*, const gchar* elementName, const gchar**, const gchar**, gpointer userDataPtr, GError**)
{
    GMarkupParseContextUserData* userData = static_cast<GMarkupParseContextUserData*>(userDataPtr);
    if (g_str_has_suffix(elementName, "pssh"))
        userData->isParsingPssh = true;
}

static void markupEndElement(GMarkupParseContext*, const gchar* elementName, gpointer userDataPtr, GError**)
{
    GMarkupParseContextUserData* userData = static_cast<GMarkupParseContextUserData*>(userDataPtr);
    if (g_str_has_suffix(elementName, "pssh")) {
        ASSERT(userData->isParsingPssh);
        userData->isParsingPssh = false;
    }
}

static void markupText(GMarkupParseContext*, const gchar* text, gsize textLength, gpointer userDataPtr, GError**)
{
    GMarkupParseContextUserData* userData = static_cast<GMarkupParseContextUserData*>(userDataPtr);
    if (userData->isParsingPssh) {
        std::optional<Vector<uint8_t>> pssh = base64Decode(text, textLength);
        if (pssh.has_value())
            userData->pssh = SharedBuffer::create(WTFMove(*pssh));
    }
}

static void markupPassthrough(GMarkupParseContext*, const gchar*, gsize, gpointer, GError**)
{
}

static void markupError(GMarkupParseContext*, GError*, gpointer)
{
}

static GMarkupParser markupParser { markupStartElement, markupEndElement, markupText, markupPassthrough, markupError };

RefPtr<SharedBuffer> InitData::extractCencIfNeeded(RefPtr<SharedBuffer>&& unparsedPayload)
{
    RefPtr<SharedBuffer> payload = WTFMove(unparsedPayload);
    if (!payload || !payload->size())
        return payload;

    GMarkupParseContextUserData userData;
    GUniquePtr<GMarkupParseContext> markupParseContext(g_markup_parse_context_new(&markupParser, (GMarkupParseFlags) 0, &userData, nullptr));

    if (g_markup_parse_context_parse(markupParseContext.get(), payload->dataAsCharPtr(), payload->size(), nullptr)) {
        if (userData.pssh)
            payload = WTFMove(userData.pssh);
        else
            GST_WARNING("XML was parsed but we could not find a viable base64 encoded pssh box");
    }

    return payload;
}

bool GStreamerEMEUtilities::cencHasInitDataForKeySystem(const InitData& initData, const String& keySystem)
{
    auto psshBoxes = InitDataRegistry::extractPsshBoxesFromCenc(*(initData.payload()));

    if (!psshBoxes) {
        return false;
    }

    auto keySystemToUuidRaw = [&keySystem]() -> auto& {
        static const Vector<uint8_t> s_ClearKeyUUIDRaw ({ 0x10,0x77,0xef,0xec,0xc0,0xb2,0x4d,0x02,0xac,0xe3,0x3c,0x1e,0x52,0xe2,0xfb,0x4b });
#if ENABLE(THUNDER)
        static const Vector<uint8_t> s_WidevineUUIDRaw ({ 0xed,0xef,0x8b,0xa9,0x79,0xd6,0x4a,0xce,0xa3,0xc8,0x27,0xdc,0xd5,0x1d,0x21,0xed });
        static const Vector<uint8_t> s_PlayReadyUUIDRaw({ 0x9a,0x04,0xf0,0x79,0x98,0x40,0x42,0x86,0xab,0x92,0xe6,0x5b,0xe0,0x88,0x5f,0x95 });
#endif
        static const Vector<uint8_t> s_InvalidUUIDRaw;

        if (isClearKeyKeySystem(keySystem))
            return s_ClearKeyUUIDRaw;

#if ENABLE(THUNDER)
        if (isWidevineKeySystem(keySystem))
            return s_WidevineUUIDRaw;

        if (isPlayReadyKeySystem(keySystem))
            return s_PlayReadyUUIDRaw;
#endif

        ASSERT_NOT_REACHED();
        return s_InvalidUUIDRaw;
    };

    auto& keySystemUuidRaw = keySystemToUuidRaw();

    for (auto& box : psshBoxes.value()) {
        if (box->systemID() == keySystemUuidRaw) {
            return true;
        }
    }
    return false;
}

}
#endif // ENABLE(ENCRYPTED_MEDIA) && USE(GSTREAMER)
