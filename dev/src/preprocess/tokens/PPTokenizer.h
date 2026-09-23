#pragma once

#include <string>

#include "preprocess/tokens/IPPTokenStream.h"

void TokenizePreprocessingSource(const std::string& source,
	IPPTokenStream& output);
