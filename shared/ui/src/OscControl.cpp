// SPDX-FileCopyrightText: 2026 Trond Lossius
// SPDX-License-Identifier: AGPL-3.0-or-later

/** @file OscControl.cpp
    @brief OSC messages in and out: the address scheme, ADM-OSC and the feedback of changes. */

#include "tlos/OscControl.h"
#include "tlos/Parameters.h"
#include "tlos/SpatialProcessor.h"

#include <algorithm>
#include <cmath>

namespace tlos
{
namespace
{
/// Pi, for the conversions between degrees and radians.
constexpr double kPi = 3.14159265358979323846;

/** The number in an argument, if it is a float or an int. */
std::optional<double> numberAt (const juce::OSCMessage& m, int index)
{
    if (index >= m.size())
        return std::nullopt;

    const auto& a = m[index];
    double v = 0.0;
    if (a.isFloat32())      v = (double) a.getFloat32();
    else if (a.isInt32())   v = (double) a.getInt32();
    else                    return std::nullopt;

    return std::isfinite (v) ? std::optional<double> (v) : std::nullopt;
}

/** Splits "/a/b/3" into {"a", "b", "3"}. */
juce::StringArray splitAddress (const juce::String& address)
{
    auto tokens = juce::StringArray::fromTokens (address, "/", "");
    tokens.removeEmptyStrings();
    return tokens;
}

/** The 1-based channel number in a token, or 0 if it is not a number in 1..64. */
int channelNumber (const juce::String& token)
{
    if (token.isEmpty() || ! token.containsOnly ("0123456789"))
        return 0;

    const int n = token.getIntValue();
    return n >= 1 && n <= kMaxChannels ? n : 0;
}

/** Sets a parameter to a real value inside a change gesture (the value is limited to the parameter's range). */
void setParameter (SpatialProcessor& p, const juce::String& id, double value)
{
    if (auto* parameter = p.getValueTreeState().getParameter (id))
    {
        parameter->beginChangeGesture();
        parameter->setValueNotifyingHost (parameter->convertTo0to1 ((float) value));
        parameter->endChangeGesture();
    }
}

/** The real value of a parameter. */
float readParameter (const SpatialProcessor& p, const juce::String& id)
{
    return p.getValueTreeState().getRawParameterValue (id)->load();
}
}

bool OscSettings::operator== (const OscSettings& o) const noexcept
{
    return receive == o.receive && receivePort == o.receivePort && send == o.send && sendHost == o.sendHost
        && sendPort == o.sendPort && format == o.format;
}

OscControl::OscControl (SpatialProcessor& p) : processor (p)
{
    receiver.addListener (this);
}

OscControl::~OscControl()
{
    stopTimer();
    receiver.removeListener (this);
    receiver.disconnect();
    sender.disconnect();
}

juce::String OscControl::getStatus() const
{
    juce::StringArray parts;

    if (receiveFailed)      parts.add ("port " + juce::String (settings.receivePort) + " is in use or not allowed");
    else if (receiving)     parts.add ("listening on port " + juce::String (settings.receivePort));

    if (sendFailed)         parts.add ("cannot send to " + settings.sendHost + ":" + juce::String (settings.sendPort));
    else if (sending)       parts.add ("sending to " + settings.sendHost + ":" + juce::String (settings.sendPort));

    return parts.isEmpty() ? juce::String ("off") : parts.joinIntoString ("; ");
}

void OscControl::configure (const OscSettings& s)
{
    const bool targetChanged = s.sendHost != settings.sendHost || s.sendPort != settings.sendPort || s.format != settings.format || s.send != settings.send;
    const bool listenChanged = s.receive != settings.receive || s.receivePort != settings.receivePort || (s.receive && ! receiving);
    settings = s;
    settings.receivePort = juce::jlimit (1, 65535, settings.receivePort);
    settings.sendPort = juce::jlimit (1, 65535, settings.sendPort);

    if (listenChanged)
    {
        receiver.disconnect();
        receiving = false;
        receiveFailed = false;
        if (settings.receive)
        {
            receiving = receiver.connect (settings.receivePort);
            receiveFailed = ! receiving;
        }
    }

    if (targetChanged)
    {
        stopTimer();
        sender.disconnect();
        sending = false;
        sendFailed = false;

        if (settings.send)
        {
            sending = sender.connect (settings.sendHost, settings.sendPort);
            sendFailed = ! sending;
        }

        sendEverything();
        if (sending || sendHook)
            startTimerHz (30);
    }
}

void OscControl::emit (const juce::OSCMessage& message)
{
    ++sent;

    if (sendHook)
        sendHook (message);
    else if (sending)
        sender.send (message);
}

bool OscControl::changed (Slot& slot, float a, float b, float c)
{
    const auto differs = [] (float x, float y) { return std::isnan (x) || std::abs (x - y) > 1e-6f; };

    if (! differs (slot.values[0], a) && ! differs (slot.values[1], b) && ! differs (slot.values[2], c))
        return false;

    slot.values = { a, b, c };
    return true;
}

void OscControl::sendEverything()
{
    for (auto* group : { &srcXY, &srcSpread, &srcTrim, &srcMute, &srcSolo, &spkXY, &spkTrim, &spkMute, &spkSolo, &spkExclude, &spkExtra, &admXYZ, &admGain, &admMute })
        group->fill (Slot {});
    globals.fill (Slot {});
}

void OscControl::sendChanges()
{
    if (! sending && ! sendHook)
        return;

    const bool own = settings.format != OscFormat::Adm, adm = settings.format != OscFormat::Own;
    const int n = processor.getActiveSources(), m = processor.getActiveSpeakers();
    const double room = processor.getRoomHalfExtent();
    const auto extra = processor.getSpeakerColumn();

    for (int i = 0; i < n; ++i)
    {
        const auto base = "/source/" + juce::String (i + 1);
        const float x = readParameter (processor, params::sourceId (i, 'x')), y = readParameter (processor, params::sourceId (i, 'y'));
        const float trim = readParameter (processor, params::sourceTrimId (i));
        const float mute = readParameter (processor, params::sourceMuteId (i)) >= 0.5f ? 1.0f : 0.0f;

        if (own)
        {
            if (changed (srcXY[(size_t) i], x, y))
                emit ({ juce::OSCAddressPattern (base + "/xy"), x, y });
            if (const float v = readParameter (processor, params::spreadId (i)); changed (srcSpread[(size_t) i], v))
                emit ({ juce::OSCAddressPattern (base + "/spread"), v });
            if (changed (srcTrim[(size_t) i], trim))
                emit ({ juce::OSCAddressPattern (base + "/trim"), trim });
            if (changed (srcMute[(size_t) i], mute))
                emit ({ juce::OSCAddressPattern (base + "/mute"), (juce::int32) mute });
            if (const float v = readParameter (processor, params::sourceSoloId (i)) >= 0.5f ? 1.0f : 0.0f; changed (srcSolo[(size_t) i], v))
                emit ({ juce::OSCAddressPattern (base + "/solo"), (juce::int32) v });
        }

        if (adm)
        {
            const auto admBase = "/adm/obj/" + juce::String (i + 1);
            const float nx = (float) juce::jlimit (-1.0, 1.0, (double) x / room), ny = (float) juce::jlimit (-1.0, 1.0, (double) y / room);
            if (changed (admXYZ[(size_t) i], nx, ny, 0.0f))
                emit ({ juce::OSCAddressPattern (admBase + "/xyz"), nx, ny, 0.0f });
            if (const float g = juce::Decibels::decibelsToGain (trim); changed (admGain[(size_t) i], g))
                emit ({ juce::OSCAddressPattern (admBase + "/gain"), g });
            if (changed (admMute[(size_t) i], mute))
                emit ({ juce::OSCAddressPattern (admBase + "/mute"), (juce::int32) mute });
        }
    }

    if (own)
    {
        for (int i = 0; i < m; ++i)
        {
            const auto base = "/speaker/" + juce::String (i + 1);
            const float x = readParameter (processor, params::speakerId (i, 'x')), y = readParameter (processor, params::speakerId (i, 'y'));
            if (changed (spkXY[(size_t) i], x, y))
                emit ({ juce::OSCAddressPattern (base + "/xy"), x, y });
            if (const float v = readParameter (processor, params::speakerTrimId (i)); changed (spkTrim[(size_t) i], v))
                emit ({ juce::OSCAddressPattern (base + "/trim"), v });
            if (const float v = readParameter (processor, params::speakerMuteId (i)) >= 0.5f ? 1.0f : 0.0f; changed (spkMute[(size_t) i], v))
                emit ({ juce::OSCAddressPattern (base + "/mute"), (juce::int32) v });
            if (const float v = readParameter (processor, params::speakerSoloId (i)) >= 0.5f ? 1.0f : 0.0f; changed (spkSolo[(size_t) i], v))
                emit ({ juce::OSCAddressPattern (base + "/solo"), (juce::int32) v });
            if (const float v = processor.isSpeakerExcluded (i) ? 1.0f : 0.0f; changed (spkExclude[(size_t) i], v))
                emit ({ juce::OSCAddressPattern (base + "/exclude"), (juce::int32) v });
            if (extra)
                if (const float v = readParameter (processor, extra->idFor (i)); changed (spkExtra[(size_t) i], v))
                    emit ({ juce::OSCAddressPattern (base + "/" + extra->attribute), v });
        }

        if (changed (globals[0], (float) room))
            emit ({ juce::OSCAddressPattern ("/room/size"), (float) room });
        if (const float v = (float) processor.getOutsideRolloff(); changed (globals[1], v))
            emit ({ juce::OSCAddressPattern ("/outside/rolloff"), v });
        if (changed (globals[2], (float) n))
            emit ({ juce::OSCAddressPattern ("/sources/count"), (juce::int32) n });
        if (changed (globals[3], (float) m))
            emit ({ juce::OSCAddressPattern ("/speakers/count"), (juce::int32) m });

        const auto pluginParameters = processor.getOscParameters();
        for (size_t k = 0; k < pluginParameters.size() && k < 4; ++k)
            if (const float v = readParameter (processor, pluginParameters[k].parameterId); changed (globals[4 + k], v))
                emit ({ juce::OSCAddressPattern (pluginParameters[k].address), v });
    }
}

void OscControl::oscMessageReceived (const juce::OSCMessage& message)
{
    handleMessage (message);
}

bool OscControl::isKnownAddress (const juce::String& address)
{
    const auto t = splitAddress (address);
    if (t.size() == 1 && t[0] == "dump")
        return true;
    if (t.size() == 2)
        return (t[0] == "room" && t[1] == "size") || (t[0] == "outside" && t[1] == "rolloff") || (t[1] == "count" && (t[0] == "sources" || t[0] == "speakers"));
    if (t.size() == 3 && (t[0] == "source" || t[0] == "speaker"))
        return channelNumber (t[1]) > 0;
    if (t.size() == 4 && t[0] == "adm" && t[1] == "obj")
        return channelNumber (t[2]) > 0;
    return false;
}

bool OscControl::handleMessage (const juce::OSCMessage& message)
{
    const auto tokens = splitAddress (message.getAddressPattern().toString());
    const auto number = [&] (int i) { return numberAt (message, i); };
    const auto flag = [&] { const auto v = numberAt (message, 0); return v ? std::optional<bool> (*v >= 0.5) : std::nullopt; };
    bool understood = false;

    if (tokens.size() == 1 && tokens[0] == "dump")
    {
        sendEverything();
        sendChanges();
        understood = true;
    }
    else if (tokens.size() == 2 && tokens[0] == "room" && tokens[1] == "size")
    {
        if (const auto v = number (0))
        {
            processor.setRoomHalfExtent (*v);
            understood = true;
        }
    }
    else if (tokens.size() == 2 && tokens[0] == "outside" && tokens[1] == "rolloff")
    {
        if (const auto v = number (0))
        {
            setParameter (processor, params::kOutsideRolloffId, *v);
            understood = true;
        }
    }
    else if (tokens.size() == 2 && tokens[1] == "count" && (tokens[0] == "sources" || tokens[0] == "speakers"))
    {
        if (const auto v = number (0))
        {
            setParameter (processor, tokens[0] == "sources" ? params::kNumSourcesId : params::kNumSpeakersId, std::round (*v));
            understood = true;
        }
    }
    else if (tokens.size() == 3 && (tokens[0] == "source" || tokens[0] == "speaker") && channelNumber (tokens[1]) > 0)
    {
        const bool source = tokens[0] == "source";
        const int i = channelNumber (tokens[1]) - 1;
        const auto& name = tokens[2];

        if (name == "xy")
        {
            const auto x = number (0), y = number (1);
            if (x && y)
            {
                setParameter (processor, source ? params::sourceId (i, 'x') : params::speakerId (i, 'x'), *x);
                setParameter (processor, source ? params::sourceId (i, 'y') : params::speakerId (i, 'y'), *y);
                understood = true;
            }
        }
        else if (name == "x" || name == "y")
        {
            if (const auto v = number (0))
            {
                setParameter (processor, source ? params::sourceId (i, name == "x" ? 'x' : 'y') : params::speakerId (i, name == "x" ? 'x' : 'y'), *v);
                understood = true;
            }
        }
        else if (name == "trim")
        {
            if (const auto v = number (0))
            {
                setParameter (processor, source ? params::sourceTrimId (i) : params::speakerTrimId (i), *v);
                understood = true;
            }
        }
        else if (name == "mute" || name == "solo")
        {
            if (const auto f = flag())
            {
                const auto id = name == "mute" ? (source ? params::sourceMuteId (i) : params::speakerMuteId (i))
                                               : (source ? params::sourceSoloId (i) : params::speakerSoloId (i));
                setParameter (processor, id, *f ? 1.0 : 0.0);
                understood = true;
            }
        }
        else if (source && name == "spread")
        {
            if (const auto v = number (0))
            {
                setParameter (processor, params::spreadId (i), *v);
                understood = true;
            }
        }
        else if (! source && name == "exclude")
        {
            if (const auto f = flag())
            {
                processor.setSpeakerExcluded (i, *f);
                understood = true;
            }
        }
        else if (! source)
        {
            if (const auto extra = processor.getSpeakerColumn(); extra && name == extra->attribute)
                if (const auto v = number (0))
                {
                    setParameter (processor, extra->idFor (i), *v);
                    understood = true;
                }
        }
    }
    else if (tokens.size() == 4 && tokens[0] == "adm" && tokens[1] == "obj" && channelNumber (tokens[2]) > 0)
    {
        const int i = channelNumber (tokens[2]) - 1;
        const auto& name = tokens[3];
        const double room = processor.getRoomHalfExtent();
        const std::function<void (double)> setX = [&] (double nx) { setParameter (processor, params::sourceId (i, 'x'), nx * room); };
        const std::function<void (double)> setY = [&] (double ny) { setParameter (processor, params::sourceId (i, 'y'), ny * room); };

        if (name == "xyz" || name == "xy")
        {
            const auto x = number (0), y = number (1);
            if (x && y)
            {
                setX (*x);
                setY (*y);
                understood = true;
            }
        }
        else if (name == "x" || name == "y")
        {
            if (const auto v = number (0))
            {
                (name == "x" ? setX : setY) (*v);
                understood = true;
            }
        }
        else if (name == "z")
        {
            understood = number (0).has_value(); // the plane is flat: accepted, ignored
        }
        else if (name == "aed" || name == "azim" || name == "elev" || name == "dist")
        {
            // Azimuth in degrees, counter-clockwise from the front (ADM); elevation in degrees; distance 0 to 1 of the room.
            // Whatever the message leaves out is taken from where the source is now.
            const double cx = (double) readParameter (processor, params::sourceId (i, 'x')) / room;
            const double cy = (double) readParameter (processor, params::sourceId (i, 'y')) / room;
            double azim = std::atan2 (-cx, cy) * 180.0 / kPi, elev = 0.0, dist = std::hypot (cx, cy);
            bool ok = true;

            if (name == "aed")
            {
                const auto a = number (0), e = number (1), d = number (2);
                ok = a && e && d;
                if (ok) { azim = *a; elev = *e; dist = *d; }
            }
            else
            {
                const auto v = number (0);
                ok = v.has_value();
                if (ok)
                {
                    if (name == "azim") azim = *v;
                    if (name == "elev") elev = *v;
                    if (name == "dist") dist = *v;
                }
            }

            if (ok)
            {
                const double horizontal = dist * std::cos (elev * kPi / 180.0);
                setX (-horizontal * std::sin (azim * kPi / 180.0));
                setY (horizontal * std::cos (azim * kPi / 180.0));
                understood = true;
            }
        }
        else if (name == "gain")
        {
            if (const auto g = number (0))
            {
                setParameter (processor, params::sourceTrimId (i), *g > 1e-6 ? 20.0 * std::log10 (*g) : (double) params::kMinTrimDb);
                understood = true;
            }
        }
        else if (name == "mute")
        {
            if (const auto f = flag())
            {
                setParameter (processor, params::sourceMuteId (i), *f ? 1.0 : 0.0);
                understood = true;
            }
        }
    }
    else if (tokens.size() == 2)
    {
        for (const auto& p : processor.getOscParameters())
            if (p.address == message.getAddressPattern().toString())
                if (const auto v = number (0))
                {
                    setParameter (processor, p.parameterId, *v);
                    understood = true;
                }
    }

    if (understood)
        ++received;

    return understood;
}
}
