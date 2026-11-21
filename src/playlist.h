#pragma once
#include <string>
#include <vector>

namespace bp
{
	// by its extension (any case): mp4, mkv, webm, avi, mov, ts...
	bool isVideoFile(const std::string& name);
	// mp3, flac, ogg, wav, m4a, opus...
	bool isAudioFile(const std::string& name);
	// avi, divx: a container whose streams all start at 0 (no start offset of their own)
	bool isAviFile(const std::string& name);
	// what the player plays: any of them, photos too (photo.h)
	bool isMediaFile(const std::string& name);

	// a picture mpv takes as the folder's songs' art (cover-art-auto): cover.jpg, folder.png, front.webp... (any case)
	// beside the file at path
	bool folderHasCover(const std::string& path);

	// "ep2" before "ep10"; letters compared without case
	bool naturalLess(const std::string& a, const std::string& b);

	// the videos and audio files in the file's folder (for a photo: the photos), in natural order, as full paths, and
	// where the file is among them; a file that can't be listed (or isn't one by its name) is a playlist of its own
	std::vector<std::string> siblingVideos(const std::string& path, int& index);
	// the same from a folder's names (a network folder's listing): the names that play with name, in order; empty when
	// name isn't among them
	std::vector<std::string> siblingNames(const std::string& name, const std::vector<std::string>& folderNames, int& index);

	// the row of a list that holds `path`, the cursor coming back from the player: `hint` when that row is it (a file
	// listed twice: the one that played), else its first row; -1 when the list hasn't it
	int playingRow(const std::vector<std::string>& paths, int hint, const std::string& path);
}
