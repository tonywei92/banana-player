#include "epub.h"
#include "htmldoc.h"
#include "manga_names.h"
#include <algorithm>
#include <cctype>
#include <cstring>
#include <functional>
#include <map>

namespace bp
{
	namespace
	{
		std::string lower(std::string s)
		{
			for (char& c : s) c = (char)std::tolower((unsigned char)c);
			return s;
		}

		std::string dirOf(const std::string& name)
		{
			size_t s = name.rfind('/');
			return s == std::string::npos ? "" : name.substr(0, s + 1);
		}

		int hexv(char c)
		{
			return c >= '0' && c <= '9' ? c - '0' : c >= 'a' && c <= 'f' ? c - 'a' + 10 : c >= 'A' && c <= 'F' ? c - 'A' + 10 : -1;
		}

		bool isWs(char c) { return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\f'; }

		// the text of one element of an XML document as written ("<dc:title ...>Name</dc:title>"): the package's
		// own tags have prefixes an HTML parser keeps whole, so a plain look is surer
		std::string xmlText(const std::string& xml, const std::string& tag)
		{
			size_t a = xml.find("<" + tag);
			if (a == std::string::npos) return "";
			a = xml.find('>', a);
			size_t b = a == std::string::npos ? a : xml.find("</" + tag, a);
			if (b == std::string::npos) return "";
			HtmlDoc d;
			d.parse("<p>" + xml.substr(a + 1, b - a - 1) + "</p>");	// (entities resolved, whitespace tidied)
			HtmlNode p = d.selectFirst("p");
			return p.valid() ? p.text() : "";
		}
	}

	std::string epubResolve(const std::string& base, const std::string& href)
	{
		std::string h = href.substr(0, href.find('#'));
		// %-escapes ("My%20Book.xhtml")
		std::string u;
		for (size_t i = 0; i < h.size(); i++)
			if (h[i] == '%' && i + 2 < h.size() && hexv(h[i + 1]) >= 0 && hexv(h[i + 2]) >= 0)
			{
				u += (char)(hexv(h[i + 1]) * 16 + hexv(h[i + 2]));
				i += 2;
			}
			else u += h[i];
		if (u.empty()) return "";
		std::string path = u[0] == '/' ? u.substr(1) : dirOf(base) + u;
		// "." and ".." folded away
		std::vector<std::string> parts;
		size_t at = 0;
		while (at <= path.size())
		{
			size_t s = path.find('/', at);
			std::string part = path.substr(at, s == std::string::npos ? std::string::npos : s - at);
			if (part == "..") { if (!parts.empty()) parts.pop_back(); }
			else if (!part.empty() && part != ".") parts.push_back(part);
			if (s == std::string::npos) break;
			at = s + 1;
		}
		std::string out;
		for (auto& p : parts) out += (out.empty() ? "" : "/") + p;
		return out;
	}

	bool Epub::open(const std::string& path)
	{
		if (!zip.open(path)) return (err = zip.error()), false;
		std::string container;
		if (!readText("META-INF/container.xml", container)) return (err = "This EPUB has no container (it may be damaged)"), false;
		HtmlDoc c;
		c.parse(container);
		HtmlNode root = c.selectFirst("rootfile");
		opf = root.valid() ? epubResolve("", root.attr("full-path")) : "";
		std::string pkg;
		if (opf.empty() || !readText(opf, pkg)) return (err = "This EPUB has no package (it may be damaged)"), false;
		opfDir = dirOf(opf);
		bookTitle = xmlText(pkg, "dc:title");
		if (pkg.find("pre-paginated") != std::string::npos) fixedLayout = true;
		HtmlDoc d;
		d.parse(pkg);
		// the manifest: id -> the file, its type and properties
		struct Item { std::string href, type, props; };
		std::map<std::string, Item> items;
		for (const HtmlNode& it : d.select("item"))
		{
			Item i{epubResolve(opf, it.attr("href")), lower(it.attr("media-type")), it.attr("properties")};
			if (i.props.find("nav") != std::string::npos && i.type.find("html") != std::string::npos) navDoc = i.href;
			if (i.type == "application/x-dtbncx+xml") ncxDoc = i.href;
			items[it.attr("id")] = i;
		}
		HtmlNode spineNode = d.selectFirst("spine");
		if (navDoc.empty() && ncxDoc.empty() && spineNode.valid())
		{
			auto t = items.find(spineNode.attr("toc"));
			if (t != items.end()) ncxDoc = t->second.href;
		}
		for (const HtmlNode& ref : d.select("itemref"))
		{
			auto it = items.find(ref.attr("idref"));
			if (it == items.end() || it->second.href.empty()) continue;
			docs.push_back(it->second.href);
		}
		if (docs.empty()) return (err = "This EPUB has no chapters"), false;
		return true;
	}

