#include "videoframe.h"
#include <algorithm>
#include <chrono>
#include <csetjmp>
#include <cstdio>
#include <jpeglib.h>

extern "C"
{
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libswscale/swscale.h>
}

namespace bp
{
	namespace
	{
		using Clock = std::chrono::steady_clock;

		int pastDeadline(void* opaque) { return Clock::now() > *static_cast<Clock::time_point*>(opaque); }

		std::string tag(AVDictionary* d, const char* key)
		{
			AVDictionaryEntry* e = av_dict_get(d, key, nullptr, 0);
			return e && e->value ? e->value : "";
		}

		double headerDuration(const AVFormatContext* f)
		{
			if (f->duration != AV_NOPTS_VALUE && f->duration > 0) return double(f->duration) / AV_TIME_BASE;
			double d = 0;
			for (unsigned i = 0; i < f->nb_streams; i++)
			{
				const AVStream* s = f->streams[i];
				if (s->duration != AV_NOPTS_VALUE && s->duration > 0 && s->time_base.num > 0)
					d = std::max(d, double(s->duration) * av_q2d(s->time_base));
			}
			return d;
		}

		// the facts the header holds (the picture's stream: vi, -1 none)
		void readMeta(AVFormatContext* f, int vi, VideoMeta& m)
		{
			m.duration = headerDuration(f);
			m.chapters = (int)f->nb_chapters;
			if (vi >= 0)
			{
				const AVCodecParameters* c = f->streams[vi]->codecpar;
				m.vcodec = avcodec_get_name(c->codec_id);
				m.width = c->width;
				m.height = c->height;
			}
			for (unsigned i = 0; i < f->nb_streams; i++)
			{
				const AVStream* s = f->streams[i];
				AVMediaType t = s->codecpar->codec_type;
				if (t != AVMEDIA_TYPE_AUDIO && t != AVMEDIA_TYPE_SUBTITLE) continue;
				VideoMeta::Stream st;
				st.lang = tag(s->metadata, "language");
				st.title = tag(s->metadata, "title");
				st.codec = avcodec_get_name(s->codecpar->codec_id);
				if (t == AVMEDIA_TYPE_AUDIO) st.channels = s->codecpar->ch_layout.nb_channels;
				(t == AVMEDIA_TYPE_AUDIO ? m.audio : m.subs).push_back(st);
			}
		}

		// the first frame decoded from the keyframe at or before `at` seconds, or null
		AVFrame* frameAt(AVFormatContext* f, AVCodecContext* dec, int vi, double at)
		{
			const AVStream* s = f->streams[vi];
			int64_t ts = (int64_t)(at / av_q2d(s->time_base));
			if (s->start_time != AV_NOPTS_VALUE) ts += s->start_time;
			if (at > 0) av_seek_frame(f, vi, ts, AVSEEK_FLAG_BACKWARD);
			avcodec_flush_buffers(dec);
			AVPacket* pkt = av_packet_alloc();
			AVFrame* fr = av_frame_alloc();
			bool got = false;
			// (a bound on the packets read: a broken file never decodes, it ends here)
			for (int n = 0; pkt && fr && !got && n < 600; n++)
			{
				int r = av_read_frame(f, pkt);
				if (r < 0)
				{
					avcodec_send_packet(dec, nullptr);	// the end: what the decoder holds
					got = avcodec_receive_frame(dec, fr) == 0;
					break;
				}
				if (pkt->stream_index == vi && avcodec_send_packet(dec, pkt) >= 0)
					got = avcodec_receive_frame(dec, fr) == 0;
				av_packet_unref(pkt);
			}
			av_packet_free(&pkt);
			if (!got) av_frame_free(&fr);
			return fr;
		}

		// w x h RGBA from a picture in any of libswscale's formats (planes and strides as AVFrame holds them)
		PageImage scale(const uint8_t* const src[4], const int stride[4], AVPixelFormat fmt, int sw, int sh, int w, int h)
		{
			PageImage out;
			if (w <= 0 || h <= 0 || sw <= 0 || sh <= 0) return out;
			SwsContext* sws = sws_getContext(sw, sh, fmt, w, h, AV_PIX_FMT_RGBA, SWS_AREA, nullptr, nullptr, nullptr);
			if (!sws) return out;
			out.w = w;
			out.h = h;
			out.rgba.assign((size_t)w * h * 4, 255);
			uint8_t* dst[4] = {out.rgba.data()};
			int dstStride[4] = {w * 4};
			sws_scale(sws, src, stride, 0, sh, dst, dstStride);
			sws_freeContext(sws);
			return out;
		}

		// the picture's mean brightness (0..255): a fade from black is no picture to show
		int meanLuma(const PageImage& img)
		{
			if (!img.ok()) return 0;
			long long sum = 0;
			size_t n = (size_t)img.w * img.h;
			for (size_t i = 0; i < n; i++)
			{
				const uint8_t* p = &img.rgba[i * 4];
				sum += (p[0] * 2 + p[1] * 5 + p[2]) / 8;
			}
			return (int)(sum / (long long)n);
		}

