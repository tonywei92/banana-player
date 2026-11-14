#include "textsource.h"
#include "textdoc.h"
#include "epub.h"
#include <algorithm>

namespace bp
{
	namespace
	{
		// a TXT's blocks are TextDoc's pieces (a line, or SEG bytes of a long one), their ids the pieces' offsets
		class TxtSource : public TextSource
		{
		public:
			bool open(const std::string& path)
			{
				if (doc.open(path)) return true;
				err = doc.error();
				return false;
			}
			uint64_t first() const override { return doc.begin(); }
			uint64_t end() const override { return doc.size(); }
			bool block(uint64_t id, TextBlock& out, uint64_t& next) override
			{
				if (id >= doc.size()) return false;
				TextDoc::Seg s = doc.segFrom((size_t)id);
				out = TextBlock{};
				out.text = doc.text(s);
				next = s.next;
				return true;
			}
			bool before(uint64_t id, uint64_t& prev) override
			{
				TextDoc::Seg s;
				if (!doc.segBefore((size_t)std::min<uint64_t>(id, doc.size()), s)) return false;
				prev = s.from;
				return true;
			}
			double fraction(uint64_t id) const override
			{
				size_t span = doc.size() - doc.begin();
				return span ? (double)(std::min<uint64_t>(id, doc.size()) - doc.begin()) / span : 0;
			}
			uint64_t at(double f) override
			{
				if (doc.size() <= doc.begin()) return doc.begin();
				f = std::clamp(f, 0.0, 1.0);
				size_t off = doc.begin() + (size_t)((doc.size() - doc.begin()) * f);
				if (off >= doc.size())
				{
					// the last piece: the one before the end
					TextDoc::Seg s;
					return doc.segBefore(doc.size(), s) ? s.from : doc.begin();
				}
				return doc.segAt(off).from;
			}
			uint64_t snap(uint64_t id) override
			{
				if (id <= doc.begin()) return doc.begin();
				if (id >= doc.size()) return at(1);
				return doc.segAt((size_t)id).from;
			}
			bool find(const std::string& query, uint64_t from, bool back, uint64_t& found, const Cancel& cancel,
				const Progress& progress) override
			{
				// from the block after this one to the end, then round from the start (back: before it, then from the end)
				size_t here = (size_t)std::clamp<uint64_t>(from, doc.begin(), doc.size());
				size_t after = here < doc.size() ? doc.segFrom(here).next : doc.size();
				size_t at = 0;
				auto prog = [&](double lo, double hi) {
					return Progress([&, lo, hi](double f) { if (progress) progress(lo + (hi - lo) * f); });
				};
				bool hit = !back ? doc.find(query, after, doc.size(), false, at, cancel, prog(0, 0.5)) ||
						(!(cancel && cancel()) && doc.find(query, doc.begin(), after, false, at, cancel, prog(0.5, 1)))
					: doc.find(query, doc.begin(), here, true, at, cancel, prog(0, 0.5)) ||
						(!(cancel && cancel()) && doc.find(query, here, doc.size(), true, at, cancel, prog(0.5, 1)));
				if (!hit) return false;
				found = doc.segAt(at).from;
				return true;
			}
			std::string encoding() const override { return doc.encoding(); }
			void setEncoding(const std::string& c) override { doc.setEncoding(c); }

		private:
			TextDoc doc;
		};
	}

	std::unique_ptr<TextSource> openTextFile(const std::string& path, std::string& error)
	{
		auto s = std::make_unique<TxtSource>();
		if (s->open(path)) return s;
		error = s->error();
		return nullptr;
	}

	bool findInFile(const std::string& path, bool epub, const std::string& charset, const std::string& query, uint64_t from,
		bool back, uint64_t& found, const TextSource::Cancel& cancel, const TextSource::Progress& progress)
	{
		std::string err;
		std::unique_ptr<TextSource> s = epub ? openEpubText(path, err) : openTextFile(path, err);
		if (!s) return false;
		if (!charset.empty()) s->setEncoding(charset);
		return s->find(query, from, back, found, cancel, progress);
	}
}
