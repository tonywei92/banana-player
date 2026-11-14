#include "pagebook.h"
#include "cbz.h"
#include "epub.h"
#include "manga_names.h"
#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstring>
#include <mutex>
#ifdef BP_ARCHIVE
#include <archive.h>
#include <archive_entry.h>
#endif
#ifdef BP_PDF
#include <fpdfview.h>
#endif

namespace bp
{
	namespace
	{
		constexpr uint64_t kMaxEntry = 64ull << 20;	// as Cbz: a page is tens of MB at most, more is a broken file
		constexpr size_t kMaxEntries = 10000;

		std::string extOf(const std::string& name)
		{
			size_t dot = name.rfind('.'), slash = name.rfind('/');
			if (dot == std::string::npos || dot == 0 || (slash != std::string::npos && dot < slash)) return "";
			std::string e = name.substr(dot + 1);
			for (char& c : e) c = (char)std::tolower((unsigned char)c);
			return e;
		}

		std::string lowered(std::string s)
		{
			for (char& c : s) c = (char)std::tolower((unsigned char)c);
			return s;
		}

		// ---- CBZ: the reader of cbz.h as it was ----
		class ZipBook : public PageBook
		{
		public:
			bool open(const std::string& path)
			{
				if (!z.open(path)) return (err = z.error()), false;
				names = z.pages();
				for (auto& e : z.entries()) all.push_back(e.name);
				return true;
			}
			bool read(const std::string& name, std::vector<uint8_t>& out) override
			{
				if (z.read(name, out)) return true;
				err = z.error();
				return false;
			}

		private:
			Cbz z;
		};

		// ---- EPUB: a comic's pages (each chapter's pictures, in spine order) ----
		class EpubBook : public PageBook
		{
		public:
			bool open(const std::string& path)
			{
				if (!e.open(path)) return (err = e.error()), false;
				names = e.pictures();
				if (names.empty() || !e.pictureBook())
				{
					// a novel: its words are for the text viewer, not pages
					err = "This EPUB is a book of text: open it from Files";
					return false;
				}
				return true;
			}
			bool read(const std::string& name, std::vector<uint8_t>& out) override
			{
				if (e.read(name, out)) return true;
				err = e.error();
				return false;
			}

		private:
			Epub e;
		};

#ifdef BP_ARCHIVE
		// ---- CBR: RAR (4 and 5) through libarchive ----
		// libarchive reads an archive front to back. The listing is one pass of headers (a non-solid archive's data
		// skipped by seeking); a page is reached by going on from where the last read stopped, so reading forward
		// (the usual way) never starts over, and only a page behind the last one opens the archive again.
		class RarBook : public PageBook
		{
		public:
			~RarBook() override { shut(); }
			bool open(const std::string& path)
			{
				file = path;
				if (!reopen()) return false;
				archive_entry* e;
				int r;
				while ((r = archive_read_next_header(a, &e)) == ARCHIVE_OK)
				{
					if (all.size() >= kMaxEntries) return (err = "Too many files in this archive"), false;
					const char* n = archive_entry_pathname(e);
					all.push_back(n ? n : "");
					if (archive_entry_filetype(e) == AE_IFREG && n && isPageImage(n)) names.push_back(n);
					next++;
				}
				if (r != ARCHIVE_EOF)
				{
					err = errorText("This archive can't be read");
					return false;
				}
				shut();
				sortPages(names);
				return true;
			}

			bool read(const std::string& name, std::vector<uint8_t>& out) override
			{
				// the entry's place in the archive (names can repeat in a broken one: the first)
				auto it = std::find(all.begin(), all.end(), name);
				if (it == all.end()) return (err = "No such page in the archive"), false;
				size_t want = (size_t)(it - all.begin());
				if (!a || want < next)
					if (!reopen()) return false;
				archive_entry* e;
				while (next <= want)
				{
					int r = archive_read_next_header(a, &e);
					if (r != ARCHIVE_OK && r != ARCHIVE_WARN)
					{
						err = errorText("The archive ended early");
						shut();
						return false;
					}
					if (next++ < want)
					{
						archive_read_data_skip(a);
						continue;
					}
					if (archive_entry_is_encrypted(e)) return (err = "This page is locked with a password"), false;
					int64_t size = archive_entry_size_is_set(e) ? archive_entry_size(e) : -1;
					if (size > (int64_t)kMaxEntry) return (err = "This page is too big"), false;
					out.clear();
					if (size > 0) out.reserve((size_t)size);
					uint8_t buf[64 * 1024];
					for (;;)
					{
						la_ssize_t got = archive_read_data(a, buf, sizeof buf);
						if (got < 0)
						{
							err = errorText("This page can't be read");
							shut();
							return false;
						}
						if (got == 0) break;
						if (out.size() + (size_t)got > kMaxEntry) return (err = "This page is too big"), false;
						out.insert(out.end(), buf, buf + got);
					}
					return true;
				}
				return (err = "No such page in the archive"), false;
			}

