/*
 * Copyright (C) 2014 Igalia S.L.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY APPLE INC. AND ITS CONTRIBUTORS ``AS IS''
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO,
 * THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL APPLE INC. OR ITS CONTRIBUTORS
 * BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF
 * THE POSSIBILITY OF SUCH DAMAGE.
 */

#include "config.h"
#include "RenderThemeWPE.h"

#include "FloatRoundedRect.h"
#include "NotImplemented.h"
#include "RenderBox.h"
#include "RenderElement.h"
#include "RenderObject.h"
#include "RenderStyle.h"
#include "UserAgentScripts.h"
#include "UserAgentStyleSheets.h"
#include <wtf/text/StringBuilder.h>

namespace WebCore {

const int menuArrowHeight = 6;
const int menuArrowWidth = menuArrowHeight * 2;
const int padding = 4;

RenderTheme& RenderTheme::singleton()
{
    static NeverDestroyed<Ref<RenderTheme>> theme(RenderThemeWPE::create());
    return theme.get();
}

void RenderThemeWPE::updateCachedSystemFontDescription(CSSValueID, FontCascadeDescription&) const
{
    notImplemented();
}

String RenderThemeWPE::extraDefaultStyleSheet()
{
    return String();
}

#if ENABLE(VIDEO)
String RenderThemeWPE::mediaControlsStyleSheet()
{
    return ASCIILiteral(mediaControlsBaseUserAgentStyleSheet);
}

String RenderThemeWPE::mediaControlsScript()
{
    StringBuilder scriptBuilder;
    scriptBuilder.append(mediaControlsLocalizedStringsJavaScript, sizeof(mediaControlsLocalizedStringsJavaScript));
    scriptBuilder.append(mediaControlsBaseJavaScript, sizeof(mediaControlsBaseJavaScript));
    return scriptBuilder.toString();
}
#endif

LengthBox RenderThemeWPE::popupInternalPaddingBox(const RenderStyle& style) const
{
    if (style.appearance() == NoControlPart)
        return { 0, 0, 0, 0 };

    return { padding, padding + (style.direction() == LTR ? (menuArrowWidth + padding) : 0), padding, padding + (style.direction() == RTL ? (menuArrowWidth + padding) : 0) };
}

bool RenderThemeWPE::paintMenuList(const RenderObject& object, const PaintInfo& paintInfo, const FloatRect& r)
{
    auto& context = paintInfo.context();
    auto states = extractControlStatesForRenderer(object);

    GraphicsContextStateSaver stateSaver(context);

    FloatSize corner(1, 1);
    FloatRoundedRect roundedRect(r, corner, corner, corner, corner);
    Path path;
    path.addRoundedRect(roundedRect);

    Color fillColor = states & ControlStates::PressedState ? makeRGB(244, 244, 244) : makeRGB(224, 224, 224);
    context.setFillColor(fillColor);
    context.fillPath(path);

    context.setStrokeThickness(1);
    context.setStrokeColor(makeRGB(94, 94, 94));
    context.strokePath(path);

    Path arrowPath;

    float leftAnchor;
    float center = r.y() + r.height() / 2.0f;
    float arrowTop = center - menuArrowHeight / 2.0f;
    float arrowBottom = center + menuArrowHeight / 2.0f;

    if (object.style().direction() == LTR)
        leftAnchor = r.maxX() - (padding + menuArrowWidth);
    else
        leftAnchor = r.x() + padding;

    arrowPath.moveTo(FloatPoint(leftAnchor, arrowTop));
    arrowPath.addLineTo(FloatPoint(leftAnchor + menuArrowWidth / 2.0f, arrowBottom));
    arrowPath.addLineTo(FloatPoint(leftAnchor + menuArrowWidth, arrowTop));

    context.setStrokeThickness(2);
    context.setStrokeColor(makeRGB(84, 84, 84));
    context.strokePath(arrowPath);

    return false;
}

bool RenderThemeWPE::paintMenuListButtonDecorations(const RenderBox& object, const PaintInfo& info, const FloatRect& rect)
{
    return paintMenuList(object, info, rect);
}


} // namespace WebCore
