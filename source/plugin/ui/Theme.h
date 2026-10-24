// Copyright (C) 2026 Henrik Juhl. Part of SpaceForge 2, licensed under the GNU AGPLv3 (see LICENSE).
// SPDX-License-Identifier: AGPL-3.0-or-later

#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

// SpaceForge's look (from SpaceForge 1): a night sky. Deep blue-black, a lilac glow for what you
// play with and a cold cyan for state (Freeze). Square-ish, quiet controls; the four macros are
// the only things that ask for attention.
namespace spaceforge::theme
{
inline const juce::Colour background { 0xff090b14 };
inline const juce::Colour panel { 0xff11142a };
inline const juce::Colour panelHi { 0xff1a1e3a };
inline const juce::Colour outline { 0xff272c4d };
inline const juce::Colour text { 0xffe4e6f5 };
inline const juce::Colour dimText { 0xff8188ab };
inline const juce::Colour glow { 0xffb7a3ff };   // lilac: macros, selection
inline const juce::Colour ice { 0xff7fe6ff };    // cyan: Freeze

inline juce::Font font (float height, bool bold = false)
{
    return juce::Font (juce::FontOptions (height, bold ? juce::Font::bold : juce::Font::plain));
}

class LookAndFeel : public juce::LookAndFeel_V4
{
public:
    LookAndFeel()
    {
        setColour (juce::ResizableWindow::backgroundColourId, background);
        setColour (juce::TextEditor::backgroundColourId, panel);
        setColour (juce::TextEditor::outlineColourId, outline);
        setColour (juce::TextEditor::focusedOutlineColourId, glow.withAlpha (0.7f));
        setColour (juce::TextEditor::textColourId, text);
        setColour (juce::TextEditor::highlightColourId, glow.withAlpha (0.3f));
        setColour (juce::CaretComponent::caretColourId, glow);
        setColour (juce::ListBox::backgroundColourId, juce::Colours::transparentBlack);
        setColour (juce::ListBox::outlineColourId, juce::Colours::transparentBlack);
        setColour (juce::ScrollBar::thumbColourId, outline);
        setColour (juce::TextButton::buttonColourId, panel);
        setColour (juce::TextButton::buttonOnColourId, panelHi);
        setColour (juce::TextButton::textColourOffId, dimText);
        setColour (juce::TextButton::textColourOnId, text);
        setColour (juce::ComboBox::outlineColourId, outline);
        setColour (juce::Label::textColourId, dimText);
        setColour (juce::PopupMenu::backgroundColourId, panel);
        setColour (juce::PopupMenu::textColourId, text);
        setColour (juce::PopupMenu::highlightedBackgroundColourId, glow.withAlpha (0.25f));
        setColour (juce::PopupMenu::highlightedTextColourId, text);
        setColour (juce::AlertWindow::backgroundColourId, panel);
        setColour (juce::AlertWindow::textColourId, text);
        setColour (juce::AlertWindow::outlineColourId, outline);
        setColour (juce::TooltipWindow::backgroundColourId, panelHi);
        setColour (juce::TooltipWindow::textColourId, text);
        setColour (juce::TooltipWindow::outlineColourId, outline);
    }

    // A ring: a dim track, the value as a glowing arc, a small bright dot at its tip.
    void drawRotarySlider (juce::Graphics& g, int x, int y, int width, int height, float position,
                           float startAngle, float endAngle, juce::Slider& slider) override
    {
        const auto bounds = juce::Rectangle<float> ((float) x, (float) y, (float) width, (float) height).reduced (6.0f);
        const float radius = juce::jmin (bounds.getWidth(), bounds.getHeight()) * 0.5f;
        const auto centre = bounds.getCentre();
        const float angle = startAngle + position * (endAngle - startAngle);
        const float thickness = juce::jmax (2.5f, radius * 0.09f);
        const float r = radius - thickness;
        const auto accent = slider.findColour (juce::Slider::rotarySliderFillColourId);

        juce::Path track, value;
        track.addCentredArc (centre.x, centre.y, r, r, 0.0f, startAngle, endAngle, true);
        value.addCentredArc (centre.x, centre.y, r, r, 0.0f, startAngle, angle, true);
        g.setColour (outline);
        g.strokePath (track, juce::PathStrokeType (thickness, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

        // the glow: the same arc, wider and faint, under the sharp one
        for (int k = 3; k >= 1; --k)
        {
            g.setColour (accent.withAlpha (0.06f * (float) (4 - k)));
            g.strokePath (value, juce::PathStrokeType (thickness * (1.0f + 1.4f * (float) k), juce::PathStrokeType::curved,
                                                       juce::PathStrokeType::rounded));
        }
        g.setColour (accent);
        g.strokePath (value, juce::PathStrokeType (thickness, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

        // body
        const float body = r - thickness * 1.8f;
        juce::ColourGradient fill (panelHi, centre.x, centre.y - body, background, centre.x, centre.y + body, false);
        g.setGradientFill (fill);
        g.fillEllipse (juce::Rectangle<float> (body * 2.0f, body * 2.0f).withCentre (centre));
        g.setColour (outline);
        g.drawEllipse (juce::Rectangle<float> (body * 2.0f, body * 2.0f).withCentre (centre), 1.0f);

        const auto tip = centre.getPointOnCircumference (body * 0.72f, angle);
        g.setColour (text);
        g.fillEllipse (juce::Rectangle<float> (thickness * 1.2f, thickness * 1.2f).withCentre (tip));
    }

    void drawButtonBackground (juce::Graphics& g, juce::Button& button, const juce::Colour& backgroundColour,
                               bool highlighted, bool down) override
    {
        const auto bounds = button.getLocalBounds().toFloat().reduced (0.5f);
        auto base = backgroundColour;
        if (down || highlighted)
            base = base.brighter (down ? 0.25f : 0.1f);
        g.setColour (base);
        g.fillRoundedRectangle (bounds, 4.0f);
        g.setColour (button.getToggleState() ? button.findColour (juce::TextButton::textColourOnId).withAlpha (0.6f) : outline);
        g.drawRoundedRectangle (bounds, 4.0f, 1.0f);
    }

    juce::Font getTextButtonFont (juce::TextButton&, int buttonHeight) override
    {
        return font (juce::jmin (14.0f, (float) buttonHeight * 0.5f));
    }
};
} // namespace spaceforge::theme
