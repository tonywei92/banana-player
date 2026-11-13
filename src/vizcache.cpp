#include "vizcache.h"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <functional>
#include <set>
#include <sstream>
#include <utility>
#include <dirent.h>
#include <sys/stat.h>
#include <unistd.h>

extern "C" {
#include <libavutil/eval.h>
}

namespace bp
{
	// a new number whenever what a file holds, or how it's made, changes: every one is made again
	static const char* LUT_VERSION = "1";
	static const int SIDE = 256;
	// a lookup kept only when it's this much quicker than the expression it replaces, and no longer than this many
	// pieces (its parse, at every build, stays well under a millisecond)
	static const double MIN_GAIN = 1.5;
	static const int MAX_LEAVES = 4096;
	size_t vizLutMinLength = 20;

	std::string vizLutFile(const std::string& key)
	{
		// FNV-1a, 64 bits, over the version and the key
		uint64_t h = 1469598103934665603ULL;
		std::string text = std::string(LUT_VERSION) + "\n" + key;
		for (unsigned char c : text) h = (h ^ c) * 1099511628211ULL;
		char name[40];
		snprintf(name, sizeof name, "lut/%016llx.txt", (unsigned long long)h);
		return name;
	}

	// a key's expression parsed with its filter's names (vf_lut2.c, vf_lut.c) less w and h, the picture's size (an
	// expression with them isn't the same table at every size; lut's gammaval functions aren't given either)
	static AVExpr* parse(bool two, const std::string& expr)
	{
		static const char* const names2[] = {"x", "y", "bdx", "bdy", nullptr};
		static const char* const names1[] = {"val", "maxval", "minval", "negval", "clipval", nullptr};
		AVExpr* e = nullptr;
		if (av_expr_parse(&e, expr.c_str(), two ? names2 : names1, nullptr, nullptr, nullptr, nullptr, 0, nullptr) < 0) return nullptr;
		return e;
	}

	// the table: lut2's 256 x 256 (row y, column x) or lut's 256 values on 8-bit pictures, as the filter makes them.
	// false: not a table (it doesn't parse, or a value the filter would turn into a number by undefined means)
	static bool table(bool two, const std::string& expr, std::vector<unsigned char>& out, double* seconds = nullptr)
	{
		AVExpr* e = parse(two, expr);
		if (!e) return false;
		auto t0 = std::chrono::steady_clock::now();
		bool ok = true;
		if (two)
		{
			// as lut2 does it: y outer, x inner, one evaluator throughout (st() and ld() carry over); its lut holds
			// uint16_t and an 8-bit output keeps the low byte. NaN fails its configure; a negative or one past 65535
			// is undefined: no table
			out.assign((size_t)SIDE * SIDE, 0);
			double vars[4] = {0, 0, 8, 8};
			for (int y = 0; y < SIDE && ok; y++)
			{
				vars[1] = y;
				for (int x = 0; x < SIDE; x++)
				{
					vars[0] = x;
					double r = av_expr_eval(e, vars, nullptr);
					if (!(r > -1.0 && r < 65536.0))
					{
						ok = false;
						break;
					}
					out[(size_t)y * SIDE + x] = (unsigned char)((uint16_t)r & 0xff);
				}
			}
		}
		else
		{
			// as lut does it on gray8: values from 0 up (the first 256 of its 65536, so st() and ld() hold the same),
			// minval 0, maxval 255, clipval the value, negval 255 less it; kept as av_clip((int)r, 0, 255)
			out.assign(SIDE, 0);
			for (int v = 0; v < SIDE; v++)
			{
				double vars[5] = {(double)v, 255, 0, (double)(255 - v), (double)v};
				double r = av_expr_eval(e, vars, nullptr);
				if (!(r > -2147483648.0 && r < 2147483648.0))
				{
					ok = false;
					break;
				}
				int i = (int)r;
				out[v] = (unsigned char)(i < 0 ? 0 : i > 255 ? 255 : i);
			}
		}
		if (seconds) *seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
		av_expr_free(e);
		return ok;
	}

	bool vizLutTable(const std::string& key, std::vector<unsigned char>& out)
	{
		if (key.size() < 2 || key[1] != ':' || (key[0] != '1' && key[0] != '2')) return false;
		return table(key[0] == '2', key.substr(2), out);
	}

