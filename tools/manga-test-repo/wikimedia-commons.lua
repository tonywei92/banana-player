-- Public domain comics from Wikimedia Commons (docs/design/manga.md 5.2).
-- Wikimedia Commons hosts historic public-domain newspaper comic strips published before 1928,
-- including Winsor McCay's Little Nemo in Slumberland and Dream of the Rarebit Fiend, George
-- Herriman's Krazy Kat, and Richard F. Outcault's The Yellow Kid and Buster Brown.
-- Uses the MediaWiki action API over HTTPS with categorymembers and imageinfo (iiurlwidth).

local API_URL = "https://commons.wikimedia.org/w/api.php"
-- Wikimedia Commons policy requires a descriptive User-Agent naming the application and purpose.
local USER_AGENT = "BananaPlayer-MangaTestSource/1.0 (https://github.com/tonywei92/banana-player; test-source) bp.http"

-- Curated catalog of historic public-domain comic strips hosted on Wikimedia Commons.
-- All works published in the United States prior to 1928 are in the public domain.
local SERIES = {
	{
		id = "little-nemo",
		title = "Little Nemo in Slumberland",
		author = "Winsor McCay",
		artist = "Winsor McCay",
		category = "Category:Little Nemo in Slumberland",
		description = "Winsor McCay's masterpiece comic strip following the weekly dream adventures of Little Nemo in Slumberland. Originally published in the New York Herald and New York American from 1905 to 1914. Public domain in the United States.",
		coverFile = "File:Little Nemo 1905-10-15.jpg",
		genres = { "Comic Strip", "Fantasy", "Surrealism", "Public Domain" },
		status = "completed",
		year = 1905,
		mode = "subcat",	-- organized by year subcategories (1905, 1906, etc.)
	},
	{
		id = "rarebit-fiend",
		title = "Dream of the Rarebit Fiend",
		author = "Winsor McCay",
		artist = "Winsor McCay",
		category = "Category:Dream of the Rarebit Fiend",
		description = "Pioneering comic strip by Winsor McCay depicting the bizarre, hallucinatory nightmares of characters who ate Welsh rarebit before going to bed. Published in the New York Evening Telegram (1904–1913). Public domain in the United States.",
		coverFile = "File:Rarebit_Fiend_1905-06-03.jpg",
		genres = { "Comic Strip", "Humor", "Surrealism", "Public Domain" },
		status = "completed",
		year = 1904,
		mode = "subcat",	-- organized by year subcategories (1904, 1905, etc.)
	},
	{
		id = "krazy-kat",
		title = "Krazy Kat",
		author = "George Herriman",
		artist = "George Herriman",
		category = "Category:Krazy Kat comics",
		description = "George Herriman's acclaimed comic strip set in Coconino County, Arizona, chronicling the surreal love triangle between Krazy Kat, Ignatz Mouse, and Officer Pupp. Public domain in the United States.",
		coverFile = "File:Krazy Kat 1916-04-23.jpg",
		genres = { "Comic Strip", "Humor", "Surrealism", "Public Domain" },
		status = "completed",
		year = 1913,
		mode = "file",		-- individual strip files directly in category
	},
	{
		id = "yellow-kid",
		title = "The Yellow Kid",
		author = "Richard F. Outcault",
		artist = "Richard F. Outcault",
		category = "Category:Yellow Kid",
		description = "Richard F. Outcault's famous comic strip Hogan's Alley, featuring Mickey Dugan ('The Yellow Kid') in his iconic yellow nightshirt. Widely recognized as one of the first Sunday newspaper comic strips (1895–1898). Public domain in the United States.",
		coverFile = "File:The Yellow Kid in the Ryan apartment.jpg",
		genres = { "Comic Strip", "Satire", "Historical", "Public Domain" },
		status = "completed",
		year = 1895,
		mode = "file",
	},
	{
		id = "buster-brown",
		title = "Buster Brown",
		author = "Richard F. Outcault",
		artist = "Richard F. Outcault",
		category = "Category:Buster Brown",
		description = "Classic comic strip created in 1902 by Richard F. Outcault about mischievous boy Buster Brown and his talking pit bull terrier Tige. Public domain in the United States.",
		coverFile = "File:Buster Brown 1906-01-21.jpg",
		genres = { "Comic Strip", "Humor", "Public Domain" },
		status = "completed",
		year = 1902,
		mode = "file",
	},
}

