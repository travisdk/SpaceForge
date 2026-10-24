// Copyright (C) 2026 Henrik Juhl. Part of SpaceForge 2, licensed under the GNU AGPLv3 (see LICENSE).
// SPDX-License-Identifier: AGPL-3.0-or-later

#pragma once

#include <juce_core/juce_core.h>

// Showing the preset folder and deleting a user preset (from SubForge, same author).
namespace spaceforge::files
{
// Shows a folder in the file manager. On Linux, JUCE's File::startAsProcess() hands the path to a
// shell, which runs "$(...)" in a folder name; here xdg-open gets it as an argument (CLAUDE.md).
inline bool openFolder (const juce::File& folder)
{
#if JUCE_LINUX
    juce::ChildProcess opener; // xdg-open keeps running after this goes
    return folder.isDirectory() && opener.start (juce::StringArray { "xdg-open", folder.getFullPathName() }, 0);
#else
    return folder.startAsProcess();
#endif
}

// JUCE's Linux File::moveToTrash() skips the .trashinfo file, so file managers can't show or
// restore the file. This follows the freedesktop.org trash spec.
inline bool moveToTrash (const juce::File& file)
{
#if JUCE_LINUX
    const auto dataHome = juce::SystemStats::getEnvironmentVariable ("XDG_DATA_HOME", {});
    const auto trash = (dataHome.isNotEmpty() ? juce::File (dataHome)
                                              : juce::File::getSpecialLocation (juce::File::userHomeDirectory).getChildFile (".local/share"))
                           .getChildFile ("Trash");
    const auto files = trash.getChildFile ("files"), info = trash.getChildFile ("info");
    if (files.createDirectory() && info.createDirectory())
    {
        const auto target = files.getNonexistentChildFile (file.getFileNameWithoutExtension(), file.getFileExtension(), false);
        const auto infoFile = info.getChildFile (target.getFileName() + ".trashinfo");
        auto segments = juce::StringArray::fromTokens (file.getFullPathName(), "/", {});
        for (auto& segment : segments)
            segment = juce::URL::addEscapeChars (segment, false); // escape the parts, keep the slashes
        const auto infoText = "[Trash Info]\nPath=" + segments.joinIntoString ("/")
                              + "\nDeletionDate=" + juce::Time::getCurrentTime().formatted ("%Y-%m-%dT%H:%M:%S") + "\n";
        if (infoFile.replaceWithText (infoText))
        {
            if (file.moveFileTo (target))
                return true;
            infoFile.deleteFile();
        }
    }
#endif
    return file.moveToTrash();
}
} // namespace spaceforge::files
