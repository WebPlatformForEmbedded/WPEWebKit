/*
 * Copyright (C) 2017 Garmin Ltd. All rights reserved.
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

#ifndef IntDim_h
#define IntDim_h

namespace WebCore {

class IntDim {
    WTF_MAKE_FAST_ALLOCATED;
public:
    IntDim() : m_start(0), m_length(0) { }
    IntDim(int start, int length) : m_start(start), m_length(length) { }

    int start() const { return m_start; }
    int length() const { return m_length; }
    int max() const { return m_start + m_length; }

    void setStart(int start) { m_start = start; }
    void setLength(int length) { m_length = length; }
    void move(int delta) { m_start += delta; }

    void shiftStart(int delta)
    {
        m_start += delta;
        m_length -= delta;
    }

    void shiftMax(int delta) { m_length += delta; }

    bool contains(int pt) const { return pt >= start() && pt < max(); }
    bool contains(const IntDim& d) const { return d.start() >= start() && d.max() <= max(); }
    bool contains(int start, int length) const
    {
        IntDim d(start, length);
        return contains(d);
    }

    bool inside(const IntDim& d) const { return d.contains(*this); }
    bool inside(int start, int length) const
    {
        IntDim d(start, length);
        return inside(d);
    }

    bool intersects(const IntDim& d) const
    {
        return contains(d.start()) || d.contains(start());
    }

    bool intersects(int start, int length) const
    {
        IntDim d(start, length);
        return intersects(d);
    }

    IntDim intersection(const IntDim& d) const
    {
        int end = std::min(d.max(), max());
        if (contains(d.start()))
            return IntDim(d.start(), end - d.start());
        if (d.contains(start()))
            return IntDim(start(), end - start());
        return IntDim();
    }

    IntDim intersection(int start, int length) const
    {
        IntDim d(start, length);
        return intersection(d);
    }

    bool isEmpty() const { return m_length == 0; };

private:
    int m_start, m_length;
};

inline bool operator==(const IntDim& a, const IntDim& b)
{
    return a.start() == b.start() && a.length() == b.length();
}

inline bool operator!=(const IntDim& a, const IntDim& b)
{
    return a.start() != b.start() || a.length() != b.length();
}

} // namespace WebCore

#endif // IntDim_h

