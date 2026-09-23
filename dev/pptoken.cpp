#include <cstdlib>
#include <iostream>
#include <stdexcept>
#include <string>

#include "preprocess/tokens/DebugPPTokenStream.h"
#include "preprocess/tokens/PPTokenizer.h"

namespace
{

std::string ReadStandardInput()
{
	std::string input;
	char input_chunk[64 * 1024];
	for (;;)
	{
		std::cin.read(input_chunk, sizeof(input_chunk));
		const std::streamsize bytes_read = std::cin.gcount();
		if (bytes_read > 0)
			input.append(input_chunk, static_cast<std::size_t>(bytes_read));
		if (std::cin.bad())
			throw std::runtime_error("failed to read source input");
		if (std::cin.eof())
			break;
		if (!std::cin)
			throw std::runtime_error("failed to read source input");
	}
	return input;
}

} // namespace

int main(int argc, char** argv)
{
	(void)argc;
	(void)argv;
	try
	{
		const std::string source = ReadStandardInput();
		DebugPPTokenStream output;
		TokenizePreprocessingSource(source, output);
		return EXIT_SUCCESS;
	}
	catch (const std::exception& error)
	{
		std::cerr << "ERROR: " << error.what() << std::endl;
		return EXIT_FAILURE;
	}
}
