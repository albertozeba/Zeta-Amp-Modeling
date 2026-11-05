#include "PluginProcessor.h"
#include "PluginEditor.h"

using Coeffs = juce::dsp::IIR::Coefficients<float>;

AmpLabProcessor::AmpLabProcessor()
    : AudioProcessor (BusesProperties()
                          .withInput ("Input", juce::AudioChannelSet::stereo(), true)
                          .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "STATE", createLayout())
{
}

juce::AudioProcessorValueTreeState::ParameterLayout AmpLabProcessor::createLayout()
{
    using F = juce::AudioParameterFloat;
    juce::AudioProcessorValueTreeState::ParameterLayout l;
    auto knob = [&] (const char* id, const char* name, float def)
    {
        l.add (std::make_unique<F> (juce::ParameterID { id, 1 }, name,
                                    juce::NormalisableRange<float> (0.0f, 10.0f, 0.01f), def));
    };
    knob ("gate", "Gate", 2);      knob ("inp", "Ingresso", 5);   knob ("drive", "Drive", 5);
    knob ("bass", "Bassi", 5);     knob ("mid", "Medi", 5);       knob ("treble", "Alti", 5);
    knob ("level", "Livello", 3);
    knob ("modRate", "Velocità", 4); knob ("modDepth", "Profondità", 5); knob ("modMix", "Mix mod", 5);
    knob ("dtime", "Tempo", 4);    knob ("dfb", "Ripetizioni", 3.5f); knob ("dmix", "Mix delay", 3);
    knob ("rsize", "Ambiente", 4); knob ("rtone", "Tono", 6);     knob ("rmix", "Mix riverbero", 3);

    l.add (std::make_unique<juce::AudioParameterChoice> (juce::ParameterID { "modType", 1 }, "Modulazione",
                                                         juce::StringArray { "Off", "Chorus", "Flanger", "Phaser" }, 0));
    l.add (std::make_unique<juce::AudioParameterBool> (juce::ParameterID { "cabOn", 1 }, "Cabinet", true));
    l.add (std::make_unique<juce::AudioParameterBool> (juce::ParameterID { "dlyOn", 1 }, "Delay", false));
    l.add (std::make_unique<juce::AudioParameterBool> (juce::ParameterID { "revOn", 1 }, "Riverbero", false));
    return l;
}

bool AmpLabProcessor::isBusesLayoutSupported (const BusesLayout& l) const
{
    auto ok = [] (const juce::AudioChannelSet& s) { return s == juce::AudioChannelSet::mono() || s == juce::AudioChannelSet::stereo(); };
    return ok (l.getMainInputChannelSet()) && ok (l.getMainOutputChannelSet());
}

void AmpLabProcessor::prepareToPlay (double sr, int block)
{
    sampleRate = sr;
    juce::dsp::ProcessSpec spec { sr, (juce::uint32) block, 2 };

    for (auto* f : { &hp, &postLp, &bass, &mid, &treble, &cabHp, &cabLp1, &cabLp2 })
        f->prepare (spec);

    *hp.state     = *Coeffs::makeHighPass (sr, 80.0f, 0.7f);
    *postLp.state = *Coeffs::makeLowPass (sr, 7500.0f, 0.7f);
    *cabHp.state  = *Coeffs::makeHighPass (sr, 90.0f, 0.7f);
    *cabLp1.state = *Coeffs::makeLowPass (sr, 5200.0f, 0.9f);
    *cabLp2.state = *Coeffs::makeLowPass (sr, 5200.0f, 0.7f);
    lastBass = lastMid = lastTreble = -999.0f;

    gate.prepare (spec);
    gate.setRatio (10.0f);
    gate.setAttack (1.0f);
    gate.setRelease (60.0f);

    chorus.prepare (spec);
    phaser.prepare (spec);
    reverb.prepare (spec);
    delay.prepare (spec);
    delay.setMaximumDelayInSamples ((int) (sr * 1.2));
    delay.reset();
    lpState[0] = lpState[1] = 0.0f;

    os.initProcessing ((size_t) block);
    os.reset();
    setLatencySamples ((int) os.getLatencyInSamples());
}