	bool Epub::read(const std::string& name, std::vector<uint8_t>& out)
	{
		if (zip.read(name, out)) return true;
		err = zip.error();
		return false;
	}

	bool Epub::readText(const std::string& name, std::string& out)
	{
		std::vector<uint8_t> b;
		if (!read(name, b)) return false;
		out.assign(b.begin(), b.end());
		return true;
	}

	uint64_t Epub::sizeOf(const std::string& name) const
	{
		for (const CbzEntry& e : zip.entries())
			if (e.name == name) return e.size;
		return 0;
	}

	std::vector<std::string> Epub::pictures()
	{
		std::vector<std::string> out;
		for (const std::string& doc : docs)
		{
			if (isPageImage(doc))
			{
				out.push_back(doc);
				continue;
			}
			std::string x;
			if (!readText(doc, x)) continue;
			HtmlDoc d;
			d.parse(x);
			for (const HtmlNode& n : d.select("img, image"))
			{
				std::string src = n.tag() == "img" ? n.attr("src") : n.attr("xlink:href");
				if (src.empty()) src = n.attr("href");
				std::string r = epubResolve(doc, src);
				if (!r.empty() && isPageImage(r) && std::find(out.begin(), out.end(), r) == out.end()) out.push_back(r);
			}
		}
		return out;
	}

	bool Epub::pictureBook()
	{
		if (picturesKnown >= 0) return picturesKnown == 1;
		// a fixed layout is pages; otherwise a few chapters looked at: pictures and next to no words are a comic
		bool pics = fixedLayout;
		if (!pics)
		{
			// (a chapter that is a picture and a few words at most is a comic's page; most of them, a comic)
			size_t pages = 0, looked = 0;
			for (size_t i = 0; i < docs.size() && looked < 8; i++)
			{
				std::string x;
				if (!readText(docs[i], x)) continue;
				looked++;
				HtmlDoc d;
				d.parse(x);
				HtmlNode body = d.selectFirst("body");
				size_t words = body.valid() ? body.text().size() : 0;
				if (!d.select("img, image").empty() && words < 80) pages++;
			}
			pics = looked > 0 && pages * 5 >= looked * 4;
		}
		picturesKnown = pics ? 1 : 0;
		return pics;
	}

	std::vector<Epub::Entry> Epub::toc()
	{
		std::vector<Entry> out;
		std::string x;
		if (!navDoc.empty() && readText(navDoc, x))
		{
			HtmlDoc d;
			d.parse(x);
			HtmlNode nav;
			for (const HtmlNode& n : d.select("nav"))
				if (n.attr("epub:type").find("toc") != std::string::npos || n.attr("role") == "doc-toc") nav = n;
			if (!nav.valid()) nav = d.selectFirst("nav");
			// each list item's link, its nested list one deeper
			std::function<void(const HtmlNode&, int)> walk = [&](const HtmlNode& ol, int depth) {
				for (const HtmlNode& li : ol.children())
				{
					if (li.tag() != "li") continue;
					HtmlNode a = li.selectFirst("a");
					if (a.valid() && !a.text().empty()) out.push_back({a.text(), epubResolve(navDoc, a.attr("href")), depth});
					for (const HtmlNode& sub : li.children())
						if (sub.tag() == "ol" || sub.tag() == "ul") walk(sub, depth + 1);
				}
			};
			if (nav.valid())
				for (const HtmlNode& ol : nav.children())
					if (ol.tag() == "ol" || ol.tag() == "ul") walk(ol, 0);
			if (!out.empty()) return out;
		}
		if (!ncxDoc.empty() && readText(ncxDoc, x))
		{
			HtmlDoc d;
			d.parse(x);
			std::function<void(const HtmlNode&, int)> walk = [&](const HtmlNode& parent, int depth) {
				for (const HtmlNode& np : parent.children())
				{
					if (np.tag() != "navpoint") continue;
					HtmlNode label = np.selectFirst("navlabel");
					HtmlNode content = np.selectFirst("content");
					if (label.valid() && content.valid()) out.push_back({label.text(), epubResolve(ncxDoc, content.attr("src")), depth});
					walk(np, depth + 1);
				}
			};
			HtmlNode map = d.selectFirst("navmap");
			if (map.valid()) walk(map, 0);
		}
		return out;
	}

