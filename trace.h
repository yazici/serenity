#pragma once
#include "string.h"

struct Symbol { string function; ref<byte> file; uint line=0; };
/// Returns debug symbol nearest to address
Symbol findNearestLine(void* address);
/// Traces current stack skipping first \a skip frames
string trace(int skip, void* ip=0);
