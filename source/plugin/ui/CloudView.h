// Copyright (C) 2026 Henrik Juhl. Part of SpaceForge, licensed under the GNU AGPLv3 (see LICENSE).
// SPDX-License-Identifier: AGPL-3.0-or-later

#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include <functional>

// The picture behind the macros (from SpaceForge 1), and it means something. Each control has
// one visual job, readable at a glance:
//
//   Space   the size of the cloud, with a visible edge: a small, close knot .. a cloud that
//           fills the field
//   Echo    rings pulse out at the echo's own rhythm (host tempo) while the input plays, and
//           fade like the repeats once it stops; no echo, no rings
//   Motion  how much the cloud moves: still at 0, drifting and undulating at 1, and it sways
//           left and right with the actual auto-pan
//   Tone    the colour: deep indigo (dark) .. almost white (bright)
//
// It glows with the wet signal.
class CloudView : public juce::Component, private juce::Timer
{
public:
    struct State
    {
        float space = 0.6f, echo = 0.0f, motion = 0.5f, tone = 0.5f; // the macros' positions
        float echoSeconds = 0.375f;  // time between repeats
        int repeats = 0;             // audible repeats (0 = echo off)
        float feedback = 0.0f;
        int onsets = 0;              // hits in the input so far (a rising count)
        float pan = 0.0f;            // the auto-pan right now, -1 .. 1
        float level = 0.0f;          // output peak
        bool frozen = false;
    };

    explicit CloudView (std::function<State()> source);

    void paint (juce::Graphics&) override;

    // For screenshots and tests: advance the animation by `seconds` without waiting.
    void advance (float seconds);
    // Animations off (settings): no picture at all, nothing drawn and nothing moving.
    // refreshStill() keeps the state current for when it is switched on again.
    void setAnimated (bool on);
    bool isAnimated() const { return animated; }
    void refreshStill();
    int ringsStarted() const { return ringsSoFar; } // echo rings sent out so far

private:
    void timerCallback() override;
    void step (float seconds);

    struct Blob
    {
        float x, y, radius, phase, speed;
    };
    struct Ring
    {
        float age = 0.0f, strength = 1.0f;
    };

    std::function<State()> stateSource;
    State state;
    std::vector<Blob> blobs;
    std::vector<Ring> rings;
    int ringsLeft = 0, ringIndex = 0; // repeats still to show since the last hit
    float nextRingIn = 0.0f;          // seconds until the next ring
    int ringsSoFar = 0;
    int seenOnsets = -1;
    float ringLife = 1.5f; // seconds; set per train so all its rings are out at once
    float energy = 0.0f, time = 0.0f;
    bool animated = true;
    float shownSpace = 0.6f, shownTone = 0.5f, shownMotion = 0.5f, shownPan = 0.0f, shownFrozen = 0.0f;
};
