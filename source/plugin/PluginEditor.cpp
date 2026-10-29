// Copyright (C) 2026 Henrik Juhl. Part of SpaceForge, licensed under the GNU AGPLv3 (see LICENSE).
// SPDX-License-Identifier: AGPL-3.0-or-later

#include "plugin/PluginEditor.h"
#include "plugin/Defaults.h"
#include "SpaceForgeVersion.h"
#include "presets/PresetFiles.h"

using namespace spaceforge;

namespace
{
constexpr float kMinScale = 0.7f, kMaxScale = 2.0f;
constexpr int kBrowserWidth = 380, kHeaderHeight = 72, kModesHeight = 34, kMixSize = 110;

struct MacroInfo
{
    const char* id;
    const char* name;
    const char* hint;
    float init;
};
const MacroInfo kMacros[] = {
    { plugin::kSpaceId, "Space", "Space: tight and short -> huge and endless (the tail's length and the cloud)", dsp::MacroPositions::kInitSpace },
    { plugin::kReflectId, "Reflect", "Reflect: repeats that melt into the room (dotted 1/8, ping-pong)", dsp::MacroPositions::kInitReflect },
    { plugin::kMotionId, "Motion", "Motion: still -> alive (drift in the tail, tempo-synced auto-pan)", dsp::MacroPositions::kInitMotion },
    { plugin::kToneId, "Tone", "Tone: dark -> bright (same loudness)", dsp::MacroPositions::kInitTone },
};

const MacroInfo kSmalls[] = {
    { plugin::kEchoId, "Echo", "Echo: distinct repeats in time with the tempo (click the text to change the note)", dsp::MacroPositions::kInitDelay },
    { plugin::kDuckId, "Duck", "Duck: the reverb pulls back while you play, and returns in the pauses", dsp::MacroPositions::kInitDuck },
};

juce::String utf8 (const std::string& s) { return juce::String::fromUTF8 (s.c_str()); }

float valueOf (const juce::AudioProcessorValueTreeState& apvts, const char* id)
{
    return apvts.getRawParameterValue (id)->load();
}
} // namespace

// --- ShapeButton (from SpaceForge 1) ----------------------------------------------------------

SpaceForgeEditor::ShapeButton::ShapeButton (Shape s, const juce::String& tooltip) : juce::Button (tooltip), shape (s)
{
    setTooltip (tooltip);
}

