# Manga Test Repository (Wikimedia Commons)

A lightweight test repository for Banana Player's Manga Reader extension (branch `manga-reader`).
This repository provides a clean, fully functional source for testing repositories, sources,
browsing, details, chapter navigation, reading, and downloading end-to-end on the handheld
with content that is completely free to use.

---

## Content and Licensing

All media and comics provided by this repository are hosted on **[Wikimedia Commons](https://commons.wikimedia.org)**
and are in the **public domain** (all original works published in the United States prior to 1928 are free of copyright):

- **Little Nemo in Slumberland** by Winsor McCay (*New York Herald*, 1905–1911; `Category:Little Nemo in Slumberland`)
- **Dream of the Rarebit Fiend** by Winsor McCay (*New York Evening Telegram*, 1904–1913; `Category:Dream of the Rarebit Fiend`)
- **Krazy Kat** by George Herriman (1913–1928 strips; `Category:Krazy Kat comics`)
- **The Yellow Kid** (*Hogan's Alley*) by Richard F. Outcault (*New York World*, 1895–1898; `Category:Yellow Kid`)
- **Buster Brown** by Richard F. Outcault (*New York Herald*, 1902–1920; `Category:Buster Brown`)

The source queries the public MediaWiki API over HTTPS (`https://commons.wikimedia.org/w/api.php`) using:
- `categorymembers` to list series subcategories (years) or strip files.
- `imageinfo` with `iiurlwidth` (default 1280px) to fetch scaled, bandwidth-friendly page thumbnails.
- A descriptive `User-Agent` header (`BananaPlayer-MangaTestSource/1.0 (...)`) compliant with Wikimedia's API policy.

No API keys, accounts, or credentials are required.

---

## How to Add on the Handheld

1. **On the handheld:**
   - Open **Extensions** from Home's fourth tile.
   - Open **Manga reader** (turn it on in **Manage extensions** if needed).
   - Press **R** to navigate to the **Sources** tab.
   - Focus **Repositories** and select **Add repository** (or press **A**).
   - Enter the repository URL:
     ```text
     https://raw.githubusercontent.com/tonywei92/banana-player/manga-reader/tools/manga-test-repo/index.json
     ```

2. **Via the Web Dashboard:**
   - Connect to the handheld's dashboard in a browser: `http://<handheld-ip>:8093/`
   - Go to **Manga** › **Sources** › **Repositories**.
   - Paste the repository URL above into the field and click **Add**.

3. **Install the source:**
   - The handheld fetches and validates `index.json`.
   - **Public domain comics (Wikimedia Commons)** will appear under **Available** sources.
   - Select it and press **Install**. The script is downloaded, verified against its SHA-256 digest, validated in the sandbox, and installed.
   - Open **Browse** to start reading!

---

## Repository Index Format

The repository index (`index.json`) strictly conforms to `bp::parseRepoIndex` (`src/mangarepo.h`):

```json
{
  "format": 1,
  "name": "Banana Player Manga Test Repository",
  "homepage": "https://github.com/tonywei92/banana-player",
  "sources": [
    {
      "id": "wikimedia-commons",
      "name": "Public domain comics (Wikimedia Commons)",
      "version": "1.0.0",
      "lang": "en",
      "file": "wikimedia-commons.lua",
      "sha256": "db224978dfcadbb3c15dab21030afb946134492fdc0053dc4db768c703ff9d3d",
      "size": 16791,
      "api": 1,
      "nsfw": false,
      "description": "Public domain comic strips from Wikimedia Commons including Little Nemo in Slumberland, Dream of the Rarebit Fiend, Krazy Kat, The Yellow Kid, and Buster Brown."
    }
  ]
}
```

- `format`: Must be `1` (newer formats require a newer Banana Player).
- `file`: Path relative to `index.json` (or an absolute HTTPS URL).
- `sha256`: Lowercase 64-character hexadecimal SHA-256 digest of the script file.
- `size`: Size of the script file in bytes.

---

## Computing and Verifying SHA-256 Checksums

During installation, Banana Player verifies that the downloaded script's SHA-256 matches the `sha256` listed in `index.json` before loading it into the sandbox (`MangaWorker::validateScript`).

To verify or update the SHA-256 digest when modifying `wikimedia-commons.lua`:

### 1. Using the Test Program (`worker-test`)
The test suite includes a validation check in `tests/worker_test.cpp` that reads `tools/manga-test-repo/wikimedia-commons.lua`, computes its SHA-256 using `bp::sha256Hex`, and verifies that `index.json` matches:

```sh
cmake --build build --target worker-check
```

### 2. Using Command-Line Tools
On standard POSIX shells with sha256 utilities:
```sh
# On Linux:
sha256sum tools/manga-test-repo/wikimedia-commons.lua

# On macOS:
shasum -a 256 tools/manga-test-repo/wikimedia-commons.lua
```

Update both `sha256` and `size` in `index.json` whenever `wikimedia-commons.lua` is modified.
