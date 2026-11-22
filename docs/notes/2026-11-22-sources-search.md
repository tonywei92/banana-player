# Sources › Available: Search beside Filter (2026-11-22)

**What.** Available (the installed Lua sources, the extensions, the Lua sources to install: hundreds of rows with a
Java repository) had only Filter (X: Refresh, Languages, Installed only). It has Search now.

**The design.**
- Two compact keys at the tab strip's right end, `lay::tabKeys(tp, 2)`: Search (magnifier, **L3**) and Filter
  (sliders, **X**), each lit while it narrows the list. Left out where the tabs leave no room (the buttons still work).
- Search opens the shared keyboard (`SRC_SEARCH_FIELD`, "Search sources"). The keyboard covers the panel, so "live"
  is its note under the keys: *12 of 340 sources* as each letter is typed (`mangaAvSearchNote`, counted against
  `mstAvTyping`, the filter taken when it opened: the keyboard has no Player).
- Done sets `mstAvQuery`; while it has words, `lay::searchLine(tp)` sits over the rows (`mangaListArea` gives the
  rows under it): the words and the count, **Clear** at the right. B clears first, the next B leaves.
- Search and Filter both apply. One struct says what narrows the list, `Ui::Manga::AvFilter` (languages, Installed
  only, adult, the words), and the three row sources read it: `mangaAvInstalled`, `javaAvailableExtensions(…, query)`,
  `mangaAvLua`. The installed rows ignore the languages (as before) but not the words.
- The match is pure and in bpcore (`manga_ui_logic.h`): `sourceSearchMatch` (every word in one of the fields, ASCII
  capitals apart), `mangaLangKept`, and `sourceListed` (both). `mangaLangName` / `splitLangs` moved there with them.
- The Installed tab is no longer a list of its own (it shows inside Available, on top), so there is nothing else to search.

**Checked.** Build, `core-test` (the match, the two together, `tabKeys` clear of the tabs over a range of widths,
the search line). **Not seen on a screen**: the Mac has no Java engine, and a fresh state has no Manga section to
reach by `--press`; the handheld is where the keys' look, the hint bar's width and the keyboard's note are judged.