		void makePictures(const AVFrame* fr, AVRational sar, VideoGrab& out, int largeW, int smallW, int smallH)
		{
			// the display aspect: the frame's pixels with their own shape
			double aspect = (double)fr->width / fr->height;
			if (sar.num > 0 && sar.den > 0) aspect *= av_q2d(sar);
			int lw = std::min(largeW, std::max(fr->width, (int)std::lround(fr->height * aspect)));
			int lh = std::max(1, (int)std::lround(lw / aspect));
			out.large = scale(fr->data, fr->linesize, (AVPixelFormat)fr->format, fr->width, fr->height, lw, lh);
			if (!out.large.ok()) return;
			// the small one: the middle of the large one at the row's own shape (16:9)
			double want = (double)smallW / smallH;
			int cw = lw, ch = lh;
			if ((double)lw / lh > want) cw = std::max(1, (int)std::lround(lh * want));
			else ch = std::max(1, (int)std::lround(lw / want));
			const uint8_t* src[4] = {&out.large.rgba[((size_t)((lh - ch) / 2) * lw + (lw - cw) / 2) * 4]};
			int stride[4] = {lw * 4};
			out.small = scale(src, stride, AV_PIX_FMT_RGBA, cw, ch, smallW, smallH);
		}

		struct JpegErr
		{
			jpeg_error_mgr pub;
			jmp_buf jump;
		};
		void jpegFail(j_common_ptr c) { longjmp(((JpegErr*)c->err)->jump, 1); }
		void jpegQuiet(j_common_ptr) {}
	}

	bool grabVideo(const std::string& path, VideoGrab& out, int largeW, int smallW, int smallH)
	{
		out = VideoGrab{};
		Clock::time_point deadline = Clock::now() + std::chrono::seconds(6);
		AVFormatContext* f = avformat_alloc_context();
		if (!f) return false;
		f->interrupt_callback.callback = pastDeadline;
		f->interrupt_callback.opaque = &deadline;
		AVDictionary* opts = nullptr;
		av_dict_set(&opts, "probesize", "2097152", 0);
		int r = avformat_open_input(&f, path.c_str(), nullptr, &opts);
		av_dict_free(&opts);
		if (r < 0) return false;	// (f freed by avformat_open_input)
		int vi = -1;
		for (unsigned i = 0; i < f->nb_streams; i++)
		{
			const AVStream* s = f->streams[i];
			if (s->codecpar->codec_type == AVMEDIA_TYPE_VIDEO && !(s->disposition & AV_DISPOSITION_ATTACHED_PIC))
			{
				vi = (int)i;
				break;
			}
		}
		// a transport stream's header has no picture size: the streams are read a little for it
		if (vi >= 0 && f->streams[vi]->codecpar->width <= 0) avformat_find_stream_info(f, nullptr);
		readMeta(f, vi, out.meta);
		out.metaOk = true;
		const AVCodec* codec = vi >= 0 ? avcodec_find_decoder(f->streams[vi]->codecpar->codec_id) : nullptr;
		AVCodecContext* dec = codec ? avcodec_alloc_context3(codec) : nullptr;
		if (dec && avcodec_parameters_to_context(dec, f->streams[vi]->codecpar) >= 0)
		{
			dec->thread_count = 1;	// (one core: playback and the panels keep the others)
			if (avcodec_open2(dec, codec, nullptr) >= 0)
			{
				// a tenth of the way in (at most 90 s: past the logos), then a third if that one is black
				double dur = out.meta.duration;
				double tries[2] = {dur > 0 ? std::min(dur * 0.1, 90.0) : 10.0, dur > 0 ? dur / 3 : 60.0};
				AVRational sar = av_guess_sample_aspect_ratio(f, f->streams[vi], nullptr);
				int best = -1;
				for (double at : tries)
				{
					AVFrame* fr = frameAt(f, dec, vi, at);
					if (!fr) continue;
					VideoGrab g;
					makePictures(fr, sar, g, largeW, smallW, smallH);
					av_frame_free(&fr);
					int luma = meanLuma(g.small);
					if (luma > best)
					{
						best = luma;
						out.large = std::move(g.large);
						out.small = std::move(g.small);
					}
					if (luma >= 24) break;
				}
			}
		}
		avcodec_free_context(&dec);
		avformat_close_input(&f);
		return true;
	}

	std::string encodeJpeg(const PageImage& img, int quality)
	{
		if (!img.ok()) return "";
		jpeg_compress_struct c;
		JpegErr e;
		c.err = jpeg_std_error(&e.pub);
		e.pub.error_exit = jpegFail;
		e.pub.output_message = jpegQuiet;
		unsigned char* mem = nullptr;
		unsigned long size = 0;
		if (setjmp(e.jump))
		{
			jpeg_destroy_compress(&c);
			free(mem);
			return "";
		}
		jpeg_create_compress(&c);
		jpeg_mem_dest(&c, &mem, &size);
		c.image_width = (JDIMENSION)img.w;
		c.image_height = (JDIMENSION)img.h;
		c.input_components = 4;
		c.in_color_space = JCS_EXT_RGBA;
		jpeg_set_defaults(&c);
		jpeg_set_quality(&c, quality, TRUE);
		jpeg_start_compress(&c, TRUE);
		while (c.next_scanline < c.image_height)
		{
			JSAMPROW row = (JSAMPROW)&img.rgba[(size_t)c.next_scanline * img.w * 4];
			jpeg_write_scanlines(&c, &row, 1);
		}
		jpeg_finish_compress(&c);
		jpeg_destroy_compress(&c);
		std::string out((const char*)mem, size);
		free(mem);
		return out;
	}
}
