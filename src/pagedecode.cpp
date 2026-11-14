#include "pagedecode.h"
#include "pagebook.h"
#include <algorithm>
#include <climits>
#include <cmath>
#include <cstring>
#include <fstream>
#include <memory>
#include <new>
#include <csetjmp>
#include <cstdio>
#include <jpeglib.h>
#include <libpng16/png.h>	// (by its folder: "png.h" is this project's own writer)

extern "C"
{
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/error.h>
#include <libswscale/swscale.h>
}

namespace bp
{
	namespace
	{
		constexpr long long kMaxFile = 64LL * 1024 * 1024;
		constexpr int kMaxSide = 16384;
		constexpr long long kMaxPixels = 100LL * 1000 * 1000;

		enum class Kind
		{
			Unknown,
			Jpeg,
			Png,
			Gif,
			Bmp,
			Webp,
			Avif
		};

		PageImage fail(const char* why)
		{
			PageImage p;
			p.error = why;
			return p;
		}

		bool avifBrand(const uint8_t* d, size_t n)
		{
			if (n < 12 || std::memcmp(d + 4, "ftyp", 4) != 0) return false;
			if (std::memcmp(d + 8, "avif", 4) == 0 || std::memcmp(d + 8, "avis", 4) == 0) return true;
			// a file whose major brand is HEIF but that lists AVIF among its compatible brands is an AVIF still too;
			// the box size (and the first 64 bytes) bounds the scan
			size_t box = ((size_t)d[0] << 24) | ((size_t)d[1] << 16) | ((size_t)d[2] << 8) | (size_t)d[3];
			if (box < 16 || box > n) box = std::min<size_t>(n, 64);
			for (size_t at = 16; at + 4 <= box; at += 4)
				if (std::memcmp(d + at, "avif", 4) == 0 || std::memcmp(d + at, "avis", 4) == 0) return true;
			return false;
		}

		// the format from the bytes themselves, never a name: a page from a server or an archive has no extension
		Kind kindOf(const uint8_t* d, size_t n)
		{
			if (n >= 3 && d[0] == 0xFF && d[1] == 0xD8 && d[2] == 0xFF) return Kind::Jpeg;
			if (n >= 8 && d[0] == 0x89 && d[1] == 'P' && d[2] == 'N' && d[3] == 'G' && d[4] == 0x0D && d[5] == 0x0A &&
			    d[6] == 0x1A && d[7] == 0x0A)
				return Kind::Png;
			if (n >= 4 && std::memcmp(d, "GIF8", 4) == 0) return Kind::Gif;
			if (n >= 2 && d[0] == 'B' && d[1] == 'M') return Kind::Bmp;
			if (n >= 12 && std::memcmp(d, "RIFF", 4) == 0 && std::memcmp(d + 8, "WEBP", 4) == 0) return Kind::Webp;
			if (avifBrand(d, n)) return Kind::Avif;
			return Kind::Unknown;
		}

		AVCodecID codecId(Kind k)
		{
			switch (k)
			{
			case Kind::Jpeg: return AV_CODEC_ID_MJPEG;
			case Kind::Png: return AV_CODEC_ID_PNG;
			case Kind::Gif: return AV_CODEC_ID_GIF;
			case Kind::Bmp: return AV_CODEC_ID_BMP;
			case Kind::Webp: return AV_CODEC_ID_WEBP;
			default: return AV_CODEC_ID_NONE;
			}
		}

		// an image decoder can return a half picture for a cut file; the missing end marker is the cheap proof
		bool jpegEnded(const uint8_t* d, size_t n)
		{
			for (size_t i = 0; i + 1 < n; i++)
				if (d[i] == 0xFF && d[i + 1] == 0xD9) return true;
			return false;
		}

		struct CodecCtxFree
		{
			void operator()(AVCodecContext* p) const { avcodec_free_context(&p); }
		};
		struct FrameFree
		{
			void operator()(AVFrame* p) const { av_frame_free(&p); }
		};
		struct PacketFree
		{
			void operator()(AVPacket* p) const { av_packet_free(&p); }
		};
		struct SwsFree
		{
			void operator()(SwsContext* p) const { sws_freeContext(p); }
		};
		struct InputFree
		{
			void operator()(AVFormatContext* p) const { avformat_close_input(&p); }
		};
		struct IoFree
		{
			// (the context doesn't own its buffer: FFmpeg wants it freed first, else every AVIF decode leaked the file's size)
			void operator()(AVIOContext* p) const
			{
				if (p) av_freep(&p->buffer);
				avio_context_free(&p);
			}
		};