void SpaceForgeEditor::ShapeButton::paintButton (juce::Graphics& g, bool highlighted, bool down)
{
    const auto bounds = getLocalBounds().toFloat();
    const float side = juce::jmin (bounds.getWidth(), bounds.getHeight());
    const auto area = juce::Rectangle<float> (side, side).withCentre (bounds.getCentre()).reduced (side * 0.2f);
    auto colour = down ? theme::glow : highlighted ? theme::text : theme::dimText;
    if (! isEnabled())
        colour = theme::dimText.withAlpha (0.3f); // visible, but clearly not available

    juce::Path p;
    switch (shape)
    {
        case Shape::Left:
        case Shape::Right:
        {
            const bool left = shape == Shape::Left;
            const float x0 = left ? area.getRight() - area.getWidth() * 0.25f : area.getX() + area.getWidth() * 0.25f;
            const float x1 = left ? area.getX() + area.getWidth() * 0.25f : area.getRight() - area.getWidth() * 0.25f;
            p.startNewSubPath (x0, area.getY());
            p.lineTo (x1, area.getCentreY());
            p.lineTo (x0, area.getBottom());
            g.setColour (colour);
            g.strokePath (p, juce::PathStrokeType (2.2f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
            return;
        }
        case Shape::Heart:
        {
            const float s = area.getWidth() * 0.62f;
            const auto c = area.getCentre().translated (0.0f, s * 0.1f);
            p.startNewSubPath (c.x, c.y + s * 0.5f);
            p.cubicTo (c.x - s * 0.95f, c.y - s * 0.1f, c.x - s * 0.5f, c.y - s * 0.85f, c.x, c.y - s * 0.3f);
            p.cubicTo (c.x + s * 0.5f, c.y - s * 0.85f, c.x + s * 0.95f, c.y - s * 0.1f, c.x, c.y + s * 0.5f);
            p.closeSubPath();
            if (getToggleState())
            {
                g.setColour (theme::ice);
                g.fillPath (p);
            }
            else
            {
                g.setColour (colour);
                g.strokePath (p, juce::PathStrokeType (1.6f));
            }
            return;
        }
        case Shape::Back:
        {
            // a counter-clockwise arc ending in an arrowhead
            const auto centre = area.getCentre();
            const float radius = area.getWidth() * 0.36f;
            p.addCentredArc (centre.x, centre.y, radius, radius, 0.0f, -1.6f, 1.9f, true);
            g.setColour (colour);
            g.strokePath (p, juce::PathStrokeType (1.6f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
            const auto tip = centre.getPointOnCircumference (radius, -1.6f);
            const float span = area.getWidth();
            juce::Path head;
            head.addTriangle (tip.translated (-span * 0.2f, 0.0f), tip.translated (span * 0.08f, -span * 0.18f), tip.translated (span * 0.08f, span * 0.18f));
            g.fillPath (head);
            return;
        }
        case Shape::Lock:
        {
            // a padlock: closed (toggle on) in the quiet colour, open in the accent
            const bool locked = getToggleState();
            if (! locked && isEnabled())
                colour = down ? theme::text : theme::glow;
            const auto body = juce::Rectangle<float> (area.getWidth() * 0.8f, area.getHeight() * 0.5f)
                                  .withCentre ({ area.getCentreX(), area.getY() + area.getHeight() * 0.72f });
            g.setColour (colour);
            g.fillRoundedRectangle (body, 2.0f);
            const float r = body.getWidth() * 0.3f;
            const float x = body.getCentreX() + (locked ? 0.0f : body.getWidth() * 0.32f);
            p.startNewSubPath (x - r, body.getY());
            p.lineTo (x - r, body.getY() - r * 0.6f);
            p.addCentredArc (x, body.getY() - r * 0.6f, r, r, 0.0f, -juce::MathConstants<float>::halfPi, juce::MathConstants<float>::halfPi);
            p.lineTo (x + r, locked ? body.getY() : body.getY() - r * 0.6f);
            g.strokePath (p, juce::PathStrokeType (1.8f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
            return;
        }
        case Shape::Gear:
        {
            // a cog: eight teeth around a ring
            const auto centre = area.getCentre();
            const float outer = area.getWidth() * 0.5f, inner = outer * 0.62f;
            g.setColour (colour);
            for (int i = 0; i < 8; ++i)
            {
                juce::Path tooth;
                tooth.addRoundedRectangle (-outer * 0.16f, -outer, outer * 0.32f, outer * 0.4f, 1.0f);
                g.fillPath (tooth, juce::AffineTransform::rotation ((float) i * juce::MathConstants<float>::pi / 4.0f).translated (centre));
            }
            g.drawEllipse (juce::Rectangle<float> (inner * 2.0f, inner * 2.0f).withCentre (centre), outer * 0.28f);
            return;
        }
        case Shape::Sidebar:
        {
            // a window with a panel on the left; the panel filled while the presets show
            g.setColour (colour);
            g.drawRoundedRectangle (area, 2.0f, 1.6f);
            const auto panel = area.withWidth (area.getWidth() * 0.36f);
            if (getToggleState())
                g.fillRoundedRectangle (panel, 2.0f);
            else
                g.drawLine (panel.getRight(), area.getY(), panel.getRight(), area.getBottom(), 1.6f);
            return;
        }
        case Shape::Dice:
        {
            // the five face: rounded square with five pips
            g.setColour (colour);
            g.drawRoundedRectangle (area, area.getWidth() * 0.18f, 1.6f);
            const float pip = area.getWidth() * 0.16f;
            const auto inner = area.reduced (area.getWidth() * 0.27f);
            for (auto centre : { inner.getTopLeft(), inner.getTopRight(), inner.getCentre(), inner.getBottomLeft(), inner.getBottomRight() })
                g.fillEllipse (juce::Rectangle<float> (pip, pip).withCentre (centre));
            return;
        }
    }
}

// --- editor ----------------------------------------------------------------------------------

SpaceForgeEditor::SpaceForgeEditor (SpaceForgeProcessor& p)
    : AudioProcessorEditor (&p),
      processor (p),
      browser (p.library()),
      cloud ([this] { return cloudState(); }),
      mixAttachment (p.parameters(), plugin::kMixId, mix)
{
    setLookAndFeel (&lookAndFeel);
    addAndMakeVisible (content);
    content.addAndMakeVisible (cloud);

    // the browser and the preset header (from SpaceForge 1)
    content.addAndMakeVisible (browser);
    browser.onLoad = [this] (int index) { processor.loadPreset (index); };
    browser.onFavourite = [this] (const juce::String& key, bool on) { processor.setFavourite (key, on); };
    for (auto* b : { &previous, &next, &favourite, &dice, &undo })
        content.addAndMakeVisible (b);
    previous.onClick = [this] { processor.step (-1, browser.visiblePresets()); };
    next.onClick = [this] { processor.step (1, browser.visiblePresets()); };
    favourite.onClick = [this] {
        const auto key = processor.preset().key();
        processor.setFavourite (key, ! processor.library().isFavourite (key));
    };
    dice.onClick = [this] { processor.roll (! processor.isModeLocked() || juce::ModifierKeys::currentModifiers.isShiftDown()); };
    modeLock.onClick = [this] { processor.setModeLocked (! processor.isModeLocked()); };
    content.addAndMakeVisible (modeLock);
    undo.onClick = [this] { processor.undoRoll(); };
    save.setTooltip ("Save as your own preset");
    save.onClick = [this] { showSaveDialog(); };
    content.addAndMakeVisible (save);
    gear.onClick = [this] { showSettingsMenu(); };
    content.addAndMakeVisible (gear);
    browserToggle.onClick = [this] { setBrowserOpen (! browserOpen()); };
    content.addAndMakeVisible (browserToggle);

    presetName.setFont (theme::font (22.0f, true));
    presetName.setColour (juce::Label::textColourId, theme::text);
    presetName.setJustificationType (juce::Justification::centred);
    presetInfo.setFont (theme::font (13.0f));
    presetInfo.setColour (juce::Label::textColourId, theme::dimText);
    presetInfo.setJustificationType (juce::Justification::centred);
    content.addAndMakeVisible (presetName);
    content.addAndMakeVisible (presetInfo);

    const juce::Colour accents[] = { theme::glow, theme::glow.withRotatedHue (-0.06f), theme::glow.withRotatedHue (0.06f),
                                     theme::ice.interpolatedWith (theme::glow, 0.5f) };
    for (int m = 0; m < kNumMacros; ++m)
    {
        const auto& info = kMacros[m];
        macros[(size_t) m] = std::make_unique<MacroKnob> (p.parameters(), info.id, info.name, utf8 (info.hint), accents[m], info.init);
        content.addAndMakeVisible (*macros[(size_t) m]);
    }

    for (int k = 0; k < 2; ++k)
    {
        const auto& info = kSmalls[k];
        smalls[(size_t) k] = std::make_unique<MacroKnob> (p.parameters(), info.id, info.name, utf8 (info.hint), accents[k == 0 ? 3 : 1], info.init);
        content.addAndMakeVisible (*smalls[(size_t) k]);
    }
    // Echo's note is a fixed choice with no knob of its own: click its text to step through them.
    smalls[0]->onTextClick = [this] {
        if (auto* param = processor.parameters().getParameter (plugin::kEchoNoteId))
        {
            const int following = (juce::roundToInt (valueOf (processor.parameters(), plugin::kEchoNoteId)) + 1) % dsp::kNumDelayNotes;
            param->beginChangeGesture();
            param->setValueNotifyingHost (param->convertTo0to1 ((float) following));
            param->endChangeGesture();
            timerCallback();
        }
    };

    mix.setRotaryParameters (juce::MathConstants<float>::pi * 1.25f, juce::MathConstants<float>::pi * 2.75f, true);
    mix.setDoubleClickReturnValue (true, plugin::kDefaultMix);
    mix.setTooltip ("Mix: dry -> wet");
    content.addAndMakeVisible (mix);
    for (auto* label : { &mixLabel, &mixValue })
    {
        label->setJustificationType (juce::Justification::centred);
        content.addAndMakeVisible (*label);
    }
    mixLabel.setText ("MIX", juce::dontSendNotification);
    mixLabel.setFont (theme::font (14.0f, true));
    mixLabel.setColour (juce::Label::textColourId, theme::text);
    mixValue.setFont (theme::font (13.0f));

    // The Modes: one choice, always visible; the description on hovering. Clicking sets the
    // parameter (the DAW sees it); the buttons follow the parameter in timerCallback.
    for (int position = 0; position < dsp::kNumModes; ++position)
    {
        const int m = dsp::kModeDisplayOrder[position];
        const auto& setup = dsp::modeSetup (m);
        auto button = std::make_unique<juce::TextButton> (setup.name);
        button->setTooltip (juce::String (setup.name) + ": " + utf8 (setup.description));
        button->setClickingTogglesState (false);
        button->setColour (juce::TextButton::buttonOnColourId, theme::glow.withAlpha (0.22f));
        button->setColour (juce::TextButton::textColourOnId, theme::glow);
        button->setConnectedEdges ((position > 0 ? juce::Button::ConnectedOnLeft : 0) | (position < dsp::kNumModes - 1 ? juce::Button::ConnectedOnRight : 0));
        button->onClick = [this, m] {
            if (auto* param = processor.parameters().getParameter (plugin::kModeId))
                param->setValueNotifyingHost (param->convertTo0to1 ((float) m));
            timerCallback();
        };
        content.addAndMakeVisible (*button);
        modeButtons[(size_t) m] = std::move (button);
    }

    logo.setText ("S P A C E F O R G E", juce::dontSendNotification);
    logo.setFont (theme::font (15.0f, true));
    logo.setColour (juce::Label::textColourId, theme::glow);
    logo.setTooltip (juce::String ("SpaceForge ") + SPACEFORGE_VERSION_STRING + " - free software under the GNU AGPLv3");
    content.addAndMakeVisible (logo);

    versionLabel.setText (juce::String ("version ") + SPACEFORGE_VERSION_STRING, juce::dontSendNotification);
    versionLabel.setFont (theme::font (11.0f, false));
    versionLabel.setColour (juce::Label::textColourId, theme::glow.withAlpha (0.55f));
    versionLabel.setInterceptsMouseClicks (false, false);
    content.addAndMakeVisible (versionLabel);

    for (auto* label : { &sortTitle, &sortHelp })
    {
        label->setJustificationType (juce::Justification::centred);
        content.addChildComponent (*label);
    }
    sortTitle.setFont (theme::font (17.0f, true));
    sortTitle.setColour (juce::Label::textColourId, theme::glow);
    sortHelp.setFont (theme::font (13.0f));
    sortHelp.setColour (juce::Label::textColourId, theme::dimText);

    if (juce::JUCEApplicationBase::isStandaloneApp())
        setWantsKeyboardFocus (true); // the arrow keys step through presets

    // the user's settings (per user, not per project)
    auto& settings = processor.settings();
    const auto accent = juce::Colour::fromString (settings.getValue ("accent", theme::accentChoices().front().colour.toString()));
    setAccent (std::any_of (theme::accentChoices().begin(), theme::accentChoices().end(), [&accent] (const auto& c) { return c.colour == accent; })
                   ? accent
                   : theme::accentChoices().front().colour);
    setTooltips (settings.getBoolValue ("tooltips", true));
    setAnimations (settings.getBoolValue ("animations", true));
    browser.setVisible (settings.getBoolValue ("browser", true));
    logo.setVisible (browserOpen());
    browserToggle.setToggleState (browserOpen(), juce::dontSendNotification);
    scale = (float) settings.getDoubleValue ("scale", 1.0);
    if (std::none_of (std::begin (kScales), std::end (kScales), [this] (float s) { return std::abs (s - scale) < 0.01f; }))
        scale = 1.0f;

    seenKnobJumps = processor.knobJumpCount();
    processor.addChangeListener (this);
    updateFromPreset();
    timerCallback();
    startTimerHz (15);

    setResizable (true, true);
    applySizeLimits();
    if (const auto folder = sortFolderFromCommandLine(); folder != juce::File())
        startSorting (folder);
}

SpaceForgeEditor::~SpaceForgeEditor()
{
    processor.removeChangeListener (this);
    setLookAndFeel (nullptr);
}

void SpaceForgeEditor::changeListenerCallback (juce::ChangeBroadcaster*)
{
    // the knobs jumped (preset, roll, undo): the sound has switched already; glide them on screen
    if (processor.knobJumpCount() != seenKnobJumps)
    {
        seenKnobJumps = processor.knobJumpCount();
        for (int m = 0; m < kNumMacros && animationsOn(); ++m)
            macros[(size_t) m]->animate (processor.knobJumpFrom()[(size_t) m], valueOf (processor.parameters(), kMacros[m].id));
    }
    updateFromPreset();
}

void SpaceForgeEditor::updateFromPreset()
{
    const auto& preset = processor.preset();
    juce::String info = PresetLibrary::categoryOf (preset);
    if (info == "Init" || info == "User")
        info << "  -  " << dsp::modeSetup (preset.mode).name;
    presetInfo.setText (info, juce::dontSendNotification);
    favourite.setToggleState (processor.library().isFavourite (preset.key()), juce::dontSendNotification);
    undo.setEnabled (processor.canUndoRoll());
    modeLock.setToggleState (processor.isModeLocked(), juce::dontSendNotification);
    modeLock.setTooltip (processor.isModeLocked() ? "Mode locked: the dice keeps the Mode (click to let it pick a Mode too)"
                                                  : "Mode open: the dice picks a Mode too (click to lock it)");
    browser.refresh (processor.presetIndex());
    timerCallback();
}

void SpaceForgeEditor::timerCallback()
{
    const auto& apvts = processor.parameters();
    const float space = valueOf (apvts, plugin::kSpaceId), echo = valueOf (apvts, plugin::kReflectId);
    const float motion = valueOf (apvts, plugin::kMotionId), tone = valueOf (apvts, plugin::kToneId);
    const int mode = shownMode();
    for (int m = 0; m < dsp::kNumModes; ++m)
        modeButtons[(size_t) m]->setToggleState (m == mode, juce::dontSendNotification);
    macros[0]->setValueText (utf8 (dsp::describeSpace (space, mode)));
    macros[1]->setValueText (utf8 (dsp::describeReflect (echo)));
    smalls[0]->setValueText (utf8 (dsp::describeDelay (valueOf (apvts, plugin::kEchoId), juce::roundToInt (valueOf (apvts, plugin::kEchoNoteId)))));
    smalls[1]->setValueText (utf8 (dsp::describeDuck (valueOf (apvts, plugin::kDuckId))));

    // Motion: the scale, and the auto-pan's cycle under it
    const auto s = dsp::mapMacros ({ space, echo, motion, tone, mode });
    juce::String pan = "no pan";
    if (s.motionPanDepth > 0.0)
        pan = s.motionPanBars == 0 ? utf8 ("pan ½ bar") : "pan " + juce::String (s.motionPanBars) + (s.motionPanBars == 1 ? " bar" : " bars");
    macros[2]->setScale ("still", "alive", motion, pan);
    macros[3]->setScale ("dark", "bright", tone);

    const auto shownName = processor.presetName() + (processor.presetUnchanged() ? "" : " *");
    if (presetName.getText() != shownName)
        presetName.setText (shownName, juce::dontSendNotification);

    mixValue.setText (juce::String (juce::roundToInt (valueOf (apvts, plugin::kMixId) * 100.0f)) + " %", juce::dontSendNotification);
    if (! cloud.isAnimated())
        cloud.refreshStill();
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
    st.echo = valueOf (apvts, plugin::kReflectId);
    st.motion = valueOf (apvts, plugin::kMotionId);
    st.tone = valueOf (apvts, plugin::kToneId);
    const auto s = dsp::mapMacros ({ st.space, st.echo, st.motion, st.tone, shownMode() });
    st.feedback = (float) s.reflectFeedback;
    st.repeats = s.reflectSend > 0.0 ? dsp::reflectRepeats (s.reflectFeedback) : 0;
    st.echoSeconds = (float) (dsp::kReflectBeats * 60.0 / processor.getBpm());
    st.onsets = processor.getOnsetCount();
    st.pan = processor.getPanPosition();
    st.level = processor.getOutputLevel();
    return st;
}

void SpaceForgeEditor::askForName (const juce::String& title, const juce::String& message, const juce::String& initial,
                                   const juce::String& button, std::function<void (const juce::String&)> onName)
{
    saveDialog = std::make_unique<juce::AlertWindow> (title, message, juce::MessageBoxIconType::NoIcon, this);
    saveDialog->setLookAndFeel (&lookAndFeel);
    saveDialog->addTextEditor ("name", initial, "Name");
    saveDialog->addButton (button, 1, juce::KeyPress (juce::KeyPress::returnKey));
    saveDialog->addButton ("Cancel", 0, juce::KeyPress (juce::KeyPress::escapeKey));
    saveDialog->enterModalState (true, juce::ModalCallbackFunction::create ([safe = juce::Component::SafePointer (this), onName] (int result) {
        if (safe == nullptr || safe->saveDialog == nullptr)
            return;
        const auto name = presets::sanitiseName (safe->saveDialog->getTextEditorContents ("name"));
        safe->saveDialog.reset();
        if (result == 1 && name.isNotEmpty())
            onName (name);
    }));
}

void SpaceForgeEditor::showSaveDialog()
{
    const auto& preset = processor.preset();
    askForName ("Save preset", "Saved in " + PresetLibrary::userFolder().getFullPathName(),
                preset.isFactory ? preset.name + " (mine)" : preset.name, "Save", [this] (const juce::String& name) {
                    if (! processor.saveUserPreset (name))
                        juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon, "Save preset",
                                                                "The preset could not be saved in " + PresetLibrary::userFolder().getFullPathName());
                });
}

// --- settings -------------------------------------------------------------------

void SpaceForgeEditor::showSettingsMenu()
{
    juce::PopupMenu menu;
    menu.setLookAndFeel (&lookAndFeel);
    menu.addSectionHeader ("Accent colour");
    for (const auto& choice : theme::accentChoices())
    {
        juce::PopupMenu::Item item (choice.name);
        item.colour = choice.colour;
        item.isTicked = choice.colour == theme::glow;
        item.action = [this, colour = choice.colour] { setAccent (colour); };
        menu.addItem (item);
    }
    menu.addSectionHeader ("Interface size");
    for (const float s : kScales)
        menu.addItem (juce::String (juce::roundToInt (s * 100.0f)) + " %", true, std::abs (scale - s) < 0.01f, [this, s] { setUiScale (s); });
    menu.addSeparator();
    menu.addItem ("Animations", true, animationsOn(), [this] { setAnimations (! animationsOn()); });
    menu.addItem ("Show tooltips", true, tooltipsOn(), [this] { setTooltips (! tooltipsOn()); });

    const auto& preset = processor.preset();
    const bool user = ! preset.isFactory && preset.file.existsAsFile();
    menu.addSectionHeader ("Presets");
    menu.addItem ("Import presets...", [this] { importPresets(); });
    menu.addItem ("Export \"" + preset.name + "\"...", [this] { exportPreset(); });
    menu.addItem ("Rename \"" + (user ? preset.name : juce::String ("user preset")) + "\"...", user, false, [this] { renamePreset(); });
    menu.addItem ("Delete \"" + (user ? preset.name : juce::String ("user preset")) + "\"", user, false, [this] { deletePreset(); });
    menu.addItem ("Open preset folder", [] {
        const auto folder = PresetLibrary::userFolder();
        folder.createDirectory();
        files::openFolder (folder);
    });
    menu.addItem ("Rescan presets", [this] { processor.presetsChanged(); });
    menu.addSeparator();
    menu.addItem ("About SpaceForge...", [this] { showAbout(); });
    menu.addItem ("Version " SPACEFORGE_VERSION_STRING, false, false, {});
    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&gear).withMinimumWidth (200).withDeletionCheck (*this));
}

void SpaceForgeEditor::setAccent (juce::Colour colour)
{
    theme::glow = colour;
    lookAndFeel.refreshAccent();
    const juce::Colour accents[] = { theme::glow, theme::glow.withRotatedHue (-0.06f), theme::glow.withRotatedHue (0.06f),
                                     theme::ice.interpolatedWith (theme::glow, 0.5f) };
    for (int m = 0; m < kNumMacros; ++m)
        macros[(size_t) m]->slider.setColour (juce::Slider::rotarySliderFillColourId, accents[m]);
    mix.setColour (juce::Slider::rotarySliderFillColourId, theme::glow.interpolatedWith (theme::text, 0.25f)); // a little quieter than the macros
    for (auto& button : modeButtons)
    {
        button->setColour (juce::TextButton::buttonOnColourId, theme::glow.withAlpha (0.22f));
        button->setColour (juce::TextButton::textColourOnId, theme::glow);
    }
    logo.setColour (juce::Label::textColourId, theme::glow);
    versionLabel.setColour (juce::Label::textColourId, theme::glow.withAlpha (0.55f));
    sortTitle.setColour (juce::Label::textColourId, theme::glow);
    processor.settings().setValue ("accent", colour.toString());
    repaint();
    content.repaint();
}

void SpaceForgeEditor::setUiScale (float s)
{
    scale = juce::jlimit (kScales[0], kScales[std::size (kScales) - 1], std::isfinite (s) ? s : 1.0f);
    processor.settings().setValue ("scale", (double) scale);
    applySizeLimits();
}

void SpaceForgeEditor::setAnimations (bool on)
{
    cloud.setAnimated (on);
    processor.settings().setValue ("animations", on);
}

void SpaceForgeEditor::setBrowserOpen (bool open)
{
    browser.setVisible (open);
    logo.setVisible (open);
    versionLabel.setVisible (open);
    browserToggle.setToggleState (open, juce::dontSendNotification);
    processor.settings().setValue ("browser", open);
    applySizeLimits();
    resized();
}

void SpaceForgeEditor::setTooltips (bool on)
{
    if (on && tooltips == nullptr)
        tooltips = std::make_unique<juce::TooltipWindow> (this, 600);
    else if (! on)
        tooltips.reset();
    processor.settings().setValue ("tooltips", on);
}

void SpaceForgeEditor::importPresets()
{
    chooser = std::make_unique<juce::FileChooser> ("Import presets", juce::File::getSpecialLocation (juce::File::userDocumentsDirectory), "*.json");
    chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles | juce::FileBrowserComponent::canSelectMultipleItems,
                          [safe = juce::Component::SafePointer (this)] (const juce::FileChooser& fc) {
                              if (safe == nullptr || fc.getResults().isEmpty())
                                  return;
                              juce::StringArray refused;
                              const int imported = safe->processor.library().importFiles (fc.getResults(), refused);
                              safe->processor.presetsChanged();
                              juce::String text = juce::String (imported) + (imported == 1 ? " preset imported." : " presets imported.");
                              if (! refused.isEmpty())
                                  text << "\n\nNot presets (skipped): " << refused.joinIntoString (", ");
                              juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::InfoIcon, "Import presets", text);
                          });
}

void SpaceForgeEditor::exportPreset()
{
    // The controls as they are now, under the preset's name.
    auto p = processor.preset();
    const auto& apvts = processor.parameters();
    p.mode = shownMode();
    p.space = valueOf (apvts, plugin::kSpaceId);
    p.reflect = valueOf (apvts, plugin::kReflectId);
    p.motion = valueOf (apvts, plugin::kMotionId);
    p.tone = valueOf (apvts, plugin::kToneId);
    p.mix = valueOf (apvts, plugin::kMixId);
    p.delay = valueOf (apvts, plugin::kEchoId);
    p.delayNote = juce::jlimit (0, dsp::kNumDelayNotes - 1, juce::roundToInt (valueOf (apvts, plugin::kEchoNoteId)));
    p.duck = valueOf (apvts, plugin::kDuckId);
    const auto start = juce::File::getSpecialLocation (juce::File::userDocumentsDirectory).getChildFile (juce::File::createLegalFileName (p.name) + ".json");
    chooser = std::make_unique<juce::FileChooser> ("Export preset", start, "*.json");
    chooser->launchAsync (juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::canSelectFiles | juce::FileBrowserComponent::warnAboutOverwriting,
                          [p] (const juce::FileChooser& fc) {
                              const auto file = fc.getResult();
                              if (file != juce::File() && ! PresetLibrary::exportPreset (p, file.withFileExtension ("json")))
                                  juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon, "Export preset",
                                                                          "Could not write " + file.getFullPathName());
                          });
}

