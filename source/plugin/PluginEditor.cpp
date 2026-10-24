// Copyright (C) 2026 Henrik Juhl. Part of SpaceForge 2, licensed under the GNU AGPLv3 (see LICENSE).
// SPDX-License-Identifier: AGPL-3.0-or-later

#include "plugin/PluginEditor.h"
#include "plugin/Defaults.h"
#include "SpaceForgeVersion.h"

using namespace spaceforge;

namespace
{
constexpr float kMinScale = 0.7f, kMaxScale = 2.0f;
constexpr int kHeaderHeight = 40, kFooterHeight = 120;

struct MacroInfo
{
    const char* id;
    const char* name;
    const char* hint;
    float init;
};
const MacroInfo kMacros[] = {
    { plugin::kSpaceId, "Space", "Space: tæt og kort -> stort og uendeligt (halens længde og skyen)", dsp::MacroPositions::kInitSpace },
    { plugin::kEchoId, "Echo", "Echo: intet ekko -> mange gentagelser (1/8 punkteret, ping-pong)", dsp::MacroPositions::kInitEcho },
    { plugin::kMotionId, "Motion", "Motion: stille -> levende (svæv i halen, auto-pan i takt)", dsp::MacroPositions::kInitMotion },
    { plugin::kToneId, "Tone", "Tone: mørk -> lys (samme lydstyrke)", dsp::MacroPositions::kInitTone },
};

juce::String utf8 (const std::string& s) { return juce::String::fromUTF8 (s.c_str()); }

float valueOf (const juce::AudioProcessorValueTreeState& apvts, const char* id)
{
    return apvts.getRawParameterValue (id)->load();
}
} // namespace

SpaceForgeEditor::SpaceForgeEditor (SpaceForgeProcessor& p)
    : AudioProcessorEditor (&p),
      processor (p),
      cloud ([this] { return cloudState(); }),
      mixAttachment (p.parameters(), plugin::kMixId, mix),
      freezeAttachment (p.parameters(), plugin::kFreezeId, freeze)
{
    setLookAndFeel (&lookAndFeel);
    addAndMakeVisible (content);
    content.addAndMakeVisible (cloud);

    const juce::Colour accents[] = { theme::glow, theme::glow.withRotatedHue (-0.06f), theme::glow.withRotatedHue (0.06f),
                                     theme::ice.interpolatedWith (theme::glow, 0.5f) };
    for (int m = 0; m < kNumMacros; ++m)
    {
        const auto& info = kMacros[m];
        macros[(size_t) m] = std::make_unique<MacroKnob> (p.parameters(), info.id, info.name, utf8 (info.hint), accents[m], info.init);
        content.addAndMakeVisible (*macros[(size_t) m]);
    }

    mix.setColour (juce::Slider::rotarySliderFillColourId, theme::text.withAlpha (0.8f));
    mix.setRotaryParameters (juce::MathConstants<float>::pi * 1.25f, juce::MathConstants<float>::pi * 2.75f, true);
    mix.setDoubleClickReturnValue (true, plugin::kDefaultMix);
    mix.setTooltip (utf8 ("Mix: tør -> våd"));
    content.addAndMakeVisible (mix);

    freeze.setClickingTogglesState (true);
    freeze.setColour (juce::TextButton::buttonOnColourId, theme::ice.withAlpha (0.22f));
    freeze.setColour (juce::TextButton::textColourOnId, theme::ice);
    freeze.setTooltip (utf8 ("Freeze: holder rummet, som det lyder lige nu; ny lyd går udenom"));
    content.addAndMakeVisible (freeze);

    for (auto* label : { &mixLabel, &mixValue, &freezeValue })
    {
        label->setJustificationType (juce::Justification::centred);
        content.addAndMakeVisible (*label);
    }
    mixLabel.setText ("MIX", juce::dontSendNotification);
    mixLabel.setFont (theme::font (14.0f, true));
    mixLabel.setColour (juce::Label::textColourId, theme::text);
    mixValue.setFont (theme::font (13.0f));
    freezeValue.setFont (theme::font (13.0f));

    // The Modes: one choice, always visible; the description on hovering. Clicking sets the
    // parameter (the DAW sees it); the buttons follow the parameter in timerCallback.
    for (int m = 0; m < dsp::kNumModes; ++m)
    {
        const auto& setup = dsp::modeSetup (m);
        auto button = std::make_unique<juce::TextButton> (setup.name);
        button->setTooltip (juce::String (setup.name) + ": " + utf8 (setup.description));
        button->setClickingTogglesState (false);
        button->setColour (juce::TextButton::buttonOnColourId, theme::glow.withAlpha (0.22f));
        button->setColour (juce::TextButton::textColourOnId, theme::glow);
        button->setConnectedEdges ((m > 0 ? juce::Button::ConnectedOnLeft : 0) | (m < dsp::kNumModes - 1 ? juce::Button::ConnectedOnRight : 0));
        button->onClick = [this, m] {
            if (auto* param = processor.parameters().getParameter (plugin::kModeId))
                param->setValueNotifyingHost (param->convertTo0to1 ((float) m));
            timerCallback();
        };
        content.addAndMakeVisible (*button);
        modeButtons[(size_t) m] = std::move (button);
    }

    logo.setText ("S P A C E F O R G E   2", juce::dontSendNotification);
    logo.setFont (theme::font (15.0f, true));
    logo.setColour (juce::Label::textColourId, theme::glow);
    logo.setTooltip (juce::String ("SpaceForge 2 ") + SPACEFORGE_VERSION_STRING + " - fri software under GNU AGPLv3");
    content.addAndMakeVisible (logo);

    timerCallback();
    startTimerHz (15);

    setResizable (true, true);
    setResizeLimits (juce::roundToInt (kWidth * kMinScale), juce::roundToInt (kHeight * kMinScale),
                     juce::roundToInt (kWidth * kMaxScale), juce::roundToInt (kHeight * kMaxScale));
    getConstrainer()->setFixedAspectRatio ((double) kWidth / (double) kHeight);
    setSize (kWidth, kHeight);
}