	// ---- a chapter into blocks ----

	namespace
	{
		struct BlockMaker
		{
			std::vector<TextBlock> out;
			std::string doc;
			TextBlock cur;
			bool pendingSpace = false;

			void flush()
			{
				// (the text's end trimmed; an empty block isn't one)
				while (!cur.text.empty() && cur.text.back() == ' ') cur.text.pop_back();
				for (auto& s : cur.spans) s.to = std::min<uint32_t>(s.to, (uint32_t)cur.text.size());
				cur.spans.erase(std::remove_if(cur.spans.begin(), cur.spans.end(), [](const TextBlock::Span& s) { return s.from >= s.to; }), cur.spans.end());
				if (!cur.text.empty()) out.push_back(cur);
				TextBlock::Kind k = cur.kind;
				cur = TextBlock{};
				cur.kind = k;
				pendingSpace = false;
			}

			void text(const std::string& t, uint8_t style, bool pre)
			{
				size_t from = std::string::npos;	// where this run's own characters start (not a space before them)
				for (char ch : t)
				{
					if (pre)
					{
						if (ch == '\n')
						{
							if (style && from != std::string::npos) cur.spans.push_back({(uint32_t)from, (uint32_t)cur.text.size(), style});
							flush();
							from = std::string::npos;
							continue;
						}
						if (ch == '\r') continue;
						if (from == std::string::npos) from = cur.text.size();
						if (ch == '\t') { cur.text += "    "; continue; }
						cur.text += ch;
						continue;
					}
					if (isWs(ch))
					{
						pendingSpace = !cur.text.empty();
						continue;
					}
					if (pendingSpace) cur.text += ' ';
					pendingSpace = false;
					if (from == std::string::npos) from = cur.text.size();
					cur.text += ch;
				}
				if (style && from != std::string::npos && cur.text.size() > from)
					cur.spans.push_back({(uint32_t)from, (uint32_t)cur.text.size(), style});
			}

			static TextBlock::Kind kindOf(const std::string& tag, TextBlock::Kind in)
			{
				if (tag == "h1") return TextBlock::H1;
				if (tag == "h2") return TextBlock::H2;
				if (tag == "h3" || tag == "h4" || tag == "h5" || tag == "h6") return TextBlock::H3;
				if (tag == "blockquote") return TextBlock::Quote;
				if (tag == "pre") return TextBlock::Pre;
				return in;
			}

			static bool isBlock(const std::string& t)
			{
				static const char* B[] = {"p", "div", "section", "article", "blockquote", "li", "h1", "h2", "h3", "h4", "h5", "h6",
					"pre", "figure", "figcaption", "tr", "dt", "dd", "header", "footer", "aside", "ul", "ol", "table", "main", "dl",
					"body", "center", "address"};
				for (const char* b : B)
					if (t == b) return true;
				return false;
			}

