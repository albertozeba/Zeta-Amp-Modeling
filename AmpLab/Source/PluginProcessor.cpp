#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "NAM/get_dsp.h"
#include <filesystem>

namespace
{
// Compatibile sia con process(NAM_SAMPLE*, ...) sia con la versione a canali process(NAM_SAMPLE**, ...)
template <class T>
auto namProcess (T& d, NAM_SAMPLE* in, NAM_SAMPLE* out, int n, int) -> decltype (d.process (&in, &out, n), void())
{
    NAM_SAMPLE* ip[1] = { in };
    NAM_SAMPLE* op[1] = { out };
    d.process (ip, op, n);
}
template <class T>
void namProcess (T& d, NAM_SAMPLE* in, NAM_SAMPLE* out, int n, long) { d.process (in, out, n); }
}

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
                                                         juce::StringArray { "Off", "Chorus", "Flanger", "Phaser", "Tremolo" }, 0));
    l.add (std::make_unique<juce::AudioParameterChoice> (juce::ParameterID { "cabMode", 1 }, "Cabinet",
                                                         juce::StringArray { "Nessuno", "Simulato", "IR" }, 1));
    l.add (std::make_unique<juce::AudioParameterChoice> (juce::ParameterID { "source", 1 }, "Sorgente",
                                                         juce::StringArray { "Ingresso", "Riff di prova", "File" }, 1));
    l.add (std::make_unique<juce::AudioParameterChoice> (juce::ParameterID { "ampMode", 1 }, "Ampli",
                                                         juce::StringArray { "Interno", "NAM" }, 0));
    l.add (std::make_unique<juce::AudioParameterBool> (juce::ParameterID { "namCab", 1 }, "NAM con cabinet", false));
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
    conv.prepare (spec);
    makeTestRiff (sr);

    maxBlock = std::max (block, 64);
    namIn.assign ((size_t) maxBlock, (NAM_SAMPLE) 0);
    namOut.assign ((size_t) maxBlock, (NAM_SAMPLE) 0);
    if (namPath.isNotEmpty()) loadNam (juce::File (namPath), false);   // ricarica il modello con i nuovi parametri audio
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

    // Sorgente interna: riff di prova o file caricato (sostituisce l'ingresso)
    {
        const juce::SpinLock::ScopedTryLockType tl (sampleLock);
        if (tl.isLocked() && samplePending) { audioSample = pendingSample; samplePending = false; playPos = 0; }
    }
    const int srcMode = (int) p ("source");
    if (srcMode > 0)
    {
        const juce::AudioBuffer<float>* sb = (srcMode == 1) ? &testBuf : audioSample.get();
        if (sb != nullptr && sb->getNumSamples() > 0)
        {
            const int len = sb->getNumSamples();
            const float* s = sb->getReadPointer (0);
            float* d = buf.getWritePointer (0);
            if (playPos >= len) playPos = 0;
            for (int i = 0; i < n; ++i) { d[i] = s[playPos]; if (++playPos >= len) playPos = 0; }
            if (buf.getNumChannels() > 1) buf.copyFrom (1, 0, buf, 0, 0, n);
        }
    }
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
    {
        const juce::SpinLock::ScopedTryLockType ml (modelLock);
        if (ml.isLocked() && modelPending) { std::swap (activeModel, pendingModel); modelPending = false; }
    }
    const bool useNam = ((int) p ("ampMode") == 1) && activeModel != nullptr && ! namIn.empty();
    if (useNam)
    {
        // Il modello NAM sostituisce drive e distorsione. La manopola Drive diventa il guadagno in ingresso al modello.
        const float pre = juce::Decibels::decibelsToGain ((p ("drive") - 5.0f) * 2.4f);
        buf.applyGainRamp (0, n, lastDrive, pre);
        lastDrive = pre;
        const int chunk = (int) namIn.size();
        const float og = namGain.load();
        float* d = buf.getWritePointer (0);
        for (int off = 0; off < n; off += chunk)
        {
            const int m = std::min (chunk, n - off);
            for (int i = 0; i < m; ++i) namIn[(size_t) i] = (NAM_SAMPLE) d[off + i];
            namProcess (*activeModel, namIn.data(), namOut.data(), m, 0);
            for (int i = 0; i < m; ++i) d[off + i] = (float) namOut[(size_t) i] * og;
        }
        if (buf.getNumChannels() > 1) buf.copyFrom (1, 0, buf, 0, 0, n);
    }
    else
    {
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
    }

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
    const int cm = (useNam && p ("namCab") > 0.5f) ? 0 : (int) p ("cabMode");   // modello head+cab: niente cabinet aggiuntivo
    if (cm == 1) { cabHp.process (ctx); cabLp1.process (ctx); cabLp2.process (ctx); }
    else if (cm == 2 && irLoaded.load()) conv.process (ctx);

    // Modulazione
    const int mt = (int) p ("modType");
    if (mt > 0)
    {
        const float rate = 0.1f * std::pow (10.0f, p ("modRate") / 5.0f);
        const float depth = p ("modDepth") / 10.0f, mix = p ("modMix") / 10.0f;
        if (mt == 4)
        {
            const float inc = rate / (float) sampleRate;
            for (int i = 0; i < n; ++i)
            {
                const float g = 1.0f - depth * 0.5f * (1.0f + std::sin (juce::MathConstants<float>::twoPi * trPhase));
                const float m = (1.0f - mix) + mix * g;
                for (int c = 0; c < buf.getNumChannels(); ++c) buf.getWritePointer (c)[i] *= m;
                trPhase += inc; if (trPhase >= 1.0f) trPhase -= 1.0f;
            }
        }
        else if (mt == 3)
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

void AmpLabProcessor::makeTestRiff (double sr)
{
    const int len = (int) (sr * 6.0);
    testBuf.setSize (1, len);
    testBuf.clear();
    float* o = testBuf.getWritePointer (0);
    struct Chord { double t; float f[3]; };
    const Chord chords[] = { { 0.0, { 82.4f, 123.5f, 164.8f } }, { 1.5, { 110.0f, 164.8f, 220.0f } },
                             { 3.0, { 98.0f, 146.8f, 196.0f } }, { 4.5, { 73.4f, 110.0f, 146.8f } } };
    juce::Random rnd (1234);
    for (auto& ch : chords)
        for (int k = 0; k < 3; ++k)
        {
            const int N = (int) std::round (sr / ch.f[k]);
            std::vector<float> ring ((size_t) N);
            for (auto& v : ring) v = rnd.nextFloat() * 2.0f - 1.0f;
            int pp = 0, s = (int) ((ch.t + k * 0.014) * sr);
            const int end = std::min (len, s + (int) (sr * 1.5));
            for (; s < end; ++s)
            {
                const float y = 0.5f * (ring[(size_t) pp] + ring[(size_t) ((pp + 1) % N)]) * 0.997f;
                ring[(size_t) pp] = y;
                pp = (pp + 1) % N;
                o[s] += y * 0.25f;
            }
        }
}

bool AmpLabProcessor::loadSample (const juce::File& f)
{
    juce::AudioFormatManager fm;
    fm.registerBasicFormats();
    std::unique_ptr<juce::AudioFormatReader> r (fm.createReaderFor (f));
    if (r == nullptr) return false;
    const int len = (int) std::min<juce::int64> (r->lengthInSamples, (juce::int64) (r->sampleRate * 60.0));
    if (len < 2) return false;
    juce::AudioBuffer<float> tmp ((int) r->numChannels, len);
    r->read (&tmp, 0, len, 0, true, true);
    const double ratio = r->sampleRate / sampleRate;
    const int outLen = std::max (2, (int) (len / ratio) - 1);
    auto out = std::make_shared<juce::AudioBuffer<float>> (1, outLen);
    float* o = out->getWritePointer (0);
    float pk = 0.0f;
    for (int i = 0; i < outLen; ++i)
    {
        const double pos = i * ratio;
        const int i0 = std::min ((int) pos, len - 1), i1 = std::min (i0 + 1, len - 1);
        const float fr = (float) (pos - i0);
        float s = 0.0f;
        for (int c = 0; c < tmp.getNumChannels(); ++c) s += tmp.getSample (c, i0) * (1.0f - fr) + tmp.getSample (c, i1) * fr;
        o[i] = s / (float) tmp.getNumChannels();
        pk = std::max (pk, std::abs (o[i]));
    }
    if (pk > 0.0f) out->applyGain (0.3f / pk);
    {
        const juce::SpinLock::ScopedLockType sl (sampleLock);
        pendingSample = out;
        samplePending = true;
    }
    if (auto* sp = apvts.getParameter ("source")) sp->setValueNotifyingHost (sp->convertTo0to1 (2.0f));
    return true;
}

bool AmpLabProcessor::loadIR (const juce::File& f)
{
    if (! f.existsAsFile()) return false;
    conv.loadImpulseResponse (f, juce::dsp::Convolution::Stereo::yes, juce::dsp::Convolution::Trim::yes, 0,
                              juce::dsp::Convolution::Normalise::yes);
    irLoaded = true;
    if (auto* cp = apvts.getParameter ("cabMode")) cp->setValueNotifyingHost (cp->convertTo0to1 (2.0f));
    return true;
}

bool AmpLabProcessor::loadNam (const juce::File& f, bool select)
{
    try
    {
        auto m = nam::get_dsp (std::filesystem::path (f.getFullPathName().toStdString()));
        if (m == nullptr) { namInfo = "Modello NAM non valido."; return false; }
        m->Reset (sampleRate, (int) std::max<size_t> (namIn.size(), 64));
        const float g = m->HasLoudness() ? juce::Decibels::decibelsToGain ((float) (-18.0 - m->GetLoudness())) : 1.0f;
        const double er = m->GetExpectedSampleRate();
        namGain.store (g);
        juce::String gear;
        {
            const juce::var j = juce::JSON::parse (f);
            if (auto* o = j.getProperty ("metadata", juce::var()).getDynamicObject())
                gear = o->getProperty ("gear_type").toString();
        }
        const bool hasCab = gear.containsIgnoreCase ("cab");   // es. amp_cab, amp_pedal_cab
        namInfo = "NAM: " + f.getFileName();
        if (er > 0.0 && std::abs (er - sampleRate) > 1.0)
            namInfo << "  (addestrato a " << juce::String ((int) er) << " Hz, in uso " << juce::String ((int) sampleRate) << " Hz)";
        {
            const juce::SpinLock::ScopedLockType ml (modelLock);
            pendingModel = std::move (m);
            modelPending = true;
        }
        if (hasCab) namInfo << "  (include il cabinet)";
        else if (gear.isEmpty()) namInfo << "  (tipo non dichiarato)";
        namPath = f.getFullPathName();
        if (select)
        {
            if (auto* ap = apvts.getParameter ("ampMode")) ap->setValueNotifyingHost (1.0f);
            if (auto* cp = apvts.getParameter ("namCab")) cp->setValueNotifyingHost (hasCab ? 1.0f : 0.0f);
        }
        return true;
    }
    catch (const std::exception& e) { namInfo = juce::String ("Errore NAM: ") + e.what(); return false; }
    catch (...) { namInfo = "Errore nel caricamento del modello NAM."; return false; }
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