SpaceForgeEditor::~SpaceForgeEditor()
{
    setLookAndFeel (nullptr);
}

void SpaceForgeEditor::timerCallback()
{
    const auto& apvts = processor.parameters();
    const float space = valueOf (apvts, plugin::kSpaceId), echo = valueOf (apvts, plugin::kEchoId);
    const float motion = valueOf (apvts, plugin::kMotionId), tone = valueOf (apvts, plugin::kToneId);
    const int mode = shownMode();
    for (int m = 0; m < dsp::kNumModes; ++m)
        modeButtons[(size_t) m]->setToggleState (m == mode, juce::dontSendNotification);
    macros[0]->setValueText (utf8 (dsp::describeSpace (space, mode)));
    macros[1]->setValueText (utf8 (dsp::describeEcho (echo)));

    // Motion: the scale, and the auto-pan's cycle under it
    const auto s = dsp::mapMacros ({ space, echo, motion, tone, mode });
    juce::String pan = "ingen pan";
    if (s.motionPanDepth > 0.0)
        pan = s.motionPanBars == 0 ? utf8 ("pan ½ takt") : "pan " + juce::String (s.motionPanBars) + (s.motionPanBars == 1 ? " takt" : " takter");
    macros[2]->setScale ("stille", "levende", motion, pan);
    macros[3]->setScale (utf8 ("mørk"), "lys", tone);

    mixValue.setText (juce::String (juce::roundToInt (valueOf (apvts, plugin::kMixId) * 100.0f)) + " %", juce::dontSendNotification);
    const bool frozen = valueOf (apvts, plugin::kFreezeId) >= 0.5f;
    freezeValue.setText (frozen ? "holder rummet" : "fra", juce::dontSendNotification);
    freezeValue.setColour (juce::Label::textColourId, frozen ? theme::ice : theme::dimText);
}

int SpaceForgeEditor::shownMode() const
{
    const float v = valueOf (processor.parameters(), plugin::kModeId);
    return std::isfinite (v) ? std::clamp ((int) std::lround (v), 0, dsp::kNumModes - 1) : 0;
}

CloudView::State SpaceForgeEditor::cloudState() const
{
    const auto& apvts = processor.parameters();
    CloudView::State st;
    st.space = valueOf (apvts, plugin::kSpaceId);
    st.echo = valueOf (apvts, plugin::kEchoId);
    st.motion = valueOf (apvts, plugin::kMotionId);
    st.tone = valueOf (apvts, plugin::kToneId);
    const auto s = dsp::mapMacros ({ st.space, st.echo, st.motion, st.tone, shownMode() });
    st.feedback = (float) s.echoFeedback;
    st.repeats = s.echoSend > 0.0 ? dsp::echoRepeats (s.echoFeedback) : 0;
    st.echoSeconds = (float) (dsp::kEchoBeats * 60.0 / processor.getBpm());
    st.onsets = processor.getOnsetCount();
    st.pan = processor.getPanPosition();
    st.level = processor.getOutputLevel();
    st.frozen = valueOf (apvts, plugin::kFreezeId) >= 0.5f;
    return st;
}

void SpaceForgeEditor::paint (juce::Graphics& g)
{
    g.fillAll (theme::background);
}

void SpaceForgeEditor::resized()
{
    // lay out at the design size, then scale to the window
    content.setBounds (0, 0, kWidth, kHeight);
    auto area = content.getLocalBounds().reduced (16);

    auto header = area.removeFromTop (kHeaderHeight);
    logo.setBounds (header.removeFromLeft (230).withTrimmedLeft (2));
    auto modes = header.withSizeKeepingCentre (header.getWidth(), 26);
    const int modeWidth = modes.getWidth() / dsp::kNumModes;
    for (auto& button : modeButtons)
        button->setBounds (modes.removeFromLeft (modeWidth));

    // footer: Mix and Freeze side by side in the middle
    auto footer = area.removeFromBottom (kFooterHeight).withSizeKeepingCentre (320, kFooterHeight);
    auto mixArea = footer.removeFromLeft (140);
    mix.setBounds (mixArea.removeFromTop (76).withSizeKeepingCentre (72, 72));
    mixLabel.setBounds (mixArea.removeFromTop (20));
    mixValue.setBounds (mixArea.removeFromTop (18));
    auto freezeArea = footer.removeFromRight (140);
    freezeArea.removeFromTop (20);
    freeze.setBounds (freezeArea.removeFromTop (36).withSizeKeepingCentre (110, 36));
    freezeArea.removeFromTop (20);
    freezeValue.setBounds (freezeArea.removeFromTop (18));

    // the cloud behind the four macros, centred on them, with room above and below for the
    // echo rings; the macros in front of it, side by side
    cloud.setBounds (area.expanded (0, 40));
    const auto row = area.reduced (6, 10);
    const int w = row.getWidth() / kNumMacros;
    auto m = row;
    const int knob = juce::roundToInt ((float) w * 0.72f); // gaps between them, so the cloud shows
    for (int k = 0; k < kNumMacros; ++k)
        macros[(size_t) k]->setBounds (m.removeFromLeft (w).withSizeKeepingCentre (w - 8, juce::jmin (row.getHeight(), knob + MacroKnob::kTextHeight)));

    content.setTransform (juce::AffineTransform::scale ((float) getWidth() / (float) kWidth));
}