			void walk(const HtmlNode& n, TextBlock::Kind kind, uint8_t style, bool pre)
			{
				for (const HtmlNode& c : n.children())
				{
					if (c.isText())
					{
						if (cur.text.empty() && cur.kind != kind) cur.kind = kind;
						text(c.rawText(), style, pre);
						continue;
					}
					std::string t = c.tag();
					if (t.empty() || t == "script" || t == "style" || t == "head" || t == "title") continue;
					if (t == "br")
					{
						flush();
						continue;
					}
					if (t == "hr")
					{
						flush();
						TextBlock r;
						r.kind = TextBlock::Rule;
						r.text = " ";
						out.push_back(r);
						continue;
					}
					if (t == "img" || t == "image")
					{
						std::string src = t == "img" ? c.attr("src") : c.attr("xlink:href");
						if (src.empty()) src = c.attr("href");
						std::string r = epubResolve(doc, src);
						if (r.empty()) continue;
						flush();
						TextBlock b;
						b.kind = TextBlock::Image;
						b.image = r;
						b.text = c.attr("alt");
						out.push_back(b);
						continue;
					}
					uint8_t st = style;
					if (t == "b" || t == "strong" || t == "th") st |= TextBlock::Bold;
					if (t == "i" || t == "em" || t == "cite" || t == "var" || t == "dfn") st |= TextBlock::Italic;
					if (isBlock(t))
					{
						flush();
						TextBlock::Kind k = kindOf(t, kind);
						cur.kind = k;
						if (t == "li") text("\xE2\x80\xA2 ", st, false);	// a bullet before the item
						walk(c, k, st | (k == TextBlock::H1 || k == TextBlock::H2 || k == TextBlock::H3 ? 0 : 0), pre || t == "pre");
						flush();
						cur.kind = kind;
						continue;
					}
					walk(c, kind, st, pre);
				}
			}
		};
	}

	std::vector<TextBlock> epubBlocks(const std::string& xhtml, const std::string& docName)
	{
		HtmlDoc d;
		if (!d.parse(xhtml)) return {};
		BlockMaker m;
		m.doc = docName;
		HtmlNode body = d.selectFirst("body");
		if (!body.valid()) return {};
		m.walk(body, TextBlock::Para, 0, false);
		m.flush();
		return std::move(m.out);
	}

	// ---- the text viewer's source ----

	namespace
	{
		class EpubText : public TextSource
		{
		public:
			bool open(const std::string& path)
			{
				if (!book.open(path)) return (err = book.error()), false;
				uint64_t at = 0;
				for (const std::string& d : book.spine())
				{
					cum.push_back(at);
					uint64_t s = std::max<uint64_t>(1, book.sizeOf(d));
					sizes.push_back(s);
					at += s;
				}
				total = std::max<uint64_t>(1, at);
				if (first() >= end()) return (err = "This EPUB has no text"), false;
				return true;
			}

			uint64_t first() const override { return firstFrom(0); }
			uint64_t end() const override { return (uint64_t)book.spine().size() << 32; }

			bool block(uint64_t id, TextBlock& out, uint64_t& next) override
			{
				size_t c = (size_t)(id >> 32), b = (size_t)(id & 0xffffffffu);
				const auto* blocks = chapter(c);
				if (!blocks || b >= blocks->size()) return false;
				out = (*blocks)[b];
				next = b + 1 < blocks->size() ? id + 1 : firstFrom(c + 1);
				return true;
			}

			bool before(uint64_t id, uint64_t& prev) override
			{
				size_t c = (size_t)(id >> 32), b = (size_t)(id & 0xffffffffu);
				if (b > 0 && c < book.spine().size())
				{
					prev = id - 1;
					return true;
				}
				for (size_t k = c; k-- > 0;)
				{
					const auto* blocks = chapter(k);
					if (blocks && !blocks->empty())
					{
						prev = ((uint64_t)k << 32) | (blocks->size() - 1);
						return true;
					}
				}
				return false;
			}

			double fraction(uint64_t id) const override
			{
				size_t c = (size_t)(id >> 32), b = (size_t)(id & 0xffffffffu);
				if (c >= cum.size()) return 1;
				auto it = cache.find(c);
				double in = it != cache.end() && !it->second.empty() ? (double)b / it->second.size() : 0;
				return (cum[c] + sizes[c] * in) / total;
			}

			uint64_t at(double f) override
			{
				f = std::clamp(f, 0.0, 1.0);
				uint64_t want = (uint64_t)(f * total);
				size_t c = 0;
				while (c + 1 < cum.size() && cum[c + 1] <= want) c++;
				const auto* blocks = chapter(c);
				if (!blocks || blocks->empty()) return snap((uint64_t)c << 32);
				size_t b = std::min(blocks->size() - 1, (size_t)((double)(want - cum[c]) / sizes[c] * blocks->size()));
				return ((uint64_t)c << 32) | b;
			}

