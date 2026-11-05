#pragma once
#include "PluginProcessor.h"

class AmpLabEditor : public juce::AudioProcessorEditor, private juce::Timer
{
public:
    explicit AmpLabEditor (AmpLabProcessor&);
    ~AmpLabEditor() override = default;
    void paint (juce::Graphics&) override;
    void resized() override;

private:
    using APVTS = juce::AudioProcessorValueTreeState;
    struct Knob
    {
        int row = 0;
        juce::Slider s;
        juce::Label l;
        std::unique_ptr<APVTS::SliderAttachment> a;   // dichiarato dopo lo slider: viene distrutto prima
    };

    void addKnob (const char* id, const char* name, int row);
    void timerCallback() override { repaint (meterArea); }

    AmpLabProcessor& proc;
    std::vector<std::unique_ptr<Knob>> knobs;
    juce::ToggleButton cabBtn { "Cabinet" }, dlyBtn { "Delay" }, revBtn { "Riverbero" };
    juce::ComboBox modBox;
    juce::Label modLbl;
    std::unique_ptr<APVTS::ButtonAttachment> cabA, dlyA, revA;
    std::unique_ptr<APVTS::ComboBoxAttachment> modA;
    juce::Rectangle<int> meterArea;
};
