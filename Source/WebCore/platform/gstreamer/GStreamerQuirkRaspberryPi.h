/*
 * Copyright (C) 2024 Igalia S.L
 * Copyright (C) 2024 Metrological Group B.V.
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

#pragma once

#if USE(GSTREAMER)

#include "GStreamerQuirks.h"

namespace WebCore {

// FIXME: We should remove this quirk with OMX and instead rely on V4L2 for the Raspberry Pi.
class GStreamerQuirkRaspberryPi final : public GStreamerQuirk {
public:
    const ASCIILiteral identifier() const final { return "RaspberryPi"_s; }

    Vector<String> extraSystemPlugins() const final { return { "gstomx"_s }; }
};

} // namespace WebCore

#endif // USE(GSTREAMER)
