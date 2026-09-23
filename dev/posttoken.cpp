// (C) 2013 CPPGM Foundation www.cppgm.org.  All rights reserved.

#include <cstdlib>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

#include "postprocess/posttoken.h"

namespace
{

std::string ReadSource(std::istream& input)
{
	std::string source;
	char chunk[64 * 1024];
	for (;;)
	{
		input.read(chunk, sizeof(chunk));
		const std::streamsize count = input.gcount();
		if (count > 0)
			source.append(chunk, static_cast<std::size_t>(count));
		if (input.bad())
			throw std::runtime_error("failed to read source input");
		if (input.eof()) break;
		if (!input)
			throw std::runtime_error("failed to read source input");
	}
	return source;
}

bool HasBatchStdinArg(int argc, char** argv)
{
	for (int i = 1; i < argc; ++i)
		if (std::string(argv[i]) == "--batch-stdin") return true;
	return false;
}

std::vector<std::string> SplitFields(const std::string& line)
{
	std::vector<std::string> fields;
	size_t start = 0;
	for (;;)
	{
		const size_t tab = line.find('\t', start);
		fields.push_back(line.substr(start,
			tab == std::string::npos ? std::string::npos : tab - start));
		if (tab == std::string::npos) break;
		start = tab + 1;
	}
	return fields;
}

int RunBatchStdin()
{
	std::string line;
	while (std::getline(std::cin, line))
	{
		if (!line.empty() && line[line.size() - 1] == '\r') line.erase(line.size() - 1);
		if (line.empty()) continue;
		const std::vector<std::string> fields = SplitFields(line);
		if (fields.size() < 3)
		{
			std::cout << "EXIT_FAILURE\n";
			continue;
		}
		std::ofstream output_file(fields[0].c_str(), std::ios::out | std::ios::trunc);
		std::ofstream error_file(fields[1].c_str(), std::ios::out | std::ios::trunc);
		std::ifstream input_file(fields[2].c_str(), std::ios::in | std::ios::binary);
		if (!output_file || !error_file || !input_file)
		{
			std::cout << "EXIT_FAILURE\n";
			continue;
		}
		std::streambuf* old_out = std::cout.rdbuf(output_file.rdbuf());
		std::streambuf* old_err = std::cerr.rdbuf(error_file.rdbuf());
		std::string status = "EXIT_SUCCESS";
		try
		{
			PostTokenizeSource(ReadSource(input_file));
		}
		catch (const std::exception& error)
		{
			std::cerr << "ERROR: " << error.what() << std::endl;
			status = "EXIT_FAILURE";
		}
		std::cout.flush();
		std::cerr.flush();
		std::cout.rdbuf(old_out);
		std::cerr.rdbuf(old_err);
		std::cout << status << '\n';
	}
	return EXIT_SUCCESS;
}

} // namespace

int main(int argc, char** argv)
{
	if (HasBatchStdinArg(argc, argv)) return RunBatchStdin();
	try
	{
		PostTokenizeSource(ReadSource(std::cin));
		return EXIT_SUCCESS;
	}
	catch (const std::exception& error)
	{
		std::cerr << "ERROR: " << error.what() << std::endl;
		return EXIT_FAILURE;
	}
}
