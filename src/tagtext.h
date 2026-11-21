// Tag text made into UTF-8 (bpcore). Old files (a RIFF/RMP3 from 1999, ID3v1) carry their words in the maker's code
// page, and the tag readers hand those bytes on as they are.
#pragma once
#include <string>

namespace bp
{
	// Valid UTF-8 stays. Otherwise Shift-JIS (CP932), GBK, then Big5 (our own tables, src/cjk: no iconv), the first that decodes whole and
	//  is mostly CJK or kana; else the bytes as Latin-1. Always valid UTF-8.
	std::string cleanTagText(const std::string& s);
}
