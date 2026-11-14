// bp.html on lexbor: the document parse, and each query as one CSS parse plus one selectors run, every lexbor object
// freed before the call returns. lexbor is C: it gets raw pointers, freed on every path, here and nowhere else.
#include "htmldoc.h"

#include "lexbor/css/parser.h"
#include "lexbor/css/selectors/selectors.h"
#include "lexbor/dom/dom.h"
#include "lexbor/html/html.h"
#include "lexbor/selectors/selectors.h"

namespace bp
{
	namespace
	{
		const size_t kMaxInput = 8u * 1024u * 1024u;	// a page's source is tens of KB; more is a runaway response
		const size_t kMaxNodes = 20000;	// the same guard on the way out, so a "*" can't eat the heap

		bool isWs(unsigned char c) { return c == ' ' || (c >= 0x09 && c <= 0x0D); }	// space, \t…\r

		// whitespace runs become one space, ends dropped: what a browser shows as the text
		std::string collapsed(const lxb_char_t* data, size_t len)
		{
			std::string out;
			out.reserve(len);
			bool pending = false;
			for (size_t i = 0; i < len; i++)
			{
				if (isWs(data[i]))
				{
					pending = !out.empty();	// leading whitespace is dropped with it
					continue;
				}
				if (pending) out.push_back(' ');
				pending = false;
				out.push_back(static_cast<char>(data[i]));
			}
			return out;
		}

		struct Collected
		{
			std::vector<void*>* nodes;
			bool firstOnly;
		};

		lxb_status_t collect(lxb_dom_node_t* node, lxb_css_selector_specificity_t, void* ctx)
		{
			Collected& c = *static_cast<Collected*>(ctx);
			c.nodes->push_back(node);	// wrapped into HtmlNodes by HtmlDoc::selectFrom, the handle's friend
			// STOP ends the walk; lxb_selectors_tree cleans up and returns OK, so nothing half-done is left
			if (c.firstOnly || c.nodes->size() >= kMaxNodes) return LXB_STATUS_STOP;
			return LXB_STATUS_OK;
		}
	}

	struct HtmlDoc::Impl
	{
		lxb_html_document_t* doc = nullptr;
	};

	HtmlDoc::HtmlDoc() : impl_(new Impl()) {}

	HtmlDoc::~HtmlDoc()
	{
		if (impl_->doc) lxb_html_document_destroy(impl_->doc);
		delete impl_;
	}

	bool HtmlDoc::parse(const std::string& html)
	{
		if (impl_->doc) { lxb_html_document_destroy(impl_->doc); impl_->doc = nullptr; }	// re-parse: the old tree goes first
		if (html.size() > kMaxInput) return false;	// refused whole, and still empty, not half a page
		lxb_html_document_t* doc = lxb_html_document_create();
		if (doc == nullptr) return false;
		if (lxb_html_document_parse(doc, reinterpret_cast<const lxb_char_t*>(html.data()), html.size())
			!= LXB_STATUS_OK)
		{
			lxb_html_document_destroy(doc);
			return false;
		}
		impl_->doc = doc;
		return true;
	}

	std::vector<HtmlNode> HtmlDoc::selectFrom(void* rootv, const std::string& css, bool firstOnly)
	{
		std::vector<HtmlNode> out;
		lxb_dom_node_t* root = static_cast<lxb_dom_node_t*>(rootv);
		if (root == nullptr || css.empty()) return out;

		lxb_css_parser_t* parser = lxb_css_parser_create();
		if (parser == nullptr) return out;
		if (lxb_css_parser_init(parser, nullptr) != LXB_STATUS_OK)
		{
			lxb_css_parser_destroy(parser, true);
			return out;
		}

		// a selector that does not parse comes back NULL, and the parser has freed what it made for it already
		lxb_css_selector_list_t* list = lxb_css_selectors_parse(parser,
			reinterpret_cast<const lxb_char_t*>(css.data()), css.size());
		if (list == nullptr)
		{
			lxb_css_parser_destroy(parser, true);
			return out;
		}

		lxb_selectors_t* sel = lxb_selectors_create();
		if (sel != nullptr && lxb_selectors_init(sel) == LXB_STATUS_OK)
		{
			// MATCH_FIRST: with "a, .x" a node both lists match would arrive once per list; we want each node once
			lxb_selectors_opt_set(sel, LXB_SELECTORS_OPT_MATCH_FIRST);
			std::vector<void*> found;
			Collected c{&found, firstOnly};
			lxb_selectors_find(sel, root, list, collect, &c);
			out.reserve(found.size());
			for (void* n : found) out.push_back(HtmlNode(n));
		}
		if (sel) lxb_selectors_destroy(sel, true);

		// the lists — every one of a comma list and their strings — live in one memory pool; one call frees the lot
		lxb_css_selector_list_destroy_memory(list);
		// what lxb_css_parser_destroy does not touch is exactly that pool, so there is no double free here
		lxb_css_parser_destroy(parser, true);
		return out;
	}