void SpaceForgeEditor::renamePreset()
{
    const auto& preset = processor.preset();
    if (preset.isFactory)
        return;
    askForName ("Rename preset", {}, preset.name, "Rename", [this] (const juce::String& name) {
        const int index = processor.library().renameUserPreset (processor.presetIndex(), name);
        if (index >= 0)
            processor.loadPreset (index);
        processor.presetsChanged();
    });
}

void SpaceForgeEditor::deletePreset()
{
    const auto& preset = processor.preset();
    if (preset.isFactory)
        return;
    juce::AlertWindow::showOkCancelBox (juce::MessageBoxIconType::QuestionIcon, "Delete preset?",
                                        "\"" + preset.name + "\" will be moved to the trash.", "Delete", "Cancel", this,
                                        juce::ModalCallbackFunction::create ([safe = juce::Component::SafePointer (this)] (int result) {
                                            if (safe == nullptr || result != 1)
                                                return;
                                            safe->processor.library().deleteUserPreset (safe->processor.presetIndex());
                                            safe->processor.presetsChanged();
                                        }));
}

juce::String SpaceForgeEditor::aboutText()
{
    return juce::String::fromUTF8 ("SpaceForge " SPACEFORGE_VERSION_STRING "\n"
                                   "© 2026 Henrik Juhl. Free software under the GNU Affero General Public License v3 or later.\n\n"
                                   "The rooms use the Cloud Seed algorithm by Ghost Note Audio (CloudSeedCore, MIT), "
                                   "originally by Valdemar Erlingsson. SpaceForge is not a Ghost Note Audio product.\n"
                                   "The granular texture uses the Clouds DSP code and stmlib by Emilie Gillet (MIT).\n"
                                   "Built with JUCE (AGPLv3) and clap-juce-extensions (MIT).\n\n"
                                   "Full notices: THIRD_PARTY.md in the source.");
}