			uint64_t snap(uint64_t id) override
			{
				size_t c = (size_t)(id >> 32), b = (size_t)(id & 0xffffffffu);
				const auto* blocks = chapter(c);
				if (blocks && b < blocks->size()) return id;
				uint64_t f = firstFrom(c + 1);
				if (f < end()) return f;
				uint64_t p;
				return before(end(), p) ? p : first();
			}

			bool image(const std::string& name, std::vector<uint8_t>& out) override { return book.read(name, out); }

			std::vector<Chapter> contents() override
			{
				std::vector<Chapter> out;
				for (const Epub::Entry& e : book.toc())
				{
					auto it = std::find(book.spine().begin(), book.spine().end(), e.doc);
					if (it == book.spine().end()) continue;
					out.push_back({e.title, (uint64_t)(it - book.spine().begin()) << 32, e.depth});
				}
				return out;
			}

			bool find(const std::string& query, uint64_t from, bool back, uint64_t& found, const Cancel& cancel,
				const Progress& progress) override
			{
				// ASCII letters in either case: the query and each block lowered alike
				auto lower = [](std::string t) {
					for (char& c : t) if (c >= 'A' && c <= 'Z') c = (char)(c + 32);
					return t;
				};
				std::string q = lower(query);
				if (q.empty()) return false;
				size_t chapters = book.spine().size();
				if (!chapters) return false;
				size_t c0 = std::min((size_t)(from >> 32), chapters - 1), b0 = (size_t)(from & 0xffffffffu);
				// every chapter once, from this one on (back: from this one down), its blocks after (before) this one
				for (size_t k = 0; k <= chapters; k++)
				{
					if (cancel && cancel()) return false;
					if (progress) progress((double)k / (chapters + 1));
					size_t c = back ? (c0 + chapters - k % chapters) % chapters : (c0 + k) % chapters;
					const auto* blocks = chapter(c);
					if (!blocks || blocks->empty()) continue;
					int n = (int)blocks->size();
					bool first = k == 0, last = k == chapters;	// (the start chapter, and back to it round the end)
					if (!back)
						for (int b = first ? (int)b0 + 1 : 0; b < n && (!last || b <= (int)b0); b++)
						{
							if (lower((*blocks)[b].text).find(q) != std::string::npos)
							{
								found = ((uint64_t)c << 32) | (uint64_t)b;
								return true;
							}
						}
					else
						for (int b = first ? (int)b0 - 1 : n - 1; b >= 0 && (!last || b >= (int)b0); b--)
						{
							if (lower((*blocks)[b].text).find(q) != std::string::npos)
							{
								found = ((uint64_t)c << 32) | (uint64_t)b;
								return true;
							}
						}
				}
				return false;
			}

			std::string title() const override { return book.title(); }
			bool paragraphs() const override { return true; }

		private:
			// a chapter's blocks, read the first time (the last few are kept: a scroll back and forth over a chapter's
			// edge doesn't read it again)
			const std::vector<TextBlock>* chapter(size_t c) const
			{
				if (c >= book.spine().size()) return nullptr;
				auto it = cache.find(c);
				if (it != cache.end())
				{
					lru.erase(std::find(lru.begin(), lru.end(), c));
					lru.push_back(c);
					return &it->second;
				}
				std::string x;
				std::vector<TextBlock> blocks;
				if (const_cast<Epub&>(book).readText(book.spine()[c], x)) blocks = epubBlocks(x, book.spine()[c]);
				if (lru.size() >= 6)
				{
					cache.erase(lru.front());
					lru.erase(lru.begin());
				}
				lru.push_back(c);
				return &(cache[c] = std::move(blocks));
			}

			uint64_t firstFrom(size_t c) const
			{
				for (; c < book.spine().size(); c++)
				{
					const auto* blocks = chapter(c);
					if (blocks && !blocks->empty()) return (uint64_t)c << 32;
				}
				return end();
			}

			mutable Epub book;
			std::vector<uint64_t> cum, sizes;
			uint64_t total = 1;
			mutable std::map<size_t, std::vector<TextBlock>> cache;
			mutable std::vector<size_t> lru;
		};
	}

	std::unique_ptr<TextSource> openEpubText(const std::string& path, std::string& error)
	{
		auto s = std::make_unique<EpubText>();
		if (s->open(path)) return s;
		error = s->error();
		return nullptr;
	}
}