	// The lookup: the table again as an expression that finds a value by halves. A line of values (a lut's, or one
	// column or row of a lut2's) cut into pieces, each one number or the variable plus one (a palette's "0 keeps the
	// art" is y; a shade added to the art, y+12); a lut2's lines with the same pieces side by side as one; a value
	// found by comparing with where the pieces start, half of them at a time. A palette of 30 colours is ~5
	// comparisons instead of ~15 nested if(eq()), a sprite or Long cat's clouds a few more instead of hundreds of
	// operations. A line that would take too many pieces (a smooth shade) is left to the expression itself there
	struct Piece
	{
		int from;
		std::string leaf;
	};
	static const int LINE_PIECES = 24;

	static std::vector<Piece> pieces(const unsigned char* v, int step, const char* var)
	{
		std::vector<Piece> out;
		for (int i = 0; i < SIDE;)
		{
			int c = i + 1, o = i + 1, k = v[i * step] - i;
			while (c < SIDE && v[c * step] == v[i * step]) c++;
			while (o < SIDE && v[o * step] - o == k) o++;
			if (o > c)
			{
				out.push_back({i, k == 0 ? std::string(var) : std::string(var) + (k > 0 ? "+" : "") + std::to_string(k)});
				i = o;
			}
			else
			{
				out.push_back({i, std::to_string(v[i * step])});
				i = c;
			}
		}
		return out;
	}

	// by halves over pieces [lo, hi]
	static std::string halves(const std::vector<Piece>& p, int lo, int hi, const char* var)
	{
		if (lo == hi) return p[lo].leaf;
		int mid = (lo + hi + 1) / 2;
		return "if(lt(" + std::string(var) + "," + std::to_string(p[mid].from) + ")," + halves(p, lo, mid - 1, var) + "," + halves(p, mid, hi, var) + ")";
	}

	// the lookup for a whole table (outer variable x and lines down y, or y and lines across x), and its pieces;
	// expr: the leaf for a line too smooth for pieces
	static std::string lookup(const std::vector<unsigned char>& t, bool two, bool byColumn, const std::string& expr, int& leaves)
	{
		if (!two)
		{
			std::vector<Piece> p = pieces(t.data(), 1, "val");
			leaves = (int)p.size();
			return halves(p, 0, (int)p.size() - 1, "val");
		}
		const char* outer = byColumn ? "x" : "y";
		const char* inner = byColumn ? "y" : "x";
		std::vector<Piece> lines;
		leaves = 0;
		for (int i = 0; i < SIDE; i++)
		{
			const unsigned char* start = byColumn ? &t[i] : &t[(size_t)i * SIDE];
			std::vector<Piece> p = pieces(start, byColumn ? SIDE : 1, inner);
			std::string e = (int)p.size() > LINE_PIECES ? "(" + expr + ")" : halves(p, 0, (int)p.size() - 1, inner);
			if (!lines.empty() && e == lines.back().leaf) continue;
			lines.push_back({i, e});
			leaves += (int)p.size() > LINE_PIECES ? 1 : (int)p.size();
			if (leaves > MAX_LEAVES) return "";
		}
		return halves(lines, 0, (int)lines.size() - 1, outer);
	}

	// the quickest lookup that gives every value of the table (checked); or "" (none quicker than the expression)
	static std::string compile(bool two, const std::string& expr, const std::vector<unsigned char>& t, double exprSeconds)
	{
		std::string best;
		double bestSeconds = exprSeconds / MIN_GAIN;
		for (int by = 0; by < (two ? 2 : 1); by++)
		{
			int leaves = 0;
			std::string e = lookup(t, two, by == 0, expr, leaves);
			if (e.empty()) continue;
			std::vector<unsigned char> check;
			double seconds = 0;
			// (lut: 256 values timed against the expression's 256; it works out 65536 of each)
			if (table(two, e, check, &seconds) && check == t && seconds < bestSeconds) best = e, bestSeconds = seconds;
		}
		return best;
	}

	static bool ready(const std::string& path)
	{
		struct stat st;
		return stat(path.c_str(), &st) == 0 && st.st_size > 0;
	}

	static std::string readFile(const std::string& path)
	{
		std::ifstream in(path, std::ios::binary);
		std::ostringstream s;
		s << in.rdbuf();
		return s.str();
	}