void SpaceForgeEditor::showAbout()
{
    juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::NoIcon, "About SpaceForge", aboutText(), "Close", this);
}

bool SpaceForgeEditor::keyPressed (const juce::KeyPress& key)
{
    if (sorter != nullptr)
        return sortKey (key);
    if (key == juce::KeyPress ('s', juce::ModifierKeys::commandModifier, 0))
    {
        showSaveDialog();
        return true;
    }
    if (key == juce::KeyPress ('z', juce::ModifierKeys::commandModifier, 0))
    {
        processor.undoRoll();
        return true;
    }
    if (key == juce::KeyPress::leftKey || key == juce::KeyPress::upKey)
    {
        processor.step (-1, browser.visiblePresets());
        return true;
    }
    if (key == juce::KeyPress::rightKey || key == juce::KeyPress::downKey)
    {
        processor.step (1, browser.visiblePresets());
        return true;
    }
    return false;
}

// --- the sorting mode --------------------------------------------------------------------------

void SpaceForgeEditor::applySizeLimits()
{
    setResizeLimits (juce::roundToInt (designWidth() * kMinScale), juce::roundToInt (designHeight() * kMinScale),
                     juce::roundToInt (designWidth() * kMaxScale), juce::roundToInt (designHeight() * kMaxScale));
    getConstrainer()->setFixedAspectRatio ((double) designWidth() / (double) designHeight());
    setSize (juce::roundToInt (designWidth() * scale), juce::roundToInt (designHeight() * scale));
}

