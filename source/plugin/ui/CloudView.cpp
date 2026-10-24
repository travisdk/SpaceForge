// Copyright (C) 2026 Henrik Juhl. Part of SpaceForge 2, licensed under the GNU AGPLv3 (see LICENSE).
// SPDX-License-Identifier: AGPL-3.0-or-later

#include "plugin/ui/CloudView.h"
#include "plugin/ui/Theme.h"

using namespace spaceforge;

namespace
{
constexpr float kFrameSeconds = 1.0f / 30.0f;
constexpr int kMaxRings = 24;

// Tone as colour: deep indigo when dark, the lilac glow in the middle, almost white when bright.
juce::Colour toneColour (float tone)
{
    const juce::Colour dark { 0xff2a2f9e }, mid = theme::glow, bright { 0xfff6f4ff };
    return tone < 0.5f ? dark.interpolatedWith (mid, tone * 2.0f) : mid.interpolatedWith (bright, tone * 2.0f - 1.0f);
}
} // namespace

CloudView::CloudView (std::function<State()> source)
    : stateSource (std::move (source))
{
    setInterceptsMouseClicks (false, false);
    juce::Random rng (20261022);
    for (int i = 0; i < 9; ++i)
    {
        // spread around the centre, so the cloud's size reads as one shape
        const float angle = (float) i / 9.0f * 6.2832f + rng.nextFloat() * 0.5f;
        const float dist = i == 0 ? 0.0f : 0.25f + 0.25f * rng.nextFloat();
        blobs.push_back ({ std::cos (angle) * dist, std::sin (angle) * dist, 0.45f + 0.3f * rng.nextFloat(),
                           rng.nextFloat() * 6.28f, 0.6f + 0.8f * rng.nextFloat() });
    }
    startTimerHz (30);
}

void CloudView::timerCallback()
{
    step (kFrameSeconds);
    repaint();
}

void CloudView::setAnimated (bool on)
{
    animated = on;
    if (on)
        startTimerHz (30);
    else
    {
        stopTimer();
        refreshStill();
    }
}

void CloudView::refreshStill()
{
    const auto now = stateSource();
    auto moved = [] (float a, float b) { return std::abs (a - b) > 1.0e-4f; };
    const bool changed = moved (now.space, shownSpace) || moved (now.tone, shownTone) || moved (now.motion, shownMotion) || ! rings.empty();
    state = now;
    shownSpace = now.space;
    shownTone = now.tone;
    shownMotion = now.motion;
    shownPan = 0.0f;
    energy = 0.3f; // a steady middle glow
    rings.clear();
    ringsLeft = 0;
    seenOnsets = now.onsets;
    if (changed)
        repaint();
}

void CloudView::advance (float seconds)
{
    for (float t = 0.0f; t < seconds; t += kFrameSeconds)
        step (kFrameSeconds);
}

void CloudView::step (float dt)
{
    state = stateSource();
    const float follow = 1.0f - std::exp (-dt / 0.08f); // the picture follows the knobs smoothly
    shownSpace += (state.space - shownSpace) * follow;
    shownTone += (state.tone - shownTone) * follow;
    shownMotion += (state.motion - shownMotion) * follow;
    shownFrozen += ((state.frozen ? 1.0f : 0.0f) - shownFrozen) * follow;
    shownPan += (state.pan - shownPan) * follow;

    const float target = juce::jlimit (0.0f, 1.0f, std::sqrt (juce::jmax (0.0f, state.level)) * 1.4f);
    energy += (target - energy) * (target > energy ? 0.25f : 0.04f);
    time += dt * (0.1f + 1.4f * shownMotion) * (1.0f - shownFrozen); // Motion sets the pace; Freeze stops it

    // Echo: the rings pulse at the echo's own rhythm (host tempo), one per repeat. A hit (re)arms
    // the pulse with `repeats` rings at full strength; once the input stops, the remaining rings
    // fade like the repeats do. The pulse keeps its phase while hits keep coming, so a steady beat
    // gives a steady stream of rings instead of one train every few seconds.
    if (seenOnsets < 0)
        seenOnsets = state.onsets;
    const float period = juce::jlimit (0.05f, 2.0f, state.echoSeconds);
    if (state.onsets != seenOnsets)
    {
        seenOnsets = state.onsets;
        ringLife = juce::jlimit (0.8f, 4.0f, (float) state.repeats * period + 0.6f);
        if (state.repeats > 0)
        {
            if (ringsLeft == 0)
                nextRingIn = period; // the first repeat comes one echo after the hit
            ringsLeft = state.repeats;
            ringIndex = 0;
        }
    }
    if (state.repeats == 0)
        ringsLeft = 0;
    if (ringsLeft > 0 && (nextRingIn -= dt) <= 0.0f)
    {
        if ((int) rings.size() < kMaxRings)
            rings.push_back ({ 0.0f, std::pow (juce::jmax (0.05f, state.feedback), (float) ringIndex) });
        ++ringIndex;
        ++ringsSoFar;
        --ringsLeft;
        nextRingIn += period; // follows tempo changes from the next repeat on
    }
    for (auto& r : rings)
        r.age += dt;
    rings.erase (std::remove_if (rings.begin(), rings.end(), [this] (const Ring& r) { return r.age > ringLife; }), rings.end());
}