		using UniqueCodec = std::unique_ptr<AVCodecContext, CodecCtxFree>;
		using UniqueFrame = std::unique_ptr<AVFrame, FrameFree>;
		using UniquePacket = std::unique_ptr<AVPacket, PacketFree>;
		using UniqueSws = std::unique_ptr<SwsContext, SwsFree>;
		using UniqueInput = std::unique_ptr<AVFormatContext, InputFree>;
		using UniqueIo = std::unique_ptr<AVIOContext, IoFree>;

		bool corrupt(const AVFrame* f)
		{
			return f->decode_error_flags != 0 || (f->flags & AV_FRAME_FLAG_CORRUPT);
		}

		// one whole image as one packet: the still decoders take the file's bytes and give their first frame
		bool decodeStill(AVCodecContext* cc, const uint8_t* data, size_t size, AVFrame* out, std::string& error)
		{
			if (size > (size_t)INT_MAX)
			{
				error = "image too large";
				return false;
			}
			UniquePacket pkt(av_packet_alloc());
			if (!pkt)
			{
				error = "out of memory";
				return false;
			}
			if (av_new_packet(pkt.get(), (int)size) < 0)
			{
				error = "out of memory";
				return false;
			}
			std::memcpy(pkt->data, data, size);
			int r = avcodec_send_packet(cc, pkt.get());
			if (r < 0 && r != AVERROR(EAGAIN))
			{
				error = "decoder rejected the image";
				return false;
			}
			bool flushed = false;
			for (;;)
			{
				r = avcodec_receive_frame(cc, out);
				if (r == 0)
				{
					if (corrupt(out))
					{
						error = "corrupt image";
						return false;
					}
					return true;
				}
				if (r == AVERROR(EAGAIN))
				{
					if (flushed)
					{
						error = "decoder returned no frame";
						return false;
					}
					int f = avcodec_send_packet(cc, nullptr);
					flushed = true;
					if (f < 0 && f != AVERROR_EOF)
					{
						error = "decoder failed";
						return false;
					}
					continue;
				}
				if (r == AVERROR_EOF)
				{
					error = "decoder returned no frame";
					return false;
				}
				error = "decoder failed";
				return false;
			}
		}

		struct MemoryReader
		{
			const uint8_t* data;
			size_t size;
			size_t pos;
		};

		int readMemory(void* opaque, uint8_t* buf, int bufSize)
		{
			auto* r = static_cast<MemoryReader*>(opaque);
			if (r->pos >= r->size) return AVERROR_EOF;
			int n = (int)std::min<size_t>(r->size - r->pos, (size_t)bufSize);
			std::memcpy(buf, r->data + r->pos, (size_t)n);
			r->pos += (size_t)n;
			return n;
		}