juce::File SpaceForgeEditor::sortFolderFromCommandLine()
{
    if (! juce::JUCEApplicationBase::isStandaloneApp())
        return {};
    const auto args = juce::JUCEApplicationBase::getCommandLineParameterArray();
    const int at = args.indexOf ("--sort");
    if (at < 0)
        return {};
    if (at + 1 < args.size() && ! args[at + 1].startsWith ("-"))
        return juce::File::getCurrentWorkingDirectory().getChildFile (args[at + 1]);
    return juce::File (SPACEFORGE_CANDIDATES_DIR);
}

void SpaceForgeEditor::startSorting (const juce::File& folder)
{
    sorter = std::make_unique<CandidateSorter> (folder);
    processor.startAudition();
    sortTitle.setVisible (true);
    sortHelp.setVisible (true);
    setWantsKeyboardFocus (true);
    applySizeLimits();
    showCandidate();
    grabKeyboardFocus();
}

void SpaceForgeEditor::showCandidate()
{
    if (sorter == nullptr)
        return;
    const int kept = sorter->count (CandidateSorter::Decision::keep);
    const auto sound = processor.auditionSoundName (processor.auditionSoundIndex());
    if (sorter->size() == 0)
    {
        sortTitle.setText ("No candidates in " + sorter->reviewFile().getParentDirectory().getFullPathName(), juce::dontSendNotification);
        sortHelp.setText ({}, juce::dontSendNotification);
        return;
    }
    if (const auto* candidate = sorter->current())
    {
        processor.applyPreset (*candidate);
        sortTitle.setText ("Candidate " + juce::String (sorter->position() + 1) + " of " + juce::String (sorter->size()) + "  ·  "
                               + dsp::modeSetup (candidate->mode).name + "  ·  kept " + juce::String (kept),
                           juce::dontSendNotification);
        sortHelp.setText (utf8 ("K keep   ·   S skip   ·   ← undo   ·   1-5 test sound (now: ") + sound + ")   ·   R restart",
                          juce::dontSendNotification);
    }
    else
    {
        sortTitle.setText ("Done: " + juce::String (kept) + " kept of " + juce::String (sorter->size()), juce::dontSendNotification);
        sortHelp.setText (utf8 ("← undo the last one   ·   the choices are saved in ") + sorter->reviewFile().getFullPathName(), juce::dontSendNotification);
    }
}

