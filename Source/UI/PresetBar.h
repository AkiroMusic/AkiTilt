#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include "Theme.h"

namespace aki::ui
{

// Small flat button drawing a folder outline; opens the user preset folder.
class FolderButton final : public juce::Button
{
public:
    FolderButton() : juce::Button ("Folder") {}

    void paintButton (juce::Graphics& g, bool over, bool down) override
    {
        juce::ignoreUnused (down);
        auto bounds = getLocalBounds().toFloat().reduced (1.0f);
        juce::Path p;
        p.addRoundedRectangle (bounds, 10.0f);
        g.setColour ((over || isOver()) ? theme::colours::accent.withAlpha (0.14f)
                              : theme::colours::surface2);
        g.fillPath (p);
        g.setColour ((over || isOver()) ? theme::colours::accent : theme::colours::border);
        g.strokePath (p, juce::PathStrokeType (1.0f));

        // Folder glyph.
        const float fw = 11.0f, fh = 8.0f;
        const float fx = bounds.getCentreX() - fw * 0.5f;
        const float fy = bounds.getCentreY() - fh * 0.5f;
        juce::Path folder;
        folder.startNewSubPath (fx, fy + fh);
        folder.lineTo (fx, fy + 1.5f);
        folder.quadraticTo (fx, fy, fx + 1.5f, fy);
        folder.lineTo (fx + fw * 0.42f, fy);
        folder.lineTo (fx + fw * 0.55f, fy + 2.2f);
        folder.lineTo (fx + fw - 1.5f, fy + 2.2f);
        folder.quadraticTo (fx + fw, fy + 2.2f, fx + fw, fy + 3.4f);
        folder.lineTo (fx + fw, fy + fh - 1.5f);
        folder.quadraticTo (fx + fw, fy + fh, fx + fw - 1.5f, fy + fh);
        folder.closeSubPath();
        g.setColour ((over || isOver()) ? theme::colours::accent : theme::colours::textSecondary);
        g.strokePath (folder, juce::PathStrokeType (1.2f, juce::PathStrokeType::curved,
                                                    juce::PathStrokeType::rounded));
    }
};

// Preset strip: A/B slots, prev/next stepping, the preset selector (factory
// section, separator, user section) and Save / open-folder buttons.
class PresetBar final : public juce::Component
{
public:
    std::function<void (int index)> onPresetChosen;
    std::function<void()> onPrev;
    std::function<void()> onNext;
    std::function<void (char slot)> onSlot;
    std::function<void()> onSave;
    std::function<void()> onOpenFolder;

    PresetBar()
    {
        slotA.setButtonText ("A");
        slotB.setButtonText ("B");
        slotA.setClickingTogglesState (true);
        slotB.setClickingTogglesState (true);
        slotA.setRadioGroupId (77);
        slotB.setRadioGroupId (77);
        slotA.setToggleState (true, juce::dontSendNotification);
        for (auto* b : { &slotA, &slotB })
        {
            b->onStateChange = [this, b]()
            {
                if (b->getToggleState() && onSlot)
                    onSlot (b == &slotA ? 'A' : 'B');
            };
            addAndMakeVisible (b);
        }

        prev.setButtonText ("<");
        next.setButtonText (">");
        for (auto* b : { &prev, &next })
        {
            b->onClick = [this, b]()
            {
                if (b == &prev)
                {
                    if (onPrev)
                        onPrev();
                }
                else if (onNext)
                {
                    onNext();
                }
            };
            addAndMakeVisible (b);
        }

        save.setButtonText ("Save");
        save.onClick = [this]()
        {
            if (onSave)
                onSave();
        };
        addAndMakeVisible (save);

        openFolder.onClick = [this]()
        {
            if (onOpenFolder)
                onOpenFolder();
        };
        addAndMakeVisible (openFolder);

        selector.setJustificationType (juce::Justification::centredLeft);
        selector.setColour (juce::ComboBox::textColourId, theme::colours::textPrimary);
        selector.setColour (juce::ComboBox::backgroundColourId, theme::colours::surface2);
        selector.setColour (juce::ComboBox::outlineColourId, theme::colours::border);
        selector.setColour (juce::ComboBox::arrowColourId, theme::colours::textSecondary);
        selector.onChange = [this]()
        {
            if (selector.getSelectedId() > 0 && onPresetChosen)
                onPresetChosen (selector.getSelectedId() - 1);
        };
        addAndMakeVisible (selector);
    }

    // factoryCount entries are listed first, then a separator, then the rest.
    void setItems (const juce::StringArray& names, int factoryCount, int selectedIndex)
    {
        selector.clear (juce::dontSendNotification);
        for (int i = 0; i < names.size(); ++i)
        {
            if (i == factoryCount && factoryCount > 0 && factoryCount < names.size())
                selector.addSeparator();
            selector.addItem (names[i], i + 1);
        }
        setSelectedIndex (selectedIndex);
    }

    void setSelectedIndex (int index)
    {
        selector.setSelectedId (index + 1, juce::dontSendNotification);
    }

    void resized() override
    {
        auto bounds = getLocalBounds();
        auto slotArea = bounds.removeFromLeft (76);
        slotA.setBounds (slotArea.removeFromLeft (34).withSizeKeepingCentre (30, 22));
        slotB.setBounds (slotArea.withSizeKeepingCentre (30, 22));
        bounds.removeFromLeft (8);
        prev.setBounds (bounds.removeFromLeft (26).withSizeKeepingCentre (22, 22));
        bounds.removeFromLeft (4);
        openFolder.setBounds (bounds.removeFromRight (26).withSizeKeepingCentre (22, 22));
        save.setBounds (bounds.removeFromRight (44).withSizeKeepingCentre (40, 22));
        bounds.removeFromRight (6);
        next.setBounds (bounds.removeFromRight (26).withSizeKeepingCentre (22, 22));
        bounds.removeFromRight (4);
        selector.setBounds (bounds);
    }

    juce::ToggleButton slotA { "A" }, slotB { "B" };
    juce::TextButton prev { "<" }, next { ">" };
    juce::TextButton save { "Save" };
    FolderButton openFolder;
    juce::ComboBox selector;
};

} // namespace aki::ui