void AmpLabProcessor::processBlock (juce::AudioBuffer<float>& buf, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;
    const int n = buf.getNumSamples();

    for (int c = getTotalNumInputChannels(); c < buf.getNumChannels(); ++c)
        buf.clear (c, 0, n);
    if (buf.getNumChannels() > 1)
        buf.copyFrom (1, 0, buf, 0, 0, n);   // la chitarra arriva sul canale 1: la copio anche sul 2

    auto p = [this] (const char* id) { return apvts.getRawParameterValue (id)->load(); };
    juce::dsp::AudioBlock<float> block (buf);
    juce::dsp::ProcessContextReplacing<float> ctx (block);

    // Ingresso e gate
    const float inG = juce::Decibels::decibelsToGain ((p ("inp") - 5.0f) * 2.4f);
    buf.applyGainRamp (0, n, lastIn, inG);
    lastIn = inG;
    if (p ("gate") >= 0.1f)
    {
        gate.setThreshold (-80.0f + p ("gate") * 6.0f);
        gate.process (ctx);
    }

    // Drive: distorsione con oversampling 4x
    hp.process (ctx);
    const float drv = juce::Decibels::decibelsToGain (p ("drive") * 3.6f);
    buf.applyGainRamp (0, n, lastDrive, drv);
    lastDrive = drv;
    {
        static const float t0 = std::tanh (0.15f);
        auto up = os.processSamplesUp (block);
        for (size_t c = 0; c < up.getNumChannels(); ++c)
        {
            auto* d = up.getChannelPointer (c);
            for (size_t i = 0; i < up.getNumSamples(); ++i)
                d[i] = (std::tanh (3.0f * d[i] + 0.15f) - t0) * 0.55f;
        }
        os.processSamplesDown (block);
    }
    postLp.process (ctx);

    // Equalizzatore a tre bande (i coefficienti si ricalcolano solo se la manopola cambia)
    auto upd = [] (Filter& f, float& last, float db, auto make)
    {
        if (std::abs (db - last) > 1.0e-4f) { last = db; *f.state = *make (juce::Decibels::decibelsToGain (db)); }
    };
    upd (bass,   lastBass,   (p ("bass") - 5.0f) * 2.4f,   [this] (float g) { return Coeffs::makeLowShelf (sampleRate, 120.0f, 0.707f, g); });
    upd (mid,    lastMid,    (p ("mid") - 5.0f) * 2.4f,    [this] (float g) { return Coeffs::makePeakFilter (sampleRate, 700.0f, 0.8f, g); });
    upd (treble, lastTreble, (p ("treble") - 5.0f) * 2.4f, [this] (float g) { return Coeffs::makeHighShelf (sampleRate, 3200.0f, 0.707f, g); });
    bass.process (ctx); mid.process (ctx); treble.process (ctx);

    // Cabinet simulato
    if (p ("cabOn") > 0.5f) { cabHp.process (ctx); cabLp1.process (ctx); cabLp2.process (ctx); }

    // Modulazione
    const int mt = (int) p ("modType");
    if (mt > 0)
    {
        const float rate = 0.1f * std::pow (10.0f, p ("modRate") / 5.0f);
        const float depth = p ("modDepth") / 10.0f, mix = p ("modMix") / 10.0f;
        if (mt == 3)
        {
            phaser.setRate (rate); phaser.setDepth (depth); phaser.setCentreFrequency (800.0f);
            phaser.setFeedback (0.4f); phaser.setMix (mix);
            phaser.process (ctx);
        }
        else
        {
            chorus.setRate (rate); chorus.setDepth (depth);
            chorus.setCentreDelay (mt == 1 ? 22.0f : 3.0f);
            chorus.setFeedback (mt == 1 ? 0.0f : 0.6f);
            chorus.setMix (mix * 0.5f);
            chorus.process (ctx);
        }
    }

    // Delay con ripetizioni scurite
    if (p ("dlyOn") > 0.5f)
    {
        delay.setDelay ((float) ((40.0 + p ("dtime") * 96.0) * 0.001 * sampleRate));
        const float fb = p ("dfb") * 0.085f, wet = p ("dmix") / 10.0f * 0.9f;
        const float k = 1.0f - std::exp (-6.2831853f * 3500.0f / (float) sampleRate);
        for (int c = 0; c < buf.getNumChannels(); ++c)
        {
            auto* d = buf.getWritePointer (c);
            for (int i = 0; i < n; ++i)
            {
                const float in = d[i], del = delay.popSample (c);
                lpState[c] += k * (del - lpState[c]);
                delay.pushSample (c, in + lpState[c] * fb);
                d[i] = in + del * wet;
            }
        }
    }

    // Riverbero
    if (p ("revOn") > 0.5f)
    {
        juce::dsp::Reverb::Parameters rp;
        rp.roomSize = 0.2f + p ("rsize") / 10.0f * 0.75f;
        rp.damping = 1.0f - p ("rtone") / 10.0f;
        rp.wetLevel = p ("rmix") / 10.0f * 0.4f;
        rp.dryLevel = 1.0f - p ("rmix") / 10.0f * 0.3f;
        rp.width = 1.0f; rp.freezeMode = 0.0f;
        reverb.setParameters (rp);
        reverb.process (ctx);
    }

    // Livello finale
    const float lv = p ("level") / 10.0f, master = lv * lv * 1.6f;
    buf.applyGainRamp (0, n, lastMaster, master);
    lastMaster = master;
    outLevel.store (std::max (buf.getMagnitude (0, n), outLevel.load() * 0.9f));
}

juce::AudioProcessorEditor* AmpLabProcessor::createEditor() { return new AmpLabEditor (*this); }

void AmpLabProcessor::getStateInformation (juce::MemoryBlock& d)
{
    if (auto xml = apvts.copyState().createXml()) copyXmlToBinary (*xml, d);
}

void AmpLabProcessor::setStateInformation (const void* data, int size)
{
    if (auto xml = getXmlFromBinary (data, size))
        if (xml->hasTagName (apvts.state.getType()))
            apvts.replaceState (juce::ValueTree::fromXml (*xml));
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new AmpLabProcessor(); }