	std::vector<HtmlNode> HtmlDoc::select(const std::string& css) const
	{
		// the document node itself: lexbor walks its children, so no root option is needed
		return selectFrom(impl_->doc ? lxb_dom_interface_node(impl_->doc) : nullptr, css, false);
	}

	HtmlNode HtmlDoc::selectFirst(const std::string& css) const
	{
		std::vector<HtmlNode> v = selectFrom(impl_->doc ? lxb_dom_interface_node(impl_->doc) : nullptr, css, true);
		return v.empty() ? HtmlNode() : v.front();
	}

	std::vector<HtmlNode> HtmlNode::select(const std::string& css) const
	{
		return HtmlDoc::selectFrom(node_, css, false);
	}

	HtmlNode HtmlNode::selectFirst(const std::string& css) const
	{
		std::vector<HtmlNode> v = HtmlDoc::selectFrom(node_, css, true);
		return v.empty() ? HtmlNode() : v.front();
	}

	std::string HtmlNode::text() const
	{
		if (node_ == nullptr) return "";
		size_t len = 0;
		// lexbor allocates the collected text from the document; taken back right after the copy
		lxb_char_t* raw = lxb_dom_node_text_content(static_cast<lxb_dom_node_t*>(node_), &len);
		if (raw == nullptr) return "";
		lxb_dom_node_t* node = static_cast<lxb_dom_node_t*>(node_);
		std::string out = collapsed(raw, len);
		lxb_dom_document_destroy_text(node->owner_document, raw);
		return out;
	}

	std::string HtmlNode::attr(const std::string& name) const
	{
		lxb_dom_node_t* node = static_cast<lxb_dom_node_t*>(node_);
		if (node == nullptr || node->type != LXB_DOM_NODE_TYPE_ELEMENT) return "";
		// the HTML parser stores attribute names lower-case; a browser matches them case-insensitively
		std::string want = name;
		for (char& c : want) if (c >= 'A' && c <= 'Z') c = static_cast<char>(c + ('a' - 'A'));
		size_t vlen = 0;
		const lxb_char_t* v = lxb_dom_element_get_attribute(lxb_dom_interface_element(node),
			reinterpret_cast<const lxb_char_t*>(want.data()), want.size(), &vlen);
		if (v == nullptr) return "";
		return std::string(reinterpret_cast<const char*>(v), vlen);
	}

	std::string HtmlNode::html() const
	{
		lxb_dom_node_t* node = static_cast<lxb_dom_node_t*>(node_);
		if (node == nullptr) return "";
		// the serializer fills a lexbor_str_t from the document's own pool; the tree variant walks node and descendants
		// alike: outerHTML (the plain one would only make the element's start tag)
		lexbor_str_t str;
		str.data = nullptr;
		str.length = 0;
		std::string out;
		if (lxb_html_serialize_tree_str(node, &str) == LXB_STATUS_OK && str.data != nullptr)
			out.assign(reinterpret_cast<const char*>(str.data), str.length);
		if (str.data != nullptr) lexbor_str_destroy(&str, node->owner_document->text, false);
		return out;
	}

	std::string HtmlNode::tag() const
	{
		lxb_dom_node_t* node = static_cast<lxb_dom_node_t*>(node_);
		if (node == nullptr || node->type != LXB_DOM_NODE_TYPE_ELEMENT) return "";
		size_t len = 0;
		const lxb_char_t* name = lxb_dom_element_local_name(lxb_dom_interface_element(node), &len);
		if (name == nullptr) return "";
		return std::string(reinterpret_cast<const char*>(name), len);
	}

	std::vector<HtmlNode> HtmlNode::children() const
	{
		std::vector<HtmlNode> out;
		lxb_dom_node_t* node = static_cast<lxb_dom_node_t*>(node_);
		if (node == nullptr) return out;
		for (lxb_dom_node_t* c = lxb_dom_node_first_child(node); c != nullptr && out.size() < kMaxNodes; c = lxb_dom_node_next(c))
			out.push_back(HtmlNode(c));
		return out;
	}

	bool HtmlNode::isText() const
	{
		lxb_dom_node_t* node = static_cast<lxb_dom_node_t*>(node_);
		return node != nullptr && node->type == LXB_DOM_NODE_TYPE_TEXT;
	}

	std::string HtmlNode::rawText() const
	{
		lxb_dom_node_t* node = static_cast<lxb_dom_node_t*>(node_);
		if (node == nullptr || node->type != LXB_DOM_NODE_TYPE_TEXT) return "";
		size_t len = 0;
		const lxb_char_t* t = lxb_dom_node_text_content(node, &len);
		std::string out = t ? std::string(reinterpret_cast<const char*>(t), len) : "";
		if (t) lxb_dom_document_destroy_text(node->owner_document, const_cast<lxb_char_t*>(t));
		return out;
	}
}
