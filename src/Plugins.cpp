// Plugin entry point.
#include "copycat/Copycat.h"
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new CopycatProcessor(); }
