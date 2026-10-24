// Copyright (C) 2026 Henrik Juhl. Part of SpaceForge 2, licensed under the GNU AGPLv3 (see LICENSE).
// SPDX-License-Identifier: AGPL-3.0-or-later

// Renders the interface of a fresh instance to a PNG, for checking the layout (from SpaceForge 1):
//   sf2_screenshot <output.png> [scale] [knobs=s,e,m,t] [freeze] [play=seconds] [sort=folder]
// sort= shows the sorting mode with the candidates in that folder.
// knobs= sets Space, Echo, Motion and Tone (0..1); play= runs the bleeps test sound through the
// plug-in for that long and lets the picture follow, so echo rings and the auto-pan show.

#include "plugin/PluginEditor.h"
#include "plugin/Defaults.h"
#include "analysis/TestSignals.h"

#include <iostream>

int main (int argc, char* argv[])
{
    if (argc < 2)
    {
        std::cerr << "usage: sf2_screenshot <output.png> [scale] [knobs=s,e,m,t] [freeze] [play=seconds] [sort=folder]\n";
        return 1;
    }

    const juce::ScopedJuceInitialiser_GUI gui;
    const std::unique_ptr<juce::AudioProcessor> plugin (createPluginFilter());
    auto& processor = dynamic_cast<SpaceForgeProcessor&> (*plugin);
    const float scale = argc > 2 && ! juce::String (argv[2]).containsChar ('=') ? juce::String (argv[2]).getFloatValue() : 1.0f;

    float playSeconds = 0.0f;
    const char* ids[] = { spaceforge::plugin::kSpaceId, spaceforge::plugin::kEchoId, spaceforge::plugin::kMotionId, spaceforge::plugin::kToneId };
    for (int i = 2; i < argc; ++i)
    {
        const juce::String option (argv[i]);
        if (option.startsWith ("knobs="))
        {
            const auto values = juce::StringArray::fromTokens (option.fromFirstOccurrenceOf ("=", false, false), ",", "");
            for (int m = 0; m < juce::jmin (4, values.size()); ++m)
                processor.parameters().getParameter (ids[m])->setValueNotifyingHost (values[m].getFloatValue());
        }
        if (option == "freeze")
            processor.parameters().getParameter (spaceforge::plugin::kFreezeId)->setValueNotifyingHost (1.0f);
        if (option.startsWith ("play="))
            playSeconds = option.fromFirstOccurrenceOf ("=", false, false).getFloatValue();
    }

    std::unique_ptr<juce::AudioProcessorEditor> editor (processor.createEditor());
    auto* view = dynamic_cast<SpaceForgeEditor*> (editor.get());
    for (int i = 2; i < argc; ++i)
        if (juce::String (argv[i]).startsWith ("sort="))
            view->startSorting (juce::File::getCurrentWorkingDirectory().getChildFile (juce::String (argv[i]).fromFirstOccurrenceOf ("=", false, false)));
    if (playSeconds > 0.0f)
    {
        constexpr int block = 1600; // 1/30 s: one block per animation frame
        processor.prepareToPlay (48000.0, block);
        const auto sound = spaceforge::analysis::makeBleeps (48000.0, playSeconds).audio;
        juce::AudioBuffer<float> buffer (2, block);
        juce::MidiBuffer midi;
        for (size_t pos = 0; pos + block <= sound.l.size() && (float) pos / 48000.0f < playSeconds; pos += block)
        {
            buffer.copyFrom (0, 0, &sound.l[pos], block);
            buffer.copyFrom (1, 0, &sound.r[pos], block);
            processor.processBlock (buffer, midi);
            view->advanceAnimation (1.0f / 30.0f);
        }
    }
    else
        view->advanceAnimation (1.0f); // let the picture settle on the knobs

    const auto image = editor->createComponentSnapshot (editor->getLocalBounds(), true, scale);
    editor.reset();

    const juce::File file = juce::File::getCurrentWorkingDirectory().getChildFile (argv[1]);
    file.deleteFile();
    juce::FileOutputStream out (file);
    if (! out.openedOk() || ! juce::PNGImageFormat().writeImageToStream (image, out))
    {
        std::cerr << "could not write " << file.getFullPathName() << "\n";
        return 1;
    }
    std::cout << "wrote " << file.getFullPathName() << " (" << image.getWidth() << " x " << image.getHeight() << ")\n";
    return 0;
}