		// AVIF is an AV1 still inside an ISOBMFF box: libavformat reads the container, the AV1 decoder the picture
		bool decodeAvif(const uint8_t* data, size_t size, AVFrame* out, std::string& error)
		{
			const AVCodec* codec = avcodec_find_decoder(AV_CODEC_ID_AV1);
			if (!codec)
			{
				error = "no AVIF decoder";
				return false;
			}
			const AVInputFormat* input = av_find_input_format("avif");
			if (!input)
			{
				error = "AVIF not supported";
				return false;
			}
			if (size > (size_t)INT_MAX)
			{
				error = "image too large";
				return false;
			}

			unsigned char* buf = (unsigned char*)av_malloc(size);
			if (!buf)
			{
				error = "out of memory";
				return false;
			}
			std::memcpy(buf, data, size);
			MemoryReader reader{data, size, 0};
			AVIOContext* io = avio_alloc_context(buf, (int)size, 0, &reader, readMemory, nullptr, nullptr);
			if (!io)
			{
				av_free(buf);
				error = "out of memory";
				return false;
			}
			UniqueIo ioGuard(io);

			AVFormatContext* f = avformat_alloc_context();
			if (!f)
			{
				error = "out of memory";
				return false;
			}
			f->pb = io;
			f->flags |= AVFMT_FLAG_CUSTOM_IO;
			if (avformat_open_input(&f, nullptr, input, nullptr) < 0)
			{
				// avformat_open_input frees f when it fails; the container is bad, not the decoder
				error = "AVIF can't be read";
				return false;
			}
			UniqueInput inputGuard(f);

			if (avformat_find_stream_info(f, nullptr) < 0)
			{
				error = "AVIF can't be read";
				return false;
			}
			int index = av_find_best_stream(f, AVMEDIA_TYPE_VIDEO, -1, -1, &codec, 0);
			if (index < 0 || !codec)
			{
				error = "AVIF has no picture";
				return false;
			}

			UniqueCodec cc(avcodec_alloc_context3(codec));
			if (!cc)
			{
				error = "out of memory";
				return false;
			}
			if (avcodec_parameters_to_context(cc.get(), f->streams[index]->codecpar) < 0)
			{
				error = "AVIF codec is unsupported";
				return false;
			}
			cc->thread_count = 1;
			cc->thread_type = 0;
			if (avcodec_open2(cc.get(), codec, nullptr) < 0)
			{
				error = "can't open the AVIF decoder";
				return false;
			}

			UniquePacket pkt(av_packet_alloc());
			if (!pkt)
			{
				error = "out of memory";
				return false;
			}
			bool ended = false;
			for (;;)
			{
				int r = avcodec_receive_frame(cc.get(), out);
				if (r == 0)
				{
					if (corrupt(out))
					{
						error = "corrupt image";
						return false;
					}
					return true;
				}
				if (r == AVERROR_EOF)
				{
					error = "decoder returned no frame";
					return false;
				}
				if (r != AVERROR(EAGAIN))
				{
					error = "decoder failed";
					return false;
				}
				if (ended)
				{
					error = "decoder returned no frame";
					return false;
				}
				int read = av_read_frame(f, pkt.get());
				if (read < 0)
				{
					ended = true;
					int s = avcodec_send_packet(cc.get(), nullptr);
					if (s < 0 && s != AVERROR_EOF)
					{
						error = "decoder failed";
						return false;
					}
					continue;
				}
				if (pkt->stream_index != index)
				{
					av_packet_unref(pkt.get());
					continue;
				}
				int s = avcodec_send_packet(cc.get(), pkt.get());
				av_packet_unref(pkt.get());
				if (s < 0 && s != AVERROR(EAGAIN))
				{
					error = "decoder failed";
					return false;
				}
			}
		}

		// The decoded frame's own RGBA rows, made from a palette when the decoder leaves the picture indexed (GIF, a
		// paletted PNG or BMP): libswscale's palette path isn't a scaler, and the palette's alpha must survive.
		bool paletteRgba(const AVFrame* f, std::vector<uint8_t>& dst, std::string& error)
		{
			if (!f->data[0] || !f->data[1])
			{
				error = "image has no palette";
				return false;
			}
			try
			{
				dst.resize((size_t)f->width * (size_t)f->height * 4);
			}
			catch (const std::bad_alloc&)
			{
				error = "out of memory";
				return false;
			}
			for (int y = 0; y < f->height; y++)
			{
				const uint8_t* src = f->data[0] + (ptrdiff_t)y * f->linesize[0];
				uint8_t* row = dst.data() + (size_t)y * (size_t)f->width * 4;
				for (int x = 0; x < f->width; x++)
				{
					uint32_t p;
					std::memcpy(&p, f->data[1] + (size_t)src[x] * 4, sizeof p);
					row[x * 4 + 0] = (uint8_t)(p >> 16);
					row[x * 4 + 1] = (uint8_t)(p >> 8);
					row[x * 4 + 2] = (uint8_t)p;
					row[x * 4 + 3] = (uint8_t)(p >> 24);
				}
			}
			return true;
		}

