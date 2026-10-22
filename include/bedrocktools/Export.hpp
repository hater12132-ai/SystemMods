#pragma once

#if defined(__GNUC__) || defined(__clang__)
#define BEDROCKTOOLS_API __attribute__((visibility("default")))
#else
#define BEDROCKTOOLS_API
#endif
