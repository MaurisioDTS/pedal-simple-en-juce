#include "../JuceLibraryCode/JuceHeader.h"
#include "Distortion.h"

//==============================================================================
Distortion::Distortion(AudioProcessorValueTreeState& vt): mParameters(vt), mSelector(0)
{
    mOversampling.reset(new dsp::Oversampling<float>(2, 3, dsp::Oversampling<float>::filterHalfBandPolyphaseIIR, false));

    //  ================================
    //  array de lambdas (no sabía que se podía hacer eso xd)
    //
    //      basicamente guardo todas las funciones de onda para aplicarlas más tarde.
    //      se pueden añadir tantas funciones como quieras-
    //      y no reventar el programa por el camino.
    mWaveFunctions = {
        [](float x) { return std::tanh(5.0f * x); },            //  softclipper
        [](float x) {
            float highFreqBoost = x - 0.8f * std::tanh(x);      //      (quito algo de bajo)
            float emphasized = x + 0.4f * (highFreqBoost - x);  //      (añado agudos)
            emphasized = emphasized * 5.2f;                     //      (lo aumentamos y se lo damos al siguiente)
            return std::clamp(emphasized, -0.9f, 0.9f);},       //  treblebooster
        [](float x) { return std::clamp(x,-0.7f,0.7f);}         //  hardclipper
    };

    // esto hace que la funcion por defecto sea la primera del array, el softclipper.
    mCurrentWaveFunction = mWaveFunctions[0];
}

Distortion::~Distortion()
{
    //  destructor vacío, maybe le pongo cosas en el futuro. (spoiler: no)
}

void Distortion::prepare(dsp::ProcessSpec spec)
{
    mSampleRate = static_cast<float>(spec.sampleRate);
    mMaxBlockSize = spec.maximumBlockSize;
    mNumChannels = spec.numChannels;

    mInputVolume.prepare(spec);
    mOutputVolume.prepare(spec);

    mOversampling->initProcessing(static_cast<size_t>(mMaxBlockSize));
    reset();
}

void Distortion::reset()
{
    mOversampling->reset();
}

void Distortion::process(dsp::ProcessContextReplacing<float> context)
{
    ScopedNoDenormals noDenormals;
    mInputVolume.process(context);

    // Upsample
    dsp::AudioBlock<float> oversampledBlock = mOversampling->processSamplesUp(context.getInputBlock());

    // ==============================
    // aplico la funcion de onda directamente sin usar waveshaper

    for (size_t channel = 0; channel < oversampledBlock.getNumChannels(); ++channel)
    {
        auto* samples = oversampledBlock.getChannelPointer(channel);

        for (size_t sampleIndex = 0; sampleIndex < oversampledBlock.getNumSamples(); ++sampleIndex)
        {
            samples[sampleIndex] = mCurrentWaveFunction(samples[sampleIndex]);
        }
    }

    oversampledBlock *= 0.7f; // Escala el volumen después del procesamiento

    // Downsample
    mOversampling->processSamplesDown(context.getOutputBlock());
    mOutputVolume.process(context);
}


void Distortion::updateParameters()
{
    float inputVolume = *mParameters.getRawParameterValue(IDs::inputVolume);
    float outputVolume = *mParameters.getRawParameterValue(IDs::outputVolume);
    int selector = static_cast<int>(*mParameters.getRawParameterValue(IDs::selector));

    // =============================================
    //  aqui declaro que cuando el gain sube el out baja respecto a la cantidad de in puesta.
    //  +12 para balancear lo que bajamos y que se escuche un poco más alto.
    //  esta funcionalidad aún está en prueba.
    outputVolume *= - ((inputVolume/2)+12);

    auto inputdB = Decibels::decibelsToGain(inputVolume);
    auto outputdB = Decibels::decibelsToGain(outputVolume);

    if (mInputVolume.getGainLinear() != inputdB) mInputVolume.setGainLinear(inputdB);
    if (mOutputVolume.getGainLinear() != outputdB) mOutputVolume.setGainLinear(outputdB);

    // este if cambia la función de onda respecto al selector.
    if (selector >= 0 && selector < mWaveFunctions.size())
    {
        mCurrentWaveFunction = mWaveFunctions[selector];
    }
}
