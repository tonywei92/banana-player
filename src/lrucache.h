#pragma once
// A cache by string key that, when full, lets go of the entry used longest ago (and only that one), each value let go
// through `drop`: Ui's text textures (a full cache used to destroy all 256 at once, and the next frame rendered every
// line it showed again through FreeType). find() counts as a use. Tests: core_test (testLruCache).
#include <cstddef>
#include <functional>
#include <list>
#include <string>
#include <unordered_map>
#include <utility>

namespace bp
{
	template <class V>
	class LruCache
	{
	public:
		LruCache(size_t capacity, std::function<void(V)> drop) : cap(capacity ? capacity : 1), dropFn(std::move(drop)) {}
		~LruCache() { clear(); }
		LruCache(const LruCache&) = delete;
		LruCache& operator=(const LruCache&) = delete;

		// the value, now the most recently used; null when absent
		V* find(const std::string& key)
		{
			auto it = index.find(key);
			if (it == index.end()) return nullptr;
			order.splice(order.begin(), order, it->second);
			return &it->second->second;
		}
		// in (a value already under the key is dropped), then the oldest let go while over the capacity
		void put(const std::string& key, V value)
		{
			auto it = index.find(key);
			if (it != index.end())
			{
				dropFn(it->second->second);
				order.erase(it->second);
				index.erase(it);
			}
			order.emplace_front(key, std::move(value));
			index[key] = order.begin();
			while (order.size() > cap)
			{
				auto& last = order.back();
				dropFn(last.second);
				index.erase(last.first);
				order.pop_back();
			}
		}
		// every entry whose key matches, dropped (a theme change's PIN digits)
		template <class Pred>
		void eraseIf(Pred pred)
		{
			for (auto it = order.begin(); it != order.end();)
			{
				if (!pred(it->first)) { ++it; continue; }
				dropFn(it->second);
				index.erase(it->first);
				it = order.erase(it);
			}
		}
		void clear()
		{
			for (auto& e : order) dropFn(e.second);
			order.clear();
			index.clear();
		}
		size_t size() const { return order.size(); }

	private:
		size_t cap;
		std::function<void(V)> dropFn;
		std::list<std::pair<std::string, V>> order;	// the most recently used first
		std::unordered_map<std::string, typename std::list<std::pair<std::string, V>>::iterator> index;
	};
}
