// SPDX-FileCopyrightText: 2026 Trond Lossius
// SPDX-License-Identifier: AGPL-3.0-or-later

/** @file OscControl.h
    @brief Control of a plugin over OSC (UDP): receives messages that set its parameters and sends changes. */

#pragma once

#include <juce_osc/juce_osc.h>

#include <array>
#include <functional>
#include <memory>
#include <optional>
#include <vector>

#include "tlos/Limits.h"

namespace tlos
{
class SpatialProcessor;

/** Which addresses OSC messages are sent with. Incoming messages of either kind are always understood. */
enum class OscFormat
{
    Own = 0,   ///< The plugin's own addresses (see OscControl).
    Adm = 1,   ///< ADM-OSC object messages (sources only).
    Both = 2   ///< Both of the above.
};

/** The OSC settings of a plugin instance. Saved with the session. Both directions are off by default: an instance
    that listens takes the port, so a second instance on the same port cannot. */
struct OscSettings
{
    bool receive = false;                 ///< Whether to listen for messages.
    int receivePort = 9000;               ///< UDP port to listen on.
    bool send = false;                    ///< Whether to send changes.
    juce::String sendHost { "127.0.0.1" }; ///< Where to send changes.
    int sendPort = 9001;                  ///< UDP port to send to.
    OscFormat format = OscFormat::Both;   ///< Addresses used for sending.

