// Copy and Move with a network folder at one end or both (Files on the handheld): a file or a folder from the cards to
// a server, from a server to the cards, or from one server folder to another. As treecopy.h does for the cards: the
// whole of it measured first, written under a hidden name, put in place last, and a failure or a cancel takes the half
// copy away again, so nothing half-copied is ever left. A move removes its original only once the copy is whole.
// Runs on a thread of its own (the Ui's file job): the backends are made here, never shared with the browser's.
#pragma once
#include "net.h"
#include <atomic>
#include <cstdint>
#include <functional>
#include <string>

namespace bp
{
	// one end: a path of the cards, or a server's path (as list() takes it) with what logs in to it
	struct XferEnd
	{
		bool net = false;
		std::string path;
		NetServer server;
		std::string password;
	};

	struct XferProgress
	{
		std::atomic<uint64_t>* done = nullptr;
		std::atomic<uint64_t>* total = nullptr;
		const std::atomic<bool>* cancel = nullptr;
		std::function<void(const std::string&)> current;	// the file being written, from the top's folder
	};

	// src copied (move: then removed) into the folder dest as `name` (taken: "name (2)"...; the name used is put
	// back in name). "" when it's done, else a sentence saying what went wrong
	std::string xferCopy(const XferEnd& src, const XferEnd& dest, std::string& name, bool move, XferProgress& pr);
	// a network file or folder renamed in its folder; "" or why not
	std::string xferRename(const XferEnd& target, const std::string& newName);
	// a network file or folder deleted, all that's in it too; "" or why not
	std::string xferDelete(const XferEnd& target, const std::atomic<bool>* cancel);
	// what a network failure means, in a sentence
	std::string xferErrorText(const NetResult& r);
}
