#include "PluginEditor.h"

static const juce::Colour kPlate (0xffc7a65a), kText (0xffefe3c2), kBody (0xff1f2d2a);

AmpLabEditor::AmpLabEditor (AmpLabProcessor& p) : AudioProcessorEditor (&p), proc (p)
{
    addKnob ("gate", "Gate", 0);       addKnob ("inp", "Ingresso", 0);  addKnob ("drive", "Drive", 0);
    addKnob ("bass", "Bassi", 0);      addKnob ("mid", "Medi", 0);      addKnob ("treble", "Alti", 0);
    addKnob ("level", "Livello", 0);
    addKnob ("modRate", "Velocità", 1); addKnob ("modDepth", "Profondità", 1); addKnob ("modMix", "Mix", 1);
    addKnob ("dtime", "Tempo", 2);     addKnob ("dfb", "Ripetizioni", 2);     addKnob ("dmix", "Mix", 2);
    addKnob ("rsize", "Ambiente", 3);  addKnob ("rtone", "Tono", 3);          addKnob ("rmix", "Mix", 3);

    for (auto* b : { &cabBtn, &dlyBtn, &revBtn })
    {
        b->setColour (juce::ToggleButton::textColourId, kText);
        b->setColour (juce::ToggleButton::tickColourId, kPlate);
        addAndMakeVisible (*b);
    }
    modBox.addItemList ({ "Off", "Chorus", "Flanger", "Phaser" }, 1);
    addAndMakeVisible (modBox);
    modLbl.setText ("Modulazione", juce::dontSendNotification);
    modLbl.setColour (juce::Label::textColourId, kText);
    addAndMakeVisible (modLbl);
    modLbl.attachToComponent (&modBox, false);

    cabA = std::make_unique<APVTS::ButtonAttachment> (proc.apvts, "cabOn", cabBtn);
    dlyA = std::make_unique<APVTS::ButtonAttachment> (proc.apvts, "dlyOn", dlyBtn);
    revA = std::make_unique<APVTS::ButtonAttachment> (proc.apvts, "revOn", revBtn);
    modA = std::make_unique<APVTS::ComboBoxAttachment> (proc.apvts, "modType", modBox);

    setSize (780, 640);
    startTimerHz (30);
}

void AmpLabEditor::addKnob (const char* id, const char* name, int row)
{
    auto k = std::make_unique<Knob>();
    k->row = row;
    k->s.setSliderStyle (juce::Slider::RotaryVerticalDrag);
    k->s.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 60, 16);
    k->s.setNumDecimalPlacesToDisplay (1);
    k->s.setColour (juce::Slider::rotarySliderFillColourId, kPlate);
    k->s.setColour (juce::Slider::thumbColourId, kText);
    k->s.setColour (juce::Slider::textBoxTextColourId, kText);
    k->s.setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
    k->s.setDoubleClickReturnValue (true, proc.apvts.getParameter (id)->convertFrom0to1 (proc.apvts.getParameter (id)->getDefaultValue()));
    addAndMakeVisible (k->s);
    k->l.setText (name, juce::dontSendNotification);
    k->l.setJustificationType (juce::Justification::centred);
    k->l.setColour (juce::Label::textColourId, kText);
    addAndMakeVisible (k->l);
    k->l.attachToComponent (&k->s, false);
    k->a = std::make_unique<APVTS::SliderAttachment> (proc.apvts, id, k->s);
    knobs.push_back (std::move (k));
}

void AmpLabEditor::paint (juce::Graphics& g)
{
    g.fillAll (kBody);
    g.setColour (kPlate);
    g.setFont (24.0f);
    g.drawText ("Amp Lab", 16, 8, 300, 30, juce::Justification::centredLeft);
    g.setColour (juce::Colours::black.withAlpha (0.5f));
    g.fillRoundedRectangle (meterArea.toFloat(), 4.0f);
    const float lv = std::min (1.0f, std::sqrt (proc.outLevel.load()));
    g.setColour (lv > 0.9f ? juce::Colours::red : lv > 0.7f ? juce::Colours::orange : juce::Colours::limegreen);
    g.fillRoundedRectangle (meterArea.toFloat().withWidth (meterArea.getWidth() * lv), 4.0f);
}

void AmpLabEditor::resized()
{
    auto area = getLocalBounds().reduced (16);
    area.removeFromTop (36);
    juce::Component* heads[4] = { &cabBtn, &modBox, &dlyBtn, &revBtn };
    for (int r = 0; r < 4; ++r)
    {
        auto row = area.removeFromTop (r == 0 ? 150 : 130);
        auto head = row.removeFromLeft (130);
        heads[r]->setBounds (head.withSizeKeepingCentre (110, 26));
        std::vector<Knob*> ks;
        for (auto& k : knobs) if (k->row == r) ks.push_back (k.get());
        const int w = row.getWidth() / (int) ks.size();
        for (auto* k : ks)
        {
            auto c = row.removeFromLeft (w).reduced (4);
            c.removeFromTop (18);
            k->s.setBounds (c);
        }
    }
    meterArea = area.removeFromBottom (14);
}