		private:
			bool reopen()
			{
				shut();
				a = archive_read_new();
				archive_read_support_format_rar(a);
				archive_read_support_format_rar5(a);
				next = 0;
				// (a large block: a card reads faster in big pieces)
				if (archive_read_open_filename(a, file.c_str(), 256 * 1024) != ARCHIVE_OK)
				{
					err = errorText("This archive can't be opened");
					shut();
					return false;
				}
				return true;
			}
			void shut()
			{
				if (a) archive_read_free(a);
				a = nullptr;
				next = 0;
			}
			std::string errorText(const char* plain)
			{
				const char* s = a ? archive_error_string(a) : nullptr;
				return s && *s ? std::string(plain) + ": " + s : plain;
			}

			std::string file;
			archive* a = nullptr;
			size_t next = 0;	// the index of the header the next archive_read_next_header gives
		};
#endif

#ifdef BP_PDF
		// pdfium is one library-wide state, not thread-safe: every call under this
		std::mutex& pdfLock()
		{
			static std::mutex m;
			return m;
		}

		class PdfBook : public PageBook
		{
		public:
			~PdfBook() override
			{
				std::lock_guard<std::mutex> l(pdfLock());
				if (doc) FPDF_CloseDocument(doc);
			}
			bool open(const std::string& path)
			{
				std::lock_guard<std::mutex> l(pdfLock());
				static bool inited = false;
				if (!inited)
				{
					FPDF_InitLibrary();
					inited = true;
				}
				doc = FPDF_LoadDocument(path.c_str(), nullptr);
				if (!doc)
				{
					unsigned long e = FPDF_GetLastError();
					err = e == FPDF_ERR_PASSWORD ? "This PDF is locked with a password"
						: e == FPDF_ERR_FORMAT ? "This file isn't a PDF, or it's damaged"
						: e == FPDF_ERR_FILE ? "This PDF can't be opened"
						: "This PDF can't be read";
					return false;
				}
				int n = FPDF_GetPageCount(doc);
				if (n <= 0) return (err = "This PDF has no pages"), false;
				names.reserve((size_t)n);
				for (int i = 1; i <= n; i++) names.push_back(std::to_string(i));
				return true;
			}
			bool read(const std::string&, std::vector<uint8_t>&) override
			{
				err = "A PDF's pages are drawn, not read";
				return false;
			}
			bool rendered() const override { return true; }

			bool pageSize(int index, int& w, int& h) override
			{
				std::lock_guard<std::mutex> l(pdfLock());
				FS_SIZEF s{};
				if (!doc || !FPDF_GetPageSizeByIndexF(doc, index, &s) || s.width <= 0 || s.height <= 0) return false;
				w = std::max(1, (int)std::lround(s.width));
				h = std::max(1, (int)std::lround(s.height));
				return true;
			}

