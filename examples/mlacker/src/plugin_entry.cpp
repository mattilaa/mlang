#include "plugin.h"
#include "plug_ids.h"
#include "version.h"

#include "pluginterfaces/vst/ivstaudioprocessor.h"
#include "public.sdk/source/main/pluginfactory.h"

using namespace Steinberg;
using namespace Steinberg::Vst;

#define stringPluginName "mlacker Drum Machine"
#define stringCompanyName "mlacker"
#define stringCompanyWeb "https://example.invalid/mlacker"
#define stringCompanyEmail "devnull@example.invalid"

BEGIN_FACTORY_DEF(stringCompanyName, stringCompanyWeb, stringCompanyEmail)
DEF_CLASS2(INLINE_UID_FROM_FUID(mlacker_drum::ProcessorUID),
           PClassInfo::kManyInstances,
           kVstAudioEffectClass,
           stringPluginName,
           0,
           PlugType::kInstrumentDrum,
           FULL_VERSION_STR,
           kVstVersionString,
           mlacker_drum::Plugin::createInstance)
END_FACTORY
