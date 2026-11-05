#include "PluginEditor.h"

namespace
{
const juce::Colour brass (0xffc7a65a), cream (0xffefe3c2), bodyCol (0xff1f2d2a),
                   panelCol (0xff2d3f3a), ink (0xff231d10), lampRed (0xffe5533d), darkField (0xff111a18);
const char* rowTitles[4] = { "Cabinet", "Modulazione", "Delay", "Riverbero" };
}

AmpLook::AmpLook()
{
    setColour (juce::ComboBox::backgroundColourId, darkField);
    setColour (juce::ComboBox::textColourId, cream);
    setColour (juce::ComboBox::outlineColourId, brass.withAlpha (0.6f));
    setColour (juce::ComboBox::arrowColourId, brass);
    setColour (juce::PopupMenu::backgroundColourId, darkField);
    setColour (juce::PopupMenu::textColourId, cream);
    setColour (juce::PopupMenu::highlightedBackgroundColourId, brass);
    setColour (juce::PopupMenu::highlightedTextColourId, ink);
    setColour (juce::TextButton::buttonColourId, darkField);
    setColour (juce::TextButton::textColourOffId, cream);
}

void AmpLook::drawRotarySlider (juce::Graphics& g, int x, int y, int w, int h, float pos, float a0, float a1, juce::Slider&)
{
    auto b = juce::Rectangle<float> ((float) x, (float) y, (float) w, (float) h).reduced (8.0f);
    const float d = std::min (b.getWidth(), b.getHeight()), r = d * 0.5f;
    const auto c = b.getCentre();
    const juce::Rectangle<float> kb (c.x - r, c.y - r, d, d);
    const float ang = a0 + (a1 - a0) * pos;

    juce::Path track, val;
    track.addCentredArc (c.x, c.y, r + 5.0f, r + 5.0f, 0.0f, a0, a1, true);
    val.addCentredArc (c.x, c.y, r + 5.0f, r + 5.0f, 0.0f, a0, ang, true);
    g.setColour (juce::Colours::black.withAlpha (0.35f));
    g.strokePath (track, juce::PathStrokeType (2.5f));
    g.setColour (brass);
    g.strokePath (val, juce::PathStrokeType (2.5f));

    g.setGradientFill (juce::ColourGradient (juce::Colour (0xff3b362b), c.x - r * 0.4f, c.y - r * 0.5f,
                                             juce::Colour (0xff12100c), c.x + r * 0.6f, c.y + r * 0.7f, true));
    g.fillEllipse (kb);
    g.setColour (juce::Colours::black);
    g.drawEllipse (kb, 1.5f);

    juce::Path pointer;
    pointer.addRoundedRectangle (-1.8f, -r + 4.0f, 3.6f, r * 0.4f, 1.5f);
    g.setColour (cream);
    g.fillPath (pointer, juce::AffineTransform::rotation (ang).translated (c.x, c.y));
}

void AmpLook::drawToggleButton (juce::Graphics& g, juce::ToggleButton& b, bool highlighted, bool)
{
    const auto r = b.getLocalBounds().toFloat().reduced (1.0f);
    const bool on = b.getToggleState();
    g.setColour (on ? lampRed : darkField);
    g.fillRoundedRectangle (r, 6.0f);
    g.setColour (brass.withAlpha (highlighted ? 1.0f : 0.6f));
    g.drawRoundedRectangle (r, 6.0f, 1.2f);
    g.setColour (on ? juce::Colours::white : cream);
    g.setFont (15.0f);
    g.drawText (on ? "Attivo" : "Spento", b.getLocalBounds(), juce::Justification::centred);
}

AmpLabEditor::AmpLabEditor (AmpLabProcessor& p) : AudioProcessorEditor (&p), proc (p)
{
    setLookAndFeel (&look);
    addKnob ("gate", "Gate", 0);       addKnob ("inp", "Ingresso", 0);  addKnob ("drive", "Drive", 0);
    addKnob ("bass", "Bassi", 0);      addKnob ("mid", "Medi", 0);      addKnob ("treble", "Alti", 0);
    addKnob ("level", "Livello", 0);
    addKnob ("modRate", "Velocità", 1); addKnob ("modDepth", "Profondità", 1); addKnob ("modMix", "Mix", 1);
    addKnob ("dtime", "Tempo", 2);     addKnob ("dfb", "Ripetizioni", 2);     addKnob ("dmix", "Mix", 2);
    addKnob ("rsize", "Ambiente", 3);  addKnob ("rtone", "Tono", 3);          addKnob ("rmix", "Mix", 3);

    srcBox.addItemList ({ "Ingresso", "Riff di prova", "File" }, 1);
    cabBox.addItemList ({ "Nessuno", "Simulato", "IR" }, 1);
    modBox.addItemList ({ "Off", "Chorus", "Flanger", "Phaser", "Tremolo" }, 1);
    for (juce::Component* c : { (juce::Component*) &srcBox, (juce::Component*) &cabBox, (juce::Component*) &modBox,
                                (juce::Component*) &dlyBtn, (juce::Component*) &revBtn,
                                (juce::Component*) &fileBtn, (juce::Component*) &irBtn })
        addAndMakeVisible (*c);

    srcA = std::make_unique<APVTS::ComboBoxAttachment> (proc.apvts, "source", srcBox);
    cabA = std::make_unique<APVTS::ComboBoxAttachment> (proc.apvts, "cabMode", cabBox);
    modA = std::make_unique<APVTS::ComboBoxAttachment> (proc.apvts, "modType", modBox);
    dlyA = std::make_unique<APVTS::ButtonAttachment> (proc.apvts, "dlyOn", dlyBtn);
    revA = std::make_unique<APVTS::ButtonAttachment> (proc.apvts, "revOn", revBtn);

    auto* pr = &proc;
    fileBtn.onClick = [this, pr]
    {
        chooser = std::make_unique<juce::FileChooser> ("Scegli un campione di chitarra", juce::File(), "*.wav;*.aif;*.aiff;*.mp3;*.flac");
        chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                              [pr] (const juce::FileChooser& fc) { auto f = fc.getResult(); if (f.existsAsFile()) pr->loadSample (f); });
    };
    irBtn.onClick = [this, pr]
    {
        chooser = std::make_unique<juce::FileChooser> ("Scegli una Impulse Response", juce::File(), "*.wav;*.aif;*.aiff");
        chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                              [pr] (const juce::FileChooser& fc) { auto f = fc.getResult(); if (f.existsAsFile()) pr->loadIR (f); });
    };

    setSize (840, 660);
    startTimerHz (30);
}

