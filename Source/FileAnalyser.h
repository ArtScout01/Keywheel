#pragma once
#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_core/juce_core.h>
#include <juce_events/juce_events.h>
#include "Analyzer.h"

// Liest eine Audiodatei (max. 150 s aus der Mitte), mischt auf Mono, analysiert Key + BPM
// in einem Hintergrundthread und meldet das Ergebnis auf dem Message-Thread zurueck.
class FileAnalyser : private juce::Thread
{
public:
    struct Outcome
    {
        bool ok = false;
        juce::String error, fileName;
        analysis::Result result;
    };
    using Callback = std::function<void (Outcome)>;

    FileAnalyser() : juce::Thread ("Key/BPM analyser") { formats.registerBasicFormats(); }
    ~FileAnalyser() override { stopThread (20000); }

    void analyse (const juce::File& f, Callback cb)
    {
        stopThread (20000);
        file = f;
        callback = std::move (cb);
        startThread (juce::Thread::Priority::background);
    }

private:
    void post (Outcome o)
    {
        auto cb = callback;
        juce::MessageManager::callAsync ([cb, o] { if (cb) cb (o); });
    }

    void run() override
    {
        Outcome o;
        o.fileName = file.getFileName();

        std::unique_ptr<juce::AudioFormatReader> reader (formats.createReaderFor (file));
        if (reader == nullptr || reader->lengthInSamples <= 0 || reader->sampleRate <= 0 || reader->numChannels == 0)
        {
            o.error = "Could not read this file (unsupported format?)";
            post (o);
            return;
        }

        const double sr = reader->sampleRate;
        const juce::int64 maxLen = (juce::int64) (sr * 150.0);
        const juce::int64 total = reader->lengthInSamples;
        const juce::int64 len = juce::jmin (total, maxLen);
        const juce::int64 start = total > maxLen ? (total - maxLen) / 2 : 0;

        const int numCh = (int) juce::jmin ((unsigned int) 2, reader->numChannels);
        constexpr int block = 65536;
        juce::AudioBuffer<float> buf (numCh, block);
        std::vector<float> mono;
        mono.reserve ((size_t) len);

        for (juce::int64 done = 0; done < len;)
        {
            if (threadShouldExit()) return;
            const int n = (int) juce::jmin ((juce::int64) block, len - done);
            buf.clear();
            if (! reader->read (&buf, 0, n, start + done, true, numCh > 1))
            {
                o.error = "Error while decoding the file";
                post (o);
                return;
            }
            for (int i = 0; i < n; ++i)
            {
                float s = 0.0f;
                for (int c = 0; c < numCh; ++c) s += buf.getSample (c, i);
                mono.push_back (s / (float) numCh);
            }
            done += n;
        }

        o.result = analysis::analyse (std::move (mono), sr, [this] { return threadShouldExit(); });
        if (threadShouldExit()) return;

        o.ok = o.result.ok;
        if (! o.ok) o.error = "Nothing to analyse (silent or too short)";
        post (o);
    }

    juce::AudioFormatManager formats;
    juce::File file;
    Callback callback;
};
