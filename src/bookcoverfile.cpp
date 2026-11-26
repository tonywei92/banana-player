#include "bookcoverfile.h"
#include "epub.h"
#include "pagebook.h"

namespace bp
{
	bool findBookCover(const std::string& path, int drawW, BookCover& out)
	{
		out = BookCover();
		std::string low = path.substr(path.rfind('.') == std::string::npos ? path.size() : path.rfind('.'));
		for (char& c : low) c = (char)tolower((unsigned char)c);
		std::vector<uint8_t> b;
		// (an EPUB first: its cover image whether it is a novel or a comic, which the page-book route refuses for a novel)
		if (low == ".epub")
		{
			Epub e;
			if (!e.open(path)) return false;
			std::string name = e.coverImage();
			if (name.empty())
			{
				auto pics = e.pictures();
				if (!pics.empty()) name = pics.front();
			}
			if (name.empty() || !e.read(name, b)) return false;
		}
		else if (isPageBookName(path))
		{
			std::string err;
			auto book = openPageBook(path, err);
			if (!book || book->pages().empty()) return false;
			if (book->rendered())
			{
				out.drawn = book->render(0, drawW, 2000000);
				return out.drawn.ok();
			}
			if (!book->read(book->pages()[0], b)) return false;
		}
		else return false;
		if (b.empty() || b.size() > (size_t)16 << 20) return false;
		out.bytes = std::move(b);
		return true;
	}
}