bool SpaceForgeEditor::sortKey (const juce::KeyPress& key)
{
    const auto c = juce::CharacterFunctions::toUpperCase (key.getTextCharacter());
    if (c == 'K' || c == 'S')
        sorter->decide (c == 'K' ? CandidateSorter::Decision::keep : CandidateSorter::Decision::skip);
    else if (key.isKeyCode (juce::KeyPress::leftKey))
        sorter->undo();
    else if (c >= '1' && c < '1' + SpaceForgeProcessor::kNumAuditionSounds)
        processor.setAuditionSound ((int) (c - '1'));
    else if (c == 'R')
    {
        processor.restartAudition();
        return true;
    }
    else
        return false;
    showCandidate();
    return true;
}

// --- layout ----------------------------------------------------------------------------------

void SpaceForgeEditor::paint (juce::Graphics& g)
{
    g.fillAll (theme::background);
}

void SpaceForgeEditor::parentHierarchyChanged()
{
   #if JUCE_WINDOWS
    // Same fix as SubForge. The CLAP wrapper (the only one that leaves wrapperType undefined) scales
    // the editor by the host's set_scale and reports sizes that include it, but JUCE 8 also scales a
    // window embedded in the host by the host window's DPI. At 125 % the view came out 1.25 times too
    // big for the host's window and was cropped. Keep the window at 1:1 as the VST3 wrapper does, and
    // push the size to the native window, which addToDesktop created at the DPI scale.
    if (processor.wrapperType == juce::AudioProcessor::wrapperType_Undefined)
        if (auto* peer = getPeer(); peer != nullptr && ! peer->getCustomPlatformScaleFactor().has_value())
        {
            peer->setCustomPlatformScaleFactor (1.0);
            peer->setBounds (peer->getComponent().getBounds(), false);
        }
   #endif
}