		PageImage scaled(const AVFrame* f, int targetW, int maxH, long long maxPixels)
		{
			PageImage out;
			out.srcW = f->width;
			out.srcH = f->height;
			if (out.srcW <= 0 || out.srcH <= 0)
			{
				out.error = "image has no size";
				return out;
			}
			if (out.srcW > kMaxSide || out.srcH > kMaxSide || (long long)out.srcW * out.srcH > kMaxPixels)
			{
				out.error = "image too large";
				return out;
			}

			double scale = 1;
			if (targetW > 0) scale = std::min(2.0, double(targetW) / double(out.srcW));
			// a cap is measured on the height the width's scale gives: both sides then fall together, keeping the aspect
			if (maxH > 0 && double(out.srcH) * scale > double(maxH)) scale = double(maxH) / double(out.srcH);
			if (maxPixels > 0)
			{
				// no wider than the image (the GPU enlarges it on screen just as well), and within the pixel cap
				scale = std::min(scale, 1.0);
				double px = double(out.srcW) * out.srcH * scale * scale;
				if (px > double(maxPixels)) scale *= std::sqrt(double(maxPixels) / px);
			}
			const int w = std::max(1, (int)std::lround(double(out.srcW) * scale));
			const int h = std::max(1, (int)std::lround(double(out.srcH) * scale));
			if (w > kMaxSide || h > kMaxSide || (long long)w * h > kMaxPixels)
			{
				out.error = "image too large";
				return out;
			}

			AVPixelFormat srcFormat = (AVPixelFormat)f->format;
			const uint8_t* src[4] = {f->data[0], f->data[1], f->data[2], f->data[3]};
			int srcStride[4] = {f->linesize[0], f->linesize[1], f->linesize[2], f->linesize[3]};
			std::vector<uint8_t> palette;
			if (srcFormat == AV_PIX_FMT_PAL8)
			{
				std::string error;
				if (!paletteRgba(f, palette, error))
				{
					out.error = error;
					return out;
				}
				src[0] = palette.data();
				src[1] = nullptr;
				src[2] = nullptr;
				src[3] = nullptr;
				srcStride[0] = out.srcW * 4;
				srcStride[1] = srcStride[2] = srcStride[3] = 0;
				srcFormat = AV_PIX_FMT_RGBA;
			}

			int flags = (w < out.srcW || h < out.srcH) ? SWS_AREA : SWS_BICUBIC;
			SwsContext* sws = sws_getContext(out.srcW, out.srcH, srcFormat, w, h, AV_PIX_FMT_RGBA, flags, nullptr, nullptr,
											nullptr);
			if (!sws && flags != SWS_BICUBIC)
				sws = sws_getContext(out.srcW, out.srcH, srcFormat, w, h, AV_PIX_FMT_RGBA, SWS_BICUBIC, nullptr, nullptr,
									 nullptr);
			UniqueSws swsGuard(sws);
			if (!sws)
			{
				out.error = "unsupported pixel format";
				return out;
			}

			try
			{
				// 255 rather than 0: a format without alpha must read as opaque, and swscale fills RGB over it
				out.rgba.assign((size_t)w * (size_t)h * 4, 255);
			}
			catch (const std::bad_alloc&)
			{
				out.error = "out of memory";
				return out;
			}
			uint8_t* dst[4] = {out.rgba.data(), nullptr, nullptr, nullptr};
			int dstStride[4] = {w * 4, 0, 0, 0};
			int lines = sws_scale(sws, src, srcStride, 0, out.srcH, dst, dstStride);
			if (lines != h)
			{
				out.rgba.clear();
				out.error = "scaling failed";
				return out;
			}
			out.w = w;
			out.h = h;
			return out;
		}
	}

	PageImage decodePage(const uint8_t* data, size_t size, int targetW, int maxH, long long maxPixels)
	{
		if (!data || size == 0) return fail("empty image");
		const Kind kind = kindOf(data, size);
		if (kind == Kind::Unknown) return fail("unknown image format");
		if (kind == Kind::Jpeg && !jpegEnded(data, size)) return fail("truncated image");

		UniqueFrame frame(av_frame_alloc());
		if (!frame) return fail("out of memory");

		std::string error;
		bool decoded = false;
		if (kind == Kind::Avif)
		{
			decoded = decodeAvif(data, size, frame.get(), error);
		}
		else
		{
			const AVCodec* codec = avcodec_find_decoder(codecId(kind));
			if (!codec) return fail("no decoder for this image");
			UniqueCodec cc(avcodec_alloc_context3(codec));
			if (!cc) return fail("out of memory");
			// one page on the caller's decode thread: a decoder that spreads itself over more would only fight it
			cc->thread_count = 1;
			cc->thread_type = 0;
			if (avcodec_open2(cc.get(), codec, nullptr) < 0) return fail("can't open the decoder");
			decoded = decodeStill(cc.get(), data, size, frame.get(), error);
		}
		if (!decoded) return fail(error.c_str());
		return scaled(frame.get(), targetW, maxH, maxPixels);
	}

	bool readPageFile(const std::string& path, std::vector<uint8_t>& bytes, std::string& error)
	{
		auto no = [&](const char* why) { error = why; return false; };
		std::ifstream in(path, std::ios::binary | std::ios::ate);
		if (!in) return no("can't open the file");
		std::streamoff n = in.tellg();
		if (n <= 0) return no(n == 0 ? "empty image" : "can't read the file");
		if ((unsigned long long)n > (unsigned long long)kMaxFile) return no("file too large");
		try
		{
			bytes.resize((size_t)n);
		}
		catch (const std::bad_alloc&)
		{
			return no("out of memory");
		}
		in.seekg(0);
		if (!in.read((char*)bytes.data(), (std::streamsize)bytes.size())) return no("can't read the file");
		return true;
	}

