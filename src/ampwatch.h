#pragma once
#include <functional>
#include <string>
#include <thread>

namespace bp
{
	// The RG DS Plus's speakers are two aw88166 amplifiers, each started by its driver as a sound stream starts. Now and
	// then one (or both) can't lock onto the stream's clock ("pll check failed cannot start ... device start failed.
	// retry = 4" in the kernel log): its driver gives up, and that speaker stays silent till the next stream starts.
	// Sound on the left only, or none. This watches the kernel's log (/dev/kmsg, from now on) for that last failure of
	// a start and tells the player, which opens its sound output again (main.cpp). Linux only; elsewhere start()
	// returns false. onFail(chip) runs on the watch's own thread.
	class AmpWatch
	{
	public:
		bool start(std::function<void(const std::string&)> onFail);
		void stop();
		~AmpWatch() { stop(); }

	private:
		void run();
		std::function<void(const std::string&)> onFail;
		std::thread thread;
		int fd = -1;
		int stopPipe[2] = {-1, -1};
	};
}