void CloudView::paint (juce::Graphics& g)
{
    // The whole picture a quarter more transparent than at first (Henrik: it filled too much).
    constexpr float opacity = 0.75f;
    const auto bounds = getLocalBounds().toFloat();
    const float side = juce::jmin (bounds.getWidth(), bounds.getHeight());
    const auto colour = toneColour (shownTone).interpolatedWith (theme::ice, 0.7f * shownFrozen);

    // Motion: the whole cloud sways with the auto-pan, and wanders a little on its own
    const float wander = shownMotion * 0.06f * side;
    const juce::Point<float> centre (bounds.getCentreX() + shownPan * 0.18f * bounds.getWidth() + wander * std::sin (time * 0.7f),
                                     bounds.getCentreY() + wander * 0.6f * std::sin (time * 0.53f + 1.0f));

    // Space: the cloud's radius, from a small knot to most of the field
    const float cloudRadius = side * (0.1f + 0.3f * shownSpace) * (0.95f + 0.1f * energy);

    // the cloud body: overlapping soft blobs that undulate with Motion
    for (const auto& b : blobs)
    {
        const float undulate = 1.0f + shownMotion * 0.35f * std::sin (time * 2.0f * b.speed + b.phase);
        const float drift = shownMotion * 0.25f;
        const juce::Point<float> at (centre.x + (b.x + drift * std::sin (time * b.speed + b.phase)) * cloudRadius,
                                     centre.y + (b.y + drift * std::cos (time * 0.8f * b.speed + b.phase)) * cloudRadius);
        const float radius = cloudRadius * b.radius * undulate;
        const float alpha = (0.16f + 0.16f * energy) * opacity; // visible even in silence
        juce::ColourGradient gradient (colour.withAlpha (alpha), at.x, at.y, colour.withAlpha (0.0f), at.x + radius, at.y, true);
        g.setGradientFill (gradient);
        g.fillEllipse (juce::Rectangle<float> (radius * 2.0f, radius * 2.0f).withCentre (at));
    }

    // the cloud's edge, so its size can be read
    g.setColour (colour.withAlpha ((0.35f + 0.2f * energy) * opacity));
    g.drawEllipse (juce::Rectangle<float> (cloudRadius * 2.0f, cloudRadius * 2.0f).withCentre (centre), 1.5f);

    // Echo rings: from the cloud's edge outwards; later repeats thinner and fainter, but visible
    for (const auto& r : rings)
    {
        const float t = r.age / ringLife;
        const float radius = cloudRadius + t * 0.3f * side;
        const float alpha = (0.25f + 0.55f * r.strength) * (1.0f - t) * opacity;
        g.setColour (colour.withAlpha (alpha));
        g.drawEllipse (juce::Rectangle<float> (radius * 2.0f, radius * 2.0f).withCentre (centre), 1.0f + 1.5f * r.strength);
    }
}