	// the file for a key: its lookup, or "=" (the expression is as quick as it gets, or can't be a table)
	static bool writeLut(const std::string& key, const std::string& mapsDir)
	{
		std::string path = mapsDir + "/" + vizLutFile(key);
		if (ready(path)) return true;
		std::string text = "=";
		std::vector<unsigned char> t;
		double seconds = 0;
		bool two = key[0] == '2';
		if (key.size() > 2 && key[1] == ':' && table(two, key.substr(2), t, &seconds))
		{
			std::string e = compile(two, key.substr(2), t, seconds);
			if (!e.empty()) text = e;
		}
		mkdir(mapsDir.c_str(), 0755);
		mkdir((mapsDir + "/lut").c_str(), 0755);
		// (a name of its own: another thread may be writing the same one)
		static int seq = 0;
		std::string tmp = path + "." + std::to_string(getpid()) + "-" + std::to_string(__sync_add_and_fetch(&seq, 1)) + ".tmp";
		{
			std::ofstream f(tmp, std::ios::binary | std::ios::trunc);
			f << text;
			if (!f.flush())
			{
				f.close();
				unlink(tmp.c_str());
				return false;
			}
		}
		if (rename(tmp.c_str(), path.c_str()) != 0)
		{
			unlink(tmp.c_str());
			return false;
		}
		return true;
	}

	bool vizWriteLuts(const std::vector<std::string>& keys, const std::string& mapsDir)
	{
		bool ok = true;
		for (const std::string& k : keys) ok = writeLut(k, mapsDir) && ok;
		return ok;
	}

	// a marker: where its expression is (from..to, inside the quotes) and its key
	struct Site
	{
		size_t marker, from, to;
		std::string key;
	};

	static std::vector<Site> sites(const std::string& g)
	{
		std::vector<Site> out;
		for (size_t at = g.find("bplut"); at != std::string::npos; at = g.find("bplut", at + 1))
		{
			bool two = g.compare(at, strlen(VIZ_LUT2), VIZ_LUT2) == 0;
			if (!two && g.compare(at, strlen(VIZ_LUT), VIZ_LUT) != 0) continue;
			size_t q = at + strlen(two ? VIZ_LUT2 : VIZ_LUT);
			if (g.compare(q, 4, "c0='") != 0) continue;
			q += 4;
			size_t end = g.find('\'', q);
			if (end == std::string::npos) break;
			out.push_back({at, q, end, std::string(two ? "2:" : "1:") + g.substr(q, end - q)});
			at = end;
		}
		return out;
	}

	// worth a table: long enough, and nothing the graph's parser would read first (a backslash)
	static bool worth(const Site& s)
	{
		return s.key.size() - 2 >= vizLutMinLength && s.key.find('\\') == std::string::npos;
	}

	std::vector<std::string> vizLutKeys(const std::string& graph)
	{
		std::vector<std::string> out;
		for (const Site& s : sites(graph))
			if (worth(s)) out.push_back(s.key);
		return out;
	}

	std::string vizPlainLuts(const std::string& graph)
	{
		std::string g = graph;
		for (size_t at = g.find("bplut"); at != std::string::npos; at = g.find("bplut", at + 1))
			if (g.compare(at, strlen(VIZ_LUT2), VIZ_LUT2) == 0 || g.compare(at, strlen(VIZ_LUT), VIZ_LUT) == 0) g.erase(at, 2);	// "bp"
		return g;
	}

	std::string vizCacheLuts(const std::string& graph, const std::string& mapsDir, std::vector<std::string>* missing)
	{
		std::vector<Site> list = sites(graph);
		if (list.empty() || mapsDir.empty()) return vizPlainLuts(graph);
		std::string out;
		size_t done = 0;
		for (const Site& s : list)
		{
			// the filter's name less its "bp", up to its expression; then the lookup, or the expression as it was
			out += graph.substr(done, s.marker - done) + graph.substr(s.marker + 2, s.from - s.marker - 2);
			done = s.to;
			std::string e;
			if (worth(s))
			{
				std::string path = mapsDir + "/" + vizLutFile(s.key);
				if (!ready(path))
				{
					if (missing) missing->push_back(s.key);
					else writeLut(s.key, mapsDir);
				}
				if (ready(path)) e = readFile(path);
			}
			out += e.empty() || e == "=" ? graph.substr(s.from, s.to - s.from) : e;
		}
		out += graph.substr(done);
		return out;
	}

	void vizPruneLuts(const std::string& mapsDir, const std::vector<std::string>& graphs)
	{
		std::set<std::string> keep;
		for (const std::string& g : graphs)
			for (const std::string& k : vizLutKeys(g)) keep.insert(vizLutFile(k).substr(4));
		std::string dir = mapsDir + "/lut";
		DIR* d = opendir(dir.c_str());
		if (!d) return;
		std::vector<std::string> gone;
		while (dirent* e = readdir(d))
		{
			std::string name = e->d_name;
			// (a .tmp: one being written, by another thread now or when the player quit: left)
			if (name.size() > 4 && name.compare(name.size() - 4, 4, ".txt") == 0 && !keep.count(name)) gone.push_back(name);
		}
		closedir(d);
		for (const std::string& name : gone) unlink((dir + "/" + name).c_str());
	}

