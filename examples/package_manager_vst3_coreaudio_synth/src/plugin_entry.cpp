#include "plugin.h"
#include "plug_ids.h"
#include "version.h"

#include "pluginterfaces/vst/ivstaudioprocessor.h"
#include "public.sdk/source/main/pluginfactory.h"

using namespace Steinberg;
using namespace Steinberg::Vst;

#define stringPluginName "MLang Mini Synth"
#define stringCompanyName "MLang"
#define stringCompanyWeb "https://example.invalid/mlang"
#define stringCompanyEmail "devnull@example.invalid"

BEGIN_FACTORY_DEF(stringCompanyName, stringCompanyWeb, stringCompanyEmail)
DEF_CLASS2(INLINE_UID_FROM_FUID(mlang_vst3_example::ProcessorUID),
           PClassInfo::kManyInstances,
           kVstAudioEffectClass,
           stringPluginName,
           0,
           PlugType::kInstrumentSynth,
           FULL_VERSION_STR,
           kVstVersionString,
           mlang_vst3_example::Plugin::createInstance)
END_FACTORY