    /** @return Whether all fields are equal. */
    bool operator== (const OscSettings& other) const noexcept;
};

/** One plugin-specific parameter that can be reached over OSC. */
struct OscParameter
{
    juce::String address;       ///< OSC address, for example "/dbap/rolloff".
    juce::String parameterId;   ///< The parameter it sets.
};

/** OSC control of a SpatialProcessor: incoming messages set parameters and state, and changes are sent out.

    **Own addresses** (numbers are 1-based, values in metres, dB or 0/1; one float per argument, ints work too):

    | Address                                         | Arguments |
    |-------------------------------------------------|-----------|
    | /source/N/xy, /x, /y                            | position (xy: two floats) |
    | /source/N/spread, /trim, /mute, /solo           | spread in m, trim in dB, 0 or 1 |
    | /speaker/N/xy, /x, /y, /trim, /mute, /solo      | as for sources |
    | /speaker/N/exclude                              | 0 or 1: exclude from the panning |
    | /speaker/N/NAME                               | the plugin's own per-speaker value (DistPan: /speaker/N/weight) |
    | /room/size                                      | half width of the room in metres |
    | /outside/rolloff                                | the level falloff outside the layout, dB |
    | /sources/count, /speakers/count                 | number of sources / speakers in use |
    | the plugin's own (DistPan: /dbap/rolloff, /dbap/blur) | see SpatialProcessor::getOscParameters() |
    | /dump                                           | send everything now |

    **ADM-OSC** objects (`/adm/obj/N/...`, sources only): `xyz` (three floats, -1 to 1, x right, y front, z ignored),
    `x`, `y`, `z`, `aed` (azimuth in degrees counter-clockwise from the front, elevation in degrees, distance 0 to 1),
    `azim`, `elev`, `dist`, `gain` (linear) and `mute`. Normalised positions and distances are fractions of the room
    half width (SpatialProcessor::getRoomHalfExtent()); the elevation only shortens the distance (the plane is flat).

    Address patterns with wildcards are not supported; addresses are matched literally. Messages are applied on the
    message thread inside a change gesture, so hosts can record them. Changes are sent at 30 Hz, only what changed
    (everything once when sending starts, and on /dump). Message thread only. */
class OscControl final : private juce::OSCReceiver::Listener<juce::OSCReceiver::MessageLoopCallback>,
                         private juce::Timer
{
public:
    /** Creates the control for `processor`; nothing is open until configure() is called with something switched on. */
    explicit OscControl (SpatialProcessor& processor);

    /** Closes the port and stops sending. */
    ~OscControl() override;

    /** Applies the settings: opens or closes the receiving port and the sending socket. A change of the target
        sends everything again. */
    void configure (const OscSettings& settings);

    /** @return The settings in effect. */
    const OscSettings& getSettings() const noexcept { return settings; }

    /** @return A one-line description of what is going on ("listening on port 9000", "port 9000 is in use", ...). */
    juce::String getStatus() const;

    /** @return The number of messages applied since the control was created (for the settings panel and the tests). */
    int getReceivedCount() const noexcept { return received; }

    /** @return The number of messages sent since the control was created. */
    int getSentCount() const noexcept { return sent; }

    /** @return The number of incoming messages that were not understood (unknown address, wrong arguments, ...). */
    int getIgnoredCount() const noexcept { return ignored; }

    /** @return The last understood incoming message as text, for example "/source/2/xy 1.5 -2.5" (empty if none). */
    juce::String getLastReceived() const { return describe (lastReceivedMessage); }

    /** @return The last outgoing message as text (empty if none). */
    juce::String getLastSent() const { return describe (lastSentMessage); }

    /** @return The last incoming message that was not understood, as text (empty if none). */
    juce::String getLastIgnored() const { return describe (lastIgnoredMessage); }

    /** Applies one incoming message. This is what the receiver calls; tests call it directly.
        @return Whether the address was understood and the arguments were usable. */
    bool handleMessage (const juce::OSCMessage& message);

    /** Sends only what changed since the last time (the timer calls this at 30 Hz while sending is on). */
    void sendChanges();

    /** Forgets what was sent, so that the next sendChanges() sends everything. */
    void sendEverything();

    /** Instead of the network, hand outgoing messages to `hook` (for the tests). Pass an empty function to go back. */
    void setSendHook (std::function<void (const juce::OSCMessage&)> hook) { sendHook = std::move (hook); }

    /** @return Whether a message with this address (a literal match, see above) is one this control understands. */
    static bool isKnownAddress (const juce::String& address);

private:
    /** The receiver's callback (message thread). */
    void oscMessageReceived (const juce::OSCMessage& message) override;
    /** The timer's callback: sends what changed. */
    void timerCallback() override { sendChanges(); }

    /** @return A message as text: its address and its arguments. Empty for an empty optional. */
    static juce::String describe (const std::optional<juce::OSCMessage>& message);

    /** Sends a message through the hook or the socket. */
    void emit (const juce::OSCMessage& message);

    /** The last values sent for one address (NaN = never sent). */
    struct Slot
    {
        std::array<float, 3> values { std::nanf (""), std::nanf (""), std::nanf ("") };  ///< The arguments.
    };

    /** @return Whether the slot differs from `values`; if so, it is updated. */
    static bool changed (Slot& slot, float a, float b = 0.0f, float c = 0.0f);

    SpatialProcessor& processor;           ///< The processor that is controlled.
    OscSettings settings;                  ///< The settings in effect.
    juce::OSCReceiver receiver;            ///< The listening socket.
    juce::OSCSender sender;                ///< The sending socket.
    bool receiving = false;                ///< Whether the receiver is connected.
    bool sending = false;                  ///< Whether the sender is connected.
    bool receiveFailed = false;            ///< The port could not be opened.
    bool sendFailed = false;               ///< The sender could not be set up.
    int received = 0;                      ///< Messages applied.
    int sent = 0;                          ///< Messages sent.
    int ignored = 0;                       ///< Incoming messages that were not understood.
    std::optional<juce::OSCMessage> lastReceivedMessage;  ///< The last understood incoming message.
    std::optional<juce::OSCMessage> lastSentMessage;      ///< The last outgoing message.
    std::optional<juce::OSCMessage> lastIgnoredMessage;   ///< The last incoming message that was not understood.
    std::function<void (const juce::OSCMessage&)> sendHook;  ///< Where messages go instead of the network (tests).

    std::array<Slot, kMaxChannels> srcXY;       ///< Last position sent for each source.
    std::array<Slot, kMaxChannels> srcSpread;   ///< Last spread sent for each source.
    std::array<Slot, kMaxChannels> srcTrim;     ///< Last trim sent for each source.
    std::array<Slot, kMaxChannels> srcMute;     ///< Last mute switch sent for each source.
    std::array<Slot, kMaxChannels> srcSolo;     ///< Last solo switch sent for each source.
    std::array<Slot, kMaxChannels> spkXY;       ///< Last position sent for each speaker.
    std::array<Slot, kMaxChannels> spkTrim;     ///< Last trim sent for each speaker.
    std::array<Slot, kMaxChannels> spkMute;     ///< Last mute switch sent for each speaker.
    std::array<Slot, kMaxChannels> spkSolo;     ///< Last solo switch sent for each speaker.
    std::array<Slot, kMaxChannels> spkExclude;  ///< Last exclusion sent for each speaker.
    std::array<std::array<Slot, kMaxChannels>, 4> srcExtra;    ///< Last value of each of the plugin's own per-source parameters sent for each source.
    std::array<std::array<Slot, kMaxChannels>, 4> spkExtra;    ///< Last value of each of the plugin's own per-speaker parameters sent for each speaker.
    std::array<Slot, 24> globals;               ///< Last room size, outside rolloff, counts (4) and the plugin's own parameters (at most 20) sent.
    std::array<Slot, kMaxChannels> admXYZ;      ///< Last ADM-OSC position sent for each source.
    std::array<Slot, kMaxChannels> admGain;     ///< Last ADM-OSC gain sent for each source.
    std::array<Slot, kMaxChannels> admMute;     ///< Last ADM-OSC mute sent for each source.

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (OscControl)
};
}
