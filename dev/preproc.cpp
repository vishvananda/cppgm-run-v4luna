// (C) 2013 CPPGM Foundation www.cppgm.org.  All rights reserved.

#include <sys/stat.h>

#include <cstdlib>
#include <ctime>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include "postprocess/posttoken.h"
#include "preprocess/preprocessor.h"

using namespace std;

typedef pair<unsigned long long, unsigned long long> PreprocessorFileId;

bool GetPreprocessorFileId(const string& path, PreprocessorFileId& fileid)
{
	struct stat info;
	if (stat(path.c_str(), &info) != 0) return false;
	fileid = make_pair(static_cast<unsigned long long>(info.st_dev),
		static_cast<unsigned long long>(info.st_ino));
	return true;
}

namespace
{

string ReadFile(const string& path)
{
	ifstream input(path.c_str(), ios::binary);
	if (!input) throw runtime_error("unable to open source file: " + path);
	string contents;
	char buffer[64 * 1024];
	for (;;)
	{
		input.read(buffer, sizeof(buffer));
		const streamsize count = input.gcount();
		if (count > 0) contents.append(buffer, static_cast<size_t>(count));
		if (input.bad()) throw runtime_error("failed to read source file: " + path);
		if (input.eof()) break;
		if (!input) throw runtime_error("failed to read source file: " + path);
	}
	return contents;
}

pair<string, string> BuildDateAndTime()
{
	const time_t now = time(NULL);
	const tm* local = localtime(&now);
	if (!local) throw runtime_error("unable to read build date and time");
	const char* text = asctime(local);
	if (!text || string(text).size() < 25)
		throw runtime_error("unable to format build date and time");
	return make_pair(string(text + 4, 6) + " " + string(text + 20, 4),
		string(text + 11, 8));
}

struct PostTokenSink : IPreprocessedTokenSink
{
	explicit PostTokenSink(PostTokenStreamWriter& writer)
		: writer_(writer), success(true) {}

	void emit_preprocessed_token(const PreprocessingToken& token)
	{
		if (success) success = writer_.emit(token);
	}

	PostTokenStreamWriter& writer_;
	bool success;
};

} // namespace

int main(int argc, char** argv)
{
	try
	{
		if (argc < 4 || string(argv[1]) != "-o")
			throw runtime_error("usage: preproc -o <outfile> <source> [<source> ...]");
		const string outfile = argv[2];
		const size_t source_count = static_cast<size_t>(argc - 3);
		for (int i = 3; i < argc; ++i)
			if (!string(argv[i]).empty() && argv[i][0] == '-')
				throw runtime_error("unsupported preprocessor option");

		ofstream output_file(outfile.c_str(), ios::out | ios::trunc);
		if (!output_file) throw runtime_error("unable to open output file: " + outfile);
		const pair<string, string> build = BuildDateAndTime();
		output_file << "preproc " << source_count << '\n';
		for (int i = 3; i < argc; ++i)
		{
			const string path = argv[i];
			const string source = ReadFile(path);
			output_file << "sof " << path << '\n';
			PostTokenStreamWriter writer(output_file);
			PostTokenSink sink(writer);
			PreprocessingMetadata metadata;
			PreprocessTranslationUnit(source, path, sink, metadata,
				build.first, build.second);
			if (!sink.success || !writer.finish(true))
				return EXIT_FAILURE;
			if (!output_file) throw runtime_error("failed to write preprocessor output");
		}
		return EXIT_SUCCESS;
	}
	catch (const exception& error)
	{
		cerr << "ERROR: " << error.what() << endl;
		return EXIT_FAILURE;
	}
}