			PageImage render(int index, int targetW, long long maxPixels) override
			{
				PageImage img;
				std::lock_guard<std::mutex> l(pdfLock());
				FPDF_PAGE pg = doc ? FPDF_LoadPage(doc, index) : nullptr;
				if (!pg)
				{
					img.error = "This page can't be read";
					return img;
				}
				double pw = FPDF_GetPageWidthF(pg), ph = FPDF_GetPageHeightF(pg);
				if (pw <= 0 || ph <= 0)
				{
					FPDF_ClosePage(pg);
					img.error = "This page is empty";
					return img;
				}
				img.srcW = (int)std::lround(pw);
				img.srcH = (int)std::lround(ph);
				// (its own size: 150 dpi, a page's points at 72)
				double w = targetW > 0 ? std::min(targetW, 2048) : pw * 150 / 72;
				double h = w * ph / pw;
				long long cap = maxPixels > 0 ? maxPixels : 16LL * 1000 * 1000;
				if (w * h > (double)cap)
				{
					double k = std::sqrt((double)cap / (w * h));
					w *= k;
					h *= k;
				}
				img.w = std::max(1, (int)w);
				img.h = std::max(1, (int)std::lround(h));
				FPDF_BITMAP bmp = FPDFBitmap_Create(img.w, img.h, 0);
				if (!bmp)
				{
					FPDF_ClosePage(pg);
					img.w = img.h = 0;
					img.error = "Not enough memory for this page";
					return img;
				}
				FPDFBitmap_FillRect(bmp, 0, 0, img.w, img.h, 0xFFFFFFFF);
				// RGBA's byte order straight from pdfium (no swizzle pass), annotations drawn (form fields, notes)
				FPDF_RenderPageBitmap(bmp, pg, 0, 0, img.w, img.h, 0, FPDF_ANNOT | FPDF_REVERSE_BYTE_ORDER);
				const uint8_t* src = (const uint8_t*)FPDFBitmap_GetBuffer(bmp);
				int stride = FPDFBitmap_GetStride(bmp);
				img.rgba.resize((size_t)img.w * img.h * 4);
				for (int y = 0; y < img.h; y++)
				{
					uint8_t* d = img.rgba.data() + (size_t)y * img.w * 4;
					std::memcpy(d, src + (size_t)y * stride, (size_t)img.w * 4);
					for (int x = 0; x < img.w; x++) d[x * 4 + 3] = 255;	// (no alpha: BGRx's x is undefined)
				}
				FPDFBitmap_Destroy(bmp);
				FPDF_ClosePage(pg);
				return img;
			}

		private:
			FPDF_DOCUMENT doc = nullptr;
		};
#endif
	}

	PageImage PageBook::render(int, int, long long)
	{
		PageImage img;
		img.error = "Not a drawn page";
		return img;
	}

	bool PageBook::pageSize(int, int&, int&)
	{
		return false;
	}

	bool PageBook::comicInfo(std::string& xml)
	{
		for (const std::string& n : all)
			if (lowered(n) == "comicinfo.xml")
			{
				std::vector<uint8_t> raw;
				if (!read(n, raw)) return false;
				xml.assign(raw.begin(), raw.end());
				return true;
			}
		return false;
	}

	bool isPdfName(const std::string& name)
	{
#ifdef BP_PDF
		return extOf(name) == "pdf";
#else
		(void)name;
		return false;
#endif
	}

	bool isComicName(const std::string& name)
	{
		std::string e = extOf(name);
#ifdef BP_ARCHIVE
		if (e == "cbr") return true;
#endif
		return e == "cbz";
	}

	bool isDocumentName(const std::string& name)
	{
		std::string e = extOf(name);
		return e == "txt" || e == "epub" || isPdfName(name);
	}

	bool isPageBookName(const std::string& name)
	{
		std::string e = extOf(name);
		if (e == "cbz" || e == "zip" || e == "epub") return true;
#ifdef BP_ARCHIVE
		if (e == "cbr" || e == "rar") return true;
#endif
		return isPdfName(name);
	}

	std::string stripBookExt(const std::string& name)
	{
		if (!isPageBookName(name)) return name;
		return name.substr(0, name.rfind('.'));
	}

	std::unique_ptr<PageBook> openPageBook(const std::string& path, std::string& error)
	{
		std::string e = extOf(path);
		if (e == "cbz" || e == "zip")
		{
			auto b = std::make_unique<ZipBook>();
			if (b->open(path)) return b;
			error = b->error();
			return nullptr;
		}
		if (e == "epub")
		{
			auto b = std::make_unique<EpubBook>();
			if (b->open(path)) return b;
			error = b->error();
			return nullptr;
		}
#ifdef BP_ARCHIVE
		if (e == "cbr" || e == "rar")
		{
			auto b = std::make_unique<RarBook>();
			if (b->open(path)) return b;
			error = b->error();
			return nullptr;
		}
#endif
#ifdef BP_PDF
		if (e == "pdf")
		{
			auto b = std::make_unique<PdfBook>();
			if (b->open(path)) return b;
			error = b->error();
			return nullptr;
		}
#endif
		error = "This kind of file can't be read";
		return nullptr;
	}
}