	PageImage decodePageFile(const std::string& path, int targetW, int maxH, long long maxPixels)
	{
		std::vector<uint8_t> bytes;
		std::string error;
		if (!readPageFile(path, bytes, error)) return fail(error.c_str());
		return decodePage(bytes.data(), bytes.size(), targetW, maxH, maxPixels);
	}

	namespace
	{
		// (libjpeg and libpng report an error by a longjmp: nothing with a destructor is made between the setjmp and
		// the calls that can jump; the rows go into the caller's vector)
		struct JpegErr
		{
			jpeg_error_mgr pub;
			jmp_buf jump;
		};

		void jpegFail(j_common_ptr c) { longjmp(((JpegErr*)c->err)->jump, 1); }
		void jpegQuiet(j_common_ptr) {}

		// rows y0 .. y0 + n of a JPEG as RGBA: the rows above are skipped (their coefficients read, not turned into
		// pixels), the ones below never read
		bool jpegRows(const uint8_t* data, size_t size, int y0, int rows, std::vector<uint8_t>& px, int& sw, int& sh,
			int& got, const char*& error)
		{
			jpeg_decompress_struct c;
			JpegErr e;
			c.err = jpeg_std_error(&e.pub);
			e.pub.error_exit = jpegFail;
			e.pub.output_message = jpegQuiet;
			if (setjmp(e.jump))
			{
				jpeg_destroy_decompress(&c);
				error = "corrupt image";
				return false;
			}
			jpeg_create_decompress(&c);
			jpeg_mem_src(&c, data, (unsigned long)size);
			jpeg_read_header(&c, TRUE);
			c.out_color_space = JCS_EXT_RGBA;	// (alpha comes as 255; a CMYK picture can't go this way and errors)
			jpeg_start_decompress(&c);
			sw = (int)c.output_width;
			sh = (int)c.output_height;
			got = std::min(rows, sh - y0);
			if (sw <= 0 || sw > kMaxSide || got <= 0)
			{
				jpeg_destroy_decompress(&c);
				error = "image too large";
				return false;
			}
			try
			{
				px.resize((size_t)sw * got * 4);
			}
			catch (const std::bad_alloc&)
			{
				jpeg_destroy_decompress(&c);
				error = "out of memory";
				return false;
			}
			if (y0 > 0) jpeg_skip_scanlines(&c, (JDIMENSION)y0);
			while ((int)c.output_scanline < y0 + got)
			{
				JSAMPROW row = px.data() + (size_t)(c.output_scanline - y0) * sw * 4;
				jpeg_read_scanlines(&c, &row, 1);
			}
			// a warning is damaged data (a cut file's rows come grey): the whole-page path says so properly
			bool bad = e.pub.num_warnings > 0;
			jpeg_destroy_decompress(&c);	// (not finish: that would read the rows below the band)
			if (bad) error = "corrupt image";
			return !bad;
		}

		struct PngSource
		{
			const uint8_t* data;
			size_t size, pos;
		};

		void pngRead(png_structp p, png_bytep out, png_size_t n)
		{
			auto* s = (PngSource*)png_get_io_ptr(p);
			if (n > s->size - s->pos) png_error(p, "cut");
			std::memcpy(out, s->data + s->pos, n);
			s->pos += n;
		}
		void pngFail(png_structp p, png_const_charp) { longjmp(png_jmpbuf(p), 1); }
		void pngQuiet(png_structp, png_const_charp) {}

