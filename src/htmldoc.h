// bp.html for the manga source scripts (docs/design/manga.md 5.2): parse a page, find nodes by CSS selector.
// lexbor sits behind this so a source script's host, and everything else, needs nothing from third_party/.
#pragma once
#include <string>
#include <vector>

namespace bp
{
	class HtmlDoc;

	// a light handle into HtmlDoc's tree: valid while that document lives, and dead the moment it is re-parsed
	class HtmlNode
	{
	public:
		HtmlNode() = default;

		bool valid() const { return node_ != nullptr; }
		// the node's and its descendants' text, entities resolved, whitespace runs collapsed to one space, trimmed
		std::string text() const;
		std::string attr(const std::string& name) const;	// "" when the node has no such attribute
		std::string html() const;	// the node as HTML, itself included: outerHTML
		std::string tag() const;	// lower-case name ("a"), "" when this is no element
		std::vector<HtmlNode> select(const std::string& css) const;	// only inside this node; never it itself
		HtmlNode selectFirst(const std::string& css) const;
		// the tree as it is, for a walk in document order (an EPUB's chapter read into paragraphs): the node's
		// children (elements and text alike), and a text node's own characters, whitespace as written
		std::vector<HtmlNode> children() const;
		bool isText() const;
		std::string rawText() const;

	private:
		friend class HtmlDoc;
		explicit HtmlNode(void* node) : node_(node) {}	// node_ is an lxb_dom_node_t*, hidden so the header stays bare
		void* node_ = nullptr;
	};

	// parse once, query often; the nodes of a select point into this document's tree
	class HtmlDoc
	{
	public:
		HtmlDoc();
		~HtmlDoc();
		HtmlDoc(const HtmlDoc&) = delete;
		HtmlDoc& operator=(const HtmlDoc&) = delete;

		// false when the page is over 8 MB (a runaway response: refuse it whole) or the parser fails; a refused or
		// failed parse leaves the document empty, and a new parse invalidates the nodes of the old one
		bool parse(const std::string& html);
		// a selector that does not parse is no nodes, never a crash; at most 20000 come back, in document order
		std::vector<HtmlNode> select(const std::string& css) const;
		HtmlNode selectFirst(const std::string& css) const;

	private:
		static std::vector<HtmlNode> selectFrom(void* root, const std::string& css, bool firstOnly);
		friend class HtmlNode;	// its select() runs through selectFrom
		struct Impl;
		Impl* impl_;
	};
}