	// n (the frame number) in a crop's or overlay's expression, as the frame number it would have at 60 a second
	// at the same moment
	static std::string frameAt60(const std::string& e, int fps)
	{
		std::string out, as = "floor(n*60/" + std::to_string(fps) + ")";
		auto word = [](char c) { return isalnum((unsigned char)c) || c == '_'; };
		for (size_t i = 0; i < e.size(); i++)
		{
			if (e[i] == 'n' && (i == 0 || !word(e[i - 1])) && (i + 1 == e.size() || (!word(e[i + 1]) && e[i + 1] != '(')))
				out += as;
			else out += e[i];
		}
		return out;
	}

	// the number after an option's key at at (the key starting an option: after ':', '=', ',' or a space): its span
	static bool optionAt(const std::string& g, size_t at, const std::string& key, size_t& from, size_t& to)
	{
		if (at > 0 && !strchr(":=, ", g[at - 1])) return false;
		from = at + key.size();
		to = from;
		while (to < g.size() && (isdigit((unsigned char)g[to]) || g[to] == '.')) to++;
		return to > from && (to == g.size() || strchr(":, ;[", g[to]));
	}

	std::string vizShapeInput(const std::string& graph)
	{
		static const std::string head = "[aid1] asplit [ao] [a]; ";
		if (graph.compare(0, head.size(), head) != 0) return graph;
		return "[aid1] asplit [ao] [ashape]; [ashape] aformat=sample_rates=44100|48000:channel_layouts=mono,"
			   "agate=threshold=0.003:range=0.05:attack=5:release=250,"
			   "acompressor=threshold=0.03:ratio=4:attack=600:release=2500:makeup=8:knee=4,"
			   "highshelf=f=2500:g=5:t=s:w=0.6 [a]; " + graph.substr(head.size());
	}

	std::string vizAtFps(const std::string& graph, int fps)
	{
		if (fps == 60 || fps <= 0 || graph.find("rate=60") == std::string::npos) return graph;
		const double k = 60.0 / fps;
		std::string g;
		// the quoted expressions of crop and overlay: their n
		std::string filter;
		bool start = true;
		for (size_t i = 0; i < graph.size(); i++)
		{
			char c = graph[i];
			if (c == '\'' || c == '[')
			{
				size_t j = graph.find(c == '[' ? ']' : '\'', i + 1);
				if (j == std::string::npos) j = graph.size() - 1;
				std::string e = graph.substr(i + 1, j - i - 1);
				if (c == '\'' && (filter == "crop" || filter == "overlay")) e = frameAt60(e, fps);
				g += graph[i] + e + graph[j];
				i = j;
				continue;
			}
			if (c == ',' || c == ';') start = true;
			else if (start && isalpha((unsigned char)c))
			{
				size_t j = i;
				while (j < graph.size() && (isalnum((unsigned char)graph[j]) || graph[j] == '_')) j++;
				filter = graph.substr(i, j - i);
				start = false;
			}
			g += c;
		}
		// the options counted in frames: a source's rate; a fall's decay (a product a frame: d^(60/fps) falls as far
		// in a second); tmix's frames (as many seconds). (Outside the quotes: no expression has these keys)
		auto rate = [&](const std::string& v) { return v == "60" ? std::to_string(fps) : v; };
		auto decay = [&](const std::string& v) {
			char b[32];
			snprintf(b, sizeof b, "%.6f", std::pow(atof(v.c_str()), k));
			return std::string(b);
		};
		auto frames = [&](const std::string& v) { return std::to_string(std::max(1, (int)std::lround(atoi(v.c_str()) / k))); };
		const std::pair<const char*, std::function<std::string(const std::string&)>> rules[] = {
			{"rate=", rate}, {"r=", rate}, {"lagfun=decay=", decay}, {"tmix=frames=", frames}};
		for (const auto& r : rules)
		{
			std::string key = r.first;
			for (size_t at = g.find(key); at != std::string::npos; at = g.find(key, at + 1))
			{
				size_t from, to;
				if (!optionAt(g, at, key, from, to)) continue;
				g.replace(from, to - from, r.second(g.substr(from, to - from)));
			}
		}
		return g;
	}
}