-- Preferred thumbnail width for page and cover scaling (iiurlwidth).
-- Banana Player displays on 640x480 panels; 1280px provides crisp rendering when zoomed.
local function getImageWidth()
	local pref = bp.prefs.get("image_width")
	if pref == "800" or pref == "1024" or pref == "1280" or pref == "1600" then
		return pref
	end
	return "1280"
end

-- Perform a GET request against the Wikimedia Commons API and decode the JSON response.
local function apiGet(params)
	local queryStr = bp.url.query(params)
	local url = API_URL .. "?" .. queryStr
	local res = bp.http.get(url, {
		headers = {
			["User-Agent"] = USER_AGENT,
			["Accept"] = "application/json",
		},
		timeout = 15,
	})
	if not res or res.status ~= 200 or not res.body or res.body == "" then
		return nil
	end
	local ok, data = pcall(bp.json.decode, res.body)
	if ok and type(data) == "table" then
		return data
	end
	return nil
end

-- Find a series entry by id.
local function findSeries(id)
	for _, s in ipairs(SERIES) do
		if s.id == id or s.category == id then
			return s
		end
	end
	return nil
end

-- Batch-query imageinfo with iiurlwidth to retrieve scaled thumbnail URLs.
-- MediaWiki supports up to 50 titles separated by pipes in one request.
local function fetchImageUrls(titles, width)
	local out = {}
	if not titles or #titles == 0 then
		return out
	end
	local w = width or getImageWidth()
	local batchSize = 50
	for i = 1, #titles, batchSize do
		local batch = {}
		for j = i, math.min(i + batchSize - 1, #titles) do
			batch[#batch + 1] = titles[j]
		end
		local joined = table.concat(batch, "|")
		local data = apiGet({
			action = "query",
			titles = joined,
			prop = "imageinfo",
			iiprop = "url",
			iiurlwidth = w,
			format = "json",
		})
		if data and data.query and data.query.pages then
			for _, page in pairs(data.query.pages) do
				if page.title and page.imageinfo and page.imageinfo[1] then
					local info = page.imageinfo[1]
					out[page.title] = info.thumburl or info.url or ""
				end
			end
		end
	end
	return out
end

-- Natural sort comparator for filenames and numbers (e.g. 1905-10-15 before 1905-10-22, page 2 before 10).
local function naturalCompare(a, b)
	local function pad(digits)
		return string.format("%08d", tonumber(digits) or 0)
	end
	local na = a:gsub("(%d+)", pad):lower()
	local nb = b:gsub("(%d+)", pad):lower()
	return na < nb
end

-- Clean up a Wikimedia Commons file or category title for user display.
local function cleanTitle(title)
	local s = title:gsub("^Category:", ""):gsub("^File:", "")
	-- Strip file extensions
	s = s:gsub("%.[jJ][pP][gG]$", ""):gsub("%.[jJ][pP][eE][gG]$", "")
	s = s:gsub("%.[pP][nN][gG]$", ""):gsub("%.[wW][eE][bB][pP]$", "")
	s = s:gsub("%.[tT][iI][fF][fF]?$", "")
	s = s:gsub("_", " ")
	return s
end

-- Check if a filename is an image scan suitable for comic reading.
local function isImageFile(title)
	local lower = title:lower()
	return lower:match("%.jpe?g$") ~= nil or lower:match("%.png$") ~= nil
		or lower:match("%.webp$") ~= nil or lower:match("%.tiff?$") ~= nil
end

return {
	api = 1,
	id = "wikimedia-commons",
	name = "Public domain comics (Wikimedia Commons)",
	lang = "en",
	version = "1.0.0",
	nsfw = false,
	baseUrl = "https://commons.wikimedia.org",
	headers = {
		["User-Agent"] = USER_AGENT,
	},

	-- Popular: the curated public domain series list with cover art fetched from Wikimedia Commons.
	popular = function(page)
		if page > 1 then
			return { items = {}, more = false }
		end
		-- Collect cover filenames to resolve thumbnail URLs in one batch call
		local coverFiles = {}
		for _, s in ipairs(SERIES) do
			if s.coverFile and s.coverFile ~= "" then
				coverFiles[#coverFiles + 1] = s.coverFile
			end
		end
		local coverUrls = fetchImageUrls(coverFiles, "320")

		local items = {}
		for _, s in ipairs(SERIES) do
			items[#items + 1] = {
				key = s.id,
				title = s.title,
				cover = coverUrls[s.coverFile] or "",
			}
		end
		return { items = items, more = false }
	end,

	-- Latest: the public domain series ordered by publication year (most recent first).
	latest = function(page)
		if page > 1 then
			return { items = {}, more = false }
		end
		local coverFiles = {}
		for _, s in ipairs(SERIES) do
			if s.coverFile and s.coverFile ~= "" then
				coverFiles[#coverFiles + 1] = s.coverFile
			end
		end
		local coverUrls = fetchImageUrls(coverFiles, "320")

		local sorted = {}
		for _, s in ipairs(SERIES) do
			sorted[#sorted + 1] = s
		end
		table.sort(sorted, function(a, b) return (a.year or 0) > (b.year or 0) end)

		local items = {}
		for _, s in ipairs(sorted) do
			items[#items + 1] = {
				key = s.id,
				title = s.title,
				cover = coverUrls[s.coverFile] or "",
			}
		end
		return { items = items, more = false }
	end,

	-- Search: search within the curated series catalogue or query Wikimedia Commons categories.
	search = function(query, page, filters)
		if page > 1 then
			return { items = {}, more = false }
		end
		local q = (query or ""):gsub("^%s+", ""):gsub("%s+$", ""):lower()

		-- First, search the curated public-domain series
		local matches = {}
		for _, s in ipairs(SERIES) do
			local matched = false
			if q == "" then
				matched = true
			elseif s.title:lower():find(q, 1, true)
				or s.author:lower():find(q, 1, true)
				or s.description:lower():find(q, 1, true) then
				matched = true
			else
				for _, g in ipairs(s.genres or {}) do
					if g:lower():find(q, 1, true) then
						matched = true
						break
					end
				end
			end
			if matched then
				matches[#matches + 1] = s
			end
		end

		-- Handle filters (sort order)
		if filters and type(filters.sort) == "table" and filters.sort.by then
			local by = filters.sort.by
			local asc = filters.sort.asc
			if by == "title" then
				table.sort(matches, function(a, b)
					if asc then return a.title < b.title else return a.title > b.title end
				end)
			elseif by == "year" then
				table.sort(matches, function(a, b)
					if asc then return (a.year or 0) < (b.year or 0) else return (a.year or 0) > (b.year or 0) end
				end)
			end
		end

		if #matches > 0 then
			local coverFiles = {}
			for _, s in ipairs(matches) do
				if s.coverFile and s.coverFile ~= "" then
					coverFiles[#coverFiles + 1] = s.coverFile
				end
			end
			local coverUrls = fetchImageUrls(coverFiles, "320")
			local items = {}
			for _, s in ipairs(matches) do
				items[#items + 1] = {
					key = s.id,
					title = s.title,
					cover = coverUrls[s.coverFile] or "",
				}
			end
			return { items = items, more = false }
		end

		-- Fallback: query Wikimedia Commons category search if query did not match curated list
		local data = apiGet({
			action = "query",
			list = "search",
			srsearch = query .. " comic",
			srnamespace = "14",	-- category namespace
			srlimit = "20",
			format = "json",
		})
		local items = {}
		if data and data.query and data.query.search then
			for _, hit in ipairs(data.query.search) do
				local catTitle = hit.title
				if not catTitle:match("^Category:") then
					catTitle = "Category:" .. catTitle
				end
				items[#items + 1] = {
					key = catTitle,
					title = cleanTitle(catTitle),
					cover = "",
				}
			end
		end
		return { items = items, more = false }
	end,

	-- Filters: sort options for browsing the collection.
	filters = function()
		return {
			{
				id = "sort",
				type = "sort",
				name = "Sort",
				options = {
					{ id = "popular", name = "Popular" },
					{ id = "year", name = "Year" },
					{ id = "title", name = "Title" },
				},
				default = { by = "popular", asc = false },
			},
		}
	end,

	-- Details: metadata for a selected series, including author, genres, synopsis and cover art.
	details = function(manga)
		local s = findSeries(manga.key)
		if s then
			local coverUrl = ""
			if s.coverFile and s.coverFile ~= "" then
				local urls = fetchImageUrls({ s.coverFile }, "400")
				coverUrl = urls[s.coverFile] or ""
			end
			return {
				title = s.title,
				author = s.author,
				artist = s.artist,
				description = s.description,
				genres = s.genres,
				status = s.status,
				cover = coverUrl,
			}
		end

		-- Dynamic category details fallback
		local title = cleanTitle(manga.key)
		return {
			title = title,
			author = "Public Domain",
			artist = "Various",
			description = "Public domain media from " .. manga.key .. " on Wikimedia Commons.",
			genres = { "Comic Strip", "Public Domain" },
			status = "completed",
			cover = "",
		}
	end,

	-- Chapters: returns chapters for a series (one chapter per year or per strip, whichever the category gives).
	-- Design 5.2 specifies newest first.
	chapters = function(manga)
		local s = findSeries(manga.key)
		local category = s and s.category or manga.key
		if not category:match("^Category:") then
			category = "Category:" .. category
		end
		local mode = s and s.mode or "auto"

		-- Mode "subcat" (or auto): check for year subcategories (e.g. Category:Little Nemo in Slumberland, 1905)
		if mode == "subcat" or mode == "auto" then
			local data = apiGet({
				action = "query",
				list = "categorymembers",
				cmtitle = category,
				cmtype = "subcat",
				cmlimit = "50",
				format = "json",
			})
			if data and data.query and data.query.categorymembers and #data.query.categorymembers > 0 then
				local chaps = {}
				for _, sub in ipairs(data.query.categorymembers) do
					local year = sub.title:match("(%d%d%d%d)")
					local num = tonumber(year) or 0
					local name = year and ("Year " .. year) or cleanTitle(sub.title)
					chaps[#chaps + 1] = {
						key = sub.title,
						name = name,
						number = num,
						group = "Wikimedia Commons",
					}
				end
				-- Newest first per design 5.2
				table.sort(chaps, function(a, b)
					if a.number ~= b.number then
						return a.number > b.number
					end
					return a.name > b.name
				end)
				return chaps
			end
		end

		-- Mode "file" (or fallback when no subcategories): strips directly in the category
		local data = apiGet({
			action = "query",
			list = "categorymembers",
			cmtitle = category,
			cmtype = "file",
			cmlimit = "100",
			format = "json",
		})
		local chaps = {}
		if data and data.query and data.query.categorymembers then
			for idx, file in ipairs(data.query.categorymembers) do
				if isImageFile(file.title) then
					local name = cleanTitle(file.title)
					local year = file.title:match("(%d%d%d%d)")
					local num = tonumber(year) or idx
					chaps[#chaps + 1] = {
						key = file.title,
						name = name,
						number = num,
						group = "Wikimedia Commons",
					}
				end
			end
			-- Sort newest / latest date first
			table.sort(chaps, function(a, b)
				return naturalCompare(b.name, a.name)
			end)
		end
		return chaps
	end,

	-- Pages: resolves scaled page image URLs for a chapter using imageinfo and iiurlwidth.
	pages = function(chapter)
		local key = chapter.key or ""
		local width = getImageWidth()

		-- Case 1: Chapter is a subcategory of comic strip pages (e.g. Category:Little Nemo in Slumberland, 1905)
		if key:match("^Category:") then
			local data = apiGet({
				action = "query",
				list = "categorymembers",
				cmtitle = key,
				cmtype = "file",
				cmlimit = "100",
				format = "json",
			})
			if not data or not data.query or not data.query.categorymembers then
				return {}
			end
			local fileTitles = {}
			for _, file in ipairs(data.query.categorymembers) do
				if isImageFile(file.title) then
					fileTitles[#fileTitles + 1] = file.title
				end
			end
			-- Sort files in chronological reading order (oldest strip first)
			table.sort(fileTitles, naturalCompare)

			-- Fetch scaled thumbnail URLs using imageinfo with iiurlwidth
			local urls = fetchImageUrls(fileTitles, width)
			local pages = {}
			for _, title in ipairs(fileTitles) do
				local u = urls[title]
				if u and u ~= "" then
					pages[#pages + 1] = {
						url = u,
						key = title,
					}
				end
			end
			return pages
		end

		-- Case 2: Chapter is an individual strip file
		if key:match("^File:") then
			local urls = fetchImageUrls({ key }, width)
			local u = urls[key]
			if u and u ~= "" then
				return { { url = u, key = key } }
			end
		end

		return {}
	end,

	-- Image: resolves a page table given as { key = ... } into a direct URL.
	image = function(page)
		if page.url and page.url ~= "" then
			return { url = page.url, headers = { ["User-Agent"] = USER_AGENT } }
		end
		if page.key and page.key ~= "" then
			local urls = fetchImageUrls({ page.key }, getImageWidth())
			local u = urls[page.key] or ""
			return { url = u, headers = { ["User-Agent"] = USER_AGENT } }
		end
		return { url = "", headers = { ["User-Agent"] = USER_AGENT } }
	end,

	-- User settings: configurable page image width.
	prefs = {
		{
			id = "image_width",
			type = "select",
			name = "Image Quality (Width)",
			options = { "1280", "1024", "800", "1600" },
			default = "1280",
		},
	},
}
