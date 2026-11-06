#pragma once
#include "PluginProcessor.h"

struct AmpLook : public juce::LookAndFeel_V4
{
    AmpLook();
    void drawRotarySlider (juce::Graphics&, int x, int y, int w, int h, float pos, float a0, float a1, juce::Slider&) override;
    void drawToggleButton (juce::Graphics&, juce::ToggleButton&, bool highlighted, bool down) override;
};

class AmpLabEditor : public juce::AudioProcessorEditor, private juce::Timer
{
public:
    explicit AmpLabEditor (AmpLabProcessor&);
    ~AmpLabEditor() override;
    void paint (juce::Graphics&) override;
    void resized() override;

private:
    using APVTS = juce::AudioProcessorValueTreeState;
    struct Knob
    {
        int row = 0;
        juce::Slider s;
        juce::Label l;
        std::unique_ptr<APVTS::SliderAttachment> a;
    };

    void addKnob (const char* id, const char* name, int row);
    void updateEnabled();
    void timerCallback() override { updateEnabled(); repaint (meterArea); repaint (lampRect); repaint (statusArea); }

    AmpLook look;
    AmpLabProcessor& proc;
    std::vector<std::unique_ptr<Knob>> knobs;
    juce::ToggleButton dlyBtn { "Delay" }, revBtn { "Riverbero" }, camBtn { "Il modello NAM include il cabinet" };
    juce::ComboBox srcBox, cabBox, modBox, ampBox;
    juce::TextButton fileBtn { "Carica file..." }, irBtn { "Carica IR..." }, namBtn { "Carica NAM..." };
    std::unique_ptr<APVTS::ButtonAttachment> dlyA, revA, camA;
    std::unique_ptr<APVTS::ComboBoxAttachment> srcA, cabA, modA, ampA;
    std::unique_ptr<juce::FileChooser> chooser;
    juce::Image logo;
    juce::Rectangle<int> statusArea, meterArea, lampRect { 244, 26, 12, 12 }, rowRect[4];
};
