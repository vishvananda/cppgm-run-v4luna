#include <cstdlib>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>

#include "preprocess/tokens/DebugPPTokenStream.h"
#include "preprocess/tokens/PPTokenizer.h"

int main(int argc, char** argv)
{
	(void)argc;
	(void)argv;
	try
	{
		std::ostringstream input_stream;
		input_stream << std::cin.rdbuf();
		const std::string input = input_stream.str();
		DebugPPTokenStream output;
		TokenizePreprocessingSource(input, output);
		return EXIT_SUCCESS;
	}
	catch (const std::exception& error)
	{
		std::cerr << "ERROR: " << error.what() << std::endl;
		return EXIT_FAILURE;
	}
}