AmpLabEditor::~AmpLabEditor() { setLookAndFeel (nullptr); }

void AmpLabEditor::addKnob (const char* id, const char* name, int row)
{
    auto k = std::make_unique<Knob>();
    const auto txt = row == 0 ? ink : cream;
    k->row = row;
    k->s.setSliderStyle (juce::Slider::RotaryVerticalDrag);
    k->s.setRotaryParameters (juce::degreesToRadians (225.0f), juce::degreesToRadians (495.0f), true);
    k->s.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 64, 16);
    k->s.setNumDecimalPlacesToDisplay (1);
    k->s.setColour (juce::Slider::textBoxTextColourId, txt);
    k->s.setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
    auto* par = proc.apvts.getParameter (id);
    k->s.setDoubleClickReturnValue (true, par->convertFrom0to1 (par->getDefaultValue()));
    addAndMakeVisible (k->s);
    k->l.setText (name, juce::dontSendNotification);
    k->l.setJustificationType (juce::Justification::centred);
    k->l.setColour (juce::Label::textColourId, txt);
    addAndMakeVisible (k->l);
    k->l.attachToComponent (&k->s, false);
    k->a = std::make_unique<APVTS::SliderAttachment> (proc.apvts, id, k->s);
    knobs.push_back (std::move (k));
}

void AmpLabEditor::paint (juce::Graphics& g)
{
    g.fillAll (bodyCol);
    g.setColour (cream);
    g.setFont (28.0f);
    g.drawText ("Amp Lab", 16, 12, 130, 36, juce::Justification::centredLeft);

    const float lv = std::min (1.0f, std::sqrt (proc.outLevel.load()));
    g.setColour (lampRed.withAlpha (lv > 0.02f ? 1.0f : 0.25f));
    g.fillEllipse (lampRect.toFloat());

    for (int r = 0; r < 4; ++r)
    {
        const auto rr = rowRect[r].toFloat();
        if (r == 0)
            g.setGradientFill (juce::ColourGradient (juce::Colour (0xffd3b469), rr.getX(), rr.getY(),
                                                     juce::Colour (0xffbd9b4f), rr.getX(), rr.getBottom(), false));
        else
            g.setColour (panelCol);
        g.fillRoundedRectangle (rr, 8.0f);
        g.setColour (r == 0 ? ink : cream);
        g.setFont (18.0f);
        g.drawText (rowTitles[r], rowRect[r].getX() + 16, rowRect[r].getY() + 12, 120, 22, juce::Justification::centredLeft);
    }

    g.setColour (juce::Colours::black.withAlpha (0.5f));
    g.fillRoundedRectangle (meterArea.toFloat(), 4.0f);
    g.setColour (lv > 0.9f ? juce::Colours::red : lv > 0.7f ? juce::Colours::orange : juce::Colours::limegreen);
    g.fillRoundedRectangle (meterArea.toFloat().withWidth ((float) meterArea.getWidth() * lv), 4.0f);
}

void AmpLabEditor::resized()
{
    auto area = getLocalBounds().reduced (16);
    auto hdr = area.removeFromTop (56);
    {
        auto rr = hdr.withSizeKeepingCentre (hdr.getWidth(), 30);
        irBtn.setBounds (rr.removeFromRight (120));   rr.removeFromRight (8);
        fileBtn.setBounds (rr.removeFromRight (120)); rr.removeFromRight (8);
        srcBox.setBounds (rr.removeFromRight (170));
    }
    juce::Component* heads[4] = { &cabBox, &modBox, &dlyBtn, &revBtn };
    const int hts[4] = { 150, 120, 120, 120 };
    for (int r = 0; r < 4; ++r)
    {
        auto row = area.removeFromTop (hts[r]);
        area.removeFromTop (10);
        rowRect[r] = row;
        auto head = row.removeFromLeft (150).reduced (12);
        head.removeFromTop (34);
        heads[r]->setBounds (head.removeFromTop (28));
        std::vector<Knob*> ks;
        for (auto& k : knobs) if (k->row == r) ks.push_back (k.get());
        const int w = row.getWidth() / (int) ks.size();
        for (auto* k : ks)
        {
            auto c = row.removeFromLeft (w).reduced (2);
            c.removeFromTop (22);
            k->s.setBounds (c);
        }
    }
    meterArea = area.removeFromBottom (14);
}