		// the same for a PNG: its rows come in order, so the ones above the band pass through `scratch` (one row)
		bool pngRows(const uint8_t* data, size_t size, int y0, int rows, std::vector<uint8_t>& px,
			std::vector<uint8_t>& scratch, int& sw, int& sh, int& got, const char*& error)
		{
			PngSource src{data, size, 0};
			png_structp p = png_create_read_struct(PNG_LIBPNG_VER_STRING, nullptr, pngFail, pngQuiet);
			if (!p) { error = "out of memory"; return false; }
			png_infop info = png_create_info_struct(p);
			if (!info || setjmp(png_jmpbuf(p)))
			{
				png_destroy_read_struct(&p, &info, nullptr);
				error = "corrupt image";
				return false;
			}
			png_set_read_fn(p, &src, pngRead);
			png_read_info(p, info);
			sw = (int)png_get_image_width(p, info);
			sh = (int)png_get_image_height(p, info);
			got = std::min(rows, sh - y0);
			// (an interlaced picture's rows don't come in order: libpng would need all of it)
			bool can = png_get_interlace_type(p, info) == PNG_INTERLACE_NONE && sw > 0 && sw <= kMaxSide && got > 0;
			if (can)
			{
				// every kind of PNG to 8-bit RGBA
				png_set_expand(p);
				png_set_strip_16(p);
				png_set_gray_to_rgb(p);
				png_set_add_alpha(p, 0xFF, PNG_FILLER_AFTER);
				png_read_update_info(p, info);
				can = png_get_rowbytes(p, info) == (size_t)sw * 4;
			}
			if (can)
			{
				try
				{
					px.resize((size_t)sw * got * 4);
					scratch.resize((size_t)sw * 4);
				}
				catch (const std::bad_alloc&)
				{
					can = false;
				}
			}
			if (!can)
			{
				png_destroy_read_struct(&p, &info, nullptr);
				error = "not a banded picture";
				return false;
			}
			for (int y = 0; y < y0 + got; y++)
				png_read_row(p, y < y0 ? scratch.data() : px.data() + (size_t)(y - y0) * sw * 4, nullptr);
			png_destroy_read_struct(&p, &info, nullptr);
			return true;
		}
	}

	PageImage decodeBand(const uint8_t* data, size_t size, int targetW, int y0, int rows)
	{
		if (!data || size == 0) return fail("empty image");
		if (y0 < 0 || rows <= 0) return fail("no such band");
		const Kind kind = kindOf(data, size);
		std::vector<uint8_t> px, scratch;
		int sw = 0, sh = 0, got = 0;
		const char* error = nullptr;
		bool ok = false;
		if (kind == Kind::Jpeg)
		{
			if (!jpegEnded(data, size)) return fail("truncated image");
			ok = jpegRows(data, size, y0, rows, px, sw, sh, got, error);
		}
		else if (kind == Kind::Png) ok = pngRows(data, size, y0, rows, px, scratch, sw, sh, got, error);
		else return fail("not a banded picture");
		if (!ok) return fail(error ? error : "corrupt image");

		PageImage out;
		out.srcW = sw;
		out.srcH = sh;
		double scale = targetW > 0 ? std::min(1.0, double(targetW) / sw) : 1.0;
		const int w = std::max(1, (int)std::lround(sw * scale)), h = std::max(1, (int)std::lround(got * scale));
		if (w == sw && h == got)
		{
			out.rgba = std::move(px);
			out.w = w;
			out.h = h;
			return out;
		}
		// the band alone through the scaler (its edge rows repeat at the cut: under a pixel's difference at a seam)
		UniqueSws sws(sws_getContext(sw, got, AV_PIX_FMT_RGBA, w, h, AV_PIX_FMT_RGBA, SWS_AREA, nullptr, nullptr, nullptr));
		if (!sws) return fail("unsupported pixel format");
		try
		{
			out.rgba.assign((size_t)w * (size_t)h * 4, 255);
		}
		catch (const std::bad_alloc&)
		{
			return fail("out of memory");
		}
		const uint8_t* s[4] = {px.data(), nullptr, nullptr, nullptr};
		int sStride[4] = {sw * 4, 0, 0, 0};
		uint8_t* d[4] = {out.rgba.data(), nullptr, nullptr, nullptr};
		int dStride[4] = {w * 4, 0, 0, 0};
		if (sws_scale(sws.get(), s, sStride, 0, got, d, dStride) != h)
		{
			out.rgba.clear();
			out.error = "scaling failed";
			return out;
		}
		out.w = w;
		out.h = h;
		return out;
	}

	PageImage bookPage(PageBook& book, int index, int targetW, long long maxPixels)
	{
		if (book.rendered()) return book.render(index, targetW, maxPixels);
		PageImage img;
		if (index < 0 || index >= (int)book.pages().size())
		{
			img.error = "No such page";
			return img;
		}
		std::vector<uint8_t> bytes;
		if (!book.read(book.pages()[index], bytes))
		{
			img.error = book.error();
			return img;
		}
		return decodePage(bytes.data(), bytes.size(), targetW, 0, maxPixels);
	}
}