void SpaceForgeEditor::resized()
{
    // lay out at the design size, then scale to the window
    content.setBounds (0, 0, designWidth(), designHeight());
    auto area = content.getLocalBounds().reduced (14);
    if (sorter != nullptr)
    {
        auto strip = area.removeFromBottom (kSortHeight).withTrimmedTop (8);
        sortTitle.setBounds (strip.removeFromTop (28));
        sortHelp.setBounds (strip.removeFromTop (22));
    }

    // left: the logo, the settings and the browser (when shown)
    auto header = area.withHeight (kHeaderHeight);
    if (browserOpen())
    {
        auto left = area.removeFromLeft (kBrowserWidth);
        auto logoRow = left.removeFromTop (34);
        logoRow = logoRow.withTrimmedLeft (2);
        logo.setBounds (logoRow.removeFromTop (21));
        versionLabel.setBounds (logoRow.withTrimmedLeft (2));
        left.removeFromTop (4);
        browser.setBounds (left);
        area.removeFromLeft (14);
        header = area.removeFromTop (kHeaderHeight);
        browserToggle.setBounds (header.removeFromLeft (30).withSizeKeepingCentre (30, 30));
    }
    else
    {
        // folded: the toggle and the settings at the start of the header
        header = area.removeFromTop (kHeaderHeight);
        browserToggle.setBounds (header.removeFromLeft (30).withSizeKeepingCentre (30, 30));
    }

    // header: < name > and the actions, the Modes under it
    gear.setBounds (header.removeFromRight (34).withSizeKeepingCentre (30, 30));
    header.removeFromRight (4);
    auto actions = header.removeFromRight (184).withSizeKeepingCentre (184, 34);
    save.setBounds (actions.removeFromRight (70));
    actions.removeFromRight (8);
    undo.setBounds (actions.removeFromRight (34));
    dice.setBounds (actions.removeFromRight (34));
    actions.removeFromRight (4);
    favourite.setBounds (actions.removeFromRight (34));
    previous.setBounds (header.removeFromLeft (34).withSizeKeepingCentre (34, 34));
    next.setBounds (header.removeFromRight (34).withSizeKeepingCentre (34, 34));
    // name and category as one block, centred on the arrows
    auto nameBlock = header.withSizeKeepingCentre (header.getWidth(), 44);
    presetName.setBounds (nameBlock.removeFromTop (27));
    presetInfo.setBounds (nameBlock);

    auto modes = area.removeFromTop (kModesHeight).withSizeKeepingCentre (area.getWidth(), 26);
    modeLock.setBounds (modes.removeFromRight (30).withSizeKeepingCentre (30, 30));
    modes.removeFromRight (4);
    const int modeWidth = modes.getWidth() / dsp::kNumModes;
    for (const int m : dsp::kModeDisplayOrder)
        modeButtons[(size_t) m]->setBounds (modes.removeFromLeft (modeWidth));

    // Mix alone in the bottom-right corner, in a strip of its own beside the knobs
    auto mixStrip = area.removeFromRight (kMixSize);
    auto corner = mixStrip.removeFromBottom (kMixSize);
    // above Mix: the two small knobs, Echo and Duck
    {
        const int smallH = 70 + MacroKnob::kTextHeight;
        auto above = mixStrip.removeFromBottom (2 * smallH + 8);
        smalls[0]->setBounds (above.removeFromTop (smallH));
        above.removeFromTop (8);
        smalls[1]->setBounds (above);
    }
    mix.setBounds (corner.removeFromTop (72).withSizeKeepingCentre (68, 68));
    mixLabel.setBounds (corner.removeFromTop (20));
    mixValue.setBounds (corner.removeFromTop (18));

    // the four macros in a 2 x 2 grid (room for the scales' words), the cloud behind them
    cloud.setBounds (area);
    const auto grid = area.reduced (4, 6);
    const int cellW = grid.getWidth() / 2, cellH = grid.getHeight() / 2;
    const int knob = juce::jmin (juce::roundToInt ((float) cellW * 0.6f), cellH - MacroKnob::kTextHeight - 6);
    for (int k = 0; k < kNumMacros; ++k)
    {
        const juce::Rectangle<int> cell (grid.getX() + (k % 2) * cellW, grid.getY() + (k / 2) * cellH, cellW, cellH);
        macros[(size_t) k]->setBounds (cell.withSizeKeepingCentre (cellW - 8, knob + MacroKnob::kTextHeight));
    }

    // the column of small knobs and Mix ends where the grid ends
    const int lift = area.getBottom() - macros[2]->getBottom();
    if (lift > 0)
        for (auto* c : std::initializer_list<juce::Component*> { smalls[0].get(), smalls[1].get(), &mix, &mixLabel, &mixValue })
            c->setBounds (c->getBounds().translated (0, -lift));

    content.setTransform (juce::AffineTransform::scale ((float) getWidth() / (float) designWidth()));
}
