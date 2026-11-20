#include "xfergroup.h"

namespace bp
{
	XferGroupResult xferGroup(const std::vector<std::string>& names, XferProgress& pr, const XferOne& one)
	{
		XferGroupResult out;
		for (size_t i = 0; i < names.size(); i++)
		{
			if (pr.cancel && pr.cancel->load()) { out.cancelled = true; break; }
			pr.doneBase = pr.done ? pr.done->load() : 0;
			pr.totalBase = pr.total ? pr.total->load() : 0;
			std::string name = names[i];
			std::string err = one(i, name, pr);
			if (!err.empty())
			{
				out.error = err;
				out.cancelled = err.rfind("Cancelled", 0) == 0 || (pr.cancel && pr.cancel->load());
				break;
			}
			out.landed.push_back({i, name});
		}
		return out;
	}
}
