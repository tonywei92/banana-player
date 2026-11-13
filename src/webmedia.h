// What a browser can play, for the web dashboard (bpcore, no window).
#pragma once
#include <string>

namespace bp
{
	// By extension (any case). Text types carry "; charset=utf-8". Unknown: "application/octet-stream".
	const char* mimeFor(const std::string& name);

	enum class Playable { Yes, Maybe, No };
	// A guess from the extension alone, for listings. Songs and pictures that browsers take: Yes; containers that
	// depend on the codecs inside (mp4, webm, mkv, mov): Maybe; formats browsers don't have, and anything else: No.
	Playable playableByName(const std::string& name);
	// From a probe: container is libavformat's format name ("mov,mp4,m4a,3gp,3g2,mj2", "matroska,webm", "mp3"...),
	// the codecs avcodec_get_name's (acodec: the DEFAULT audio stream's); "" for no such stream. Known-unplayable (ape, wma, dts, ac3/eac3, truehd, mpeg4 part 2,
	// mpeg2, avi, asf...) is No; the table's sure ones Yes; the rest, and what isn't known, Maybe (the browser decides).
	Playable playability(const std::string& container, const std::string& vcodec, const std::string& acodec);

	// Subtitles as WebVTT, for the web dashboard's player (the browser can't read SRT or ASS).
	// "00:01:02.345"; hours always, negative seconds are 0
	std::string vttTimestamp(double seconds);
	// An ASS dialogue text as a cue's: override tags {\...} dropped (\i \b \u kept as <i> <b> <u>), \N and \n a line
	// break, \h a space, drawings ({\p1}...) dropped, & < > escaped
	std::string assCueText(const std::string& text);
	// "WEBVTT\n\n" followed by one cue: "start --> end\ntext\n\n"; empty text, or an end that isn't after the start: ""
	std::string vttCue(double start, double end, const std::string& text);
	// Whole subtitle files by their extension (any case): srt, ass, ssa, vtt. "" for another one. The text may be UTF-8 (BOM or not), UTF-16 with a BOM, or an old
	// code page: langHint ("ja", "jpn", "zh", "chi", "ko"...) picks the charsets to try (Shift-JIS, GB18030...), else Windows-1252. Cues are in start order
	std::string subtitleFileToVtt(const std::string& ext, const std::string& text, const std::string& langHint = "");
	// A subtitle file's text as UTF-8 with \n line ends (an ASS file for the browser's libass). Not UTF-8 (nor UTF-16
	// with a BOM): the chosen code page (setSubtitleCharset), else the guess (uchardet) unless the language (langHint:
	// "sl", "slv", "ru"...) names another family, else the language's code pages, else Windows-1252
	std::string subtitleTextToUtf8(const std::string& bytes, const std::string& langHint = "");
	// Settings › Subtitles › Text encoding's iconv name ("CP1250"...), "" for Auto: set by the player, read by the
	// web server's threads
	void setSubtitleCharset(const std::string& charset);
	// the code page uchardet guesses for these bytes ("WINDOWS-1250"...), "" when it can't or isn't built in
	std::string guessCharset(const std::string& bytes);
	// picture subtitles (hdmv_pgs_subtitle, dvd_subtitle, dvb_subtitle, xsub...), which the web can't show
	bool imageSubtitleCodec(const std::string& codec);

	// A date tag's year: "2021-05-03", "2021", "20210503", "May 2021" -> "2021" (the first run of four digits, 1000-2999); "" if none
	std::string yearFromDate(const std::string& date);
	// A track/disc tag as written, trimmed ("3", "3/12"); "" if it has no digit. "03" keeps its zero: it's as written
	std::string trackText(const std::string& raw);
}
