#include "ampwatch.h"
#include <cerrno>
#include <cstring>
#include <fcntl.h>
#include <poll.h>
#include <unistd.h>

namespace bp
{
	bool AmpWatch::start(std::function<void(const std::string&)> cb)
	{
#ifdef __linux__
		onFail = std::move(cb);
		fd = open("/dev/kmsg", O_RDONLY | O_NONBLOCK | O_CLOEXEC);
		if (fd < 0) return false;	// (not root, or no such log: nothing to watch)
		lseek(fd, 0, SEEK_END);	// only what comes from now on (the boot's old failures aren't news)
		if (pipe(stopPipe) < 0)
		{
			close(fd);
			fd = -1;
			return false;
		}
		thread = std::thread([this] { run(); });
		return true;
#else
		(void)cb;
		return false;
#endif
	}

	void AmpWatch::stop()
	{
		if (!thread.joinable()) return;
		char c = 'q';
		if (write(stopPipe[1], &c, 1) < 0) {}
		thread.join();
		close(stopPipe[0]);
		close(stopPipe[1]);
		if (fd >= 0) close(fd);
		fd = -1;
	}

	void AmpWatch::run()
	{
		// each read of /dev/kmsg is one record: "prio,seq,usec,flags;text"
		char buf[2048];
		for (;;)
		{
			pollfd p[2] = {{fd, POLLIN, 0}, {stopPipe[0], POLLIN, 0}};
			if (poll(p, 2, -1) < 0 && errno != EINTR) return;
			if (p[1].revents) return;
			for (;;)
			{
				ssize_t n = read(fd, buf, sizeof buf - 1);
				if (n < 0)
				{
					if (errno == EPIPE) continue;	// records overwritten before we read them: go on with the next
					break;	// (EAGAIN: none left for now)
				}
				buf[n] = 0;
				const char* text = strchr(buf, ';');
				text = text ? text + 1 : buf;
				// the driver's last try ("retry = 4") of starting one amplifier: it won't try again by itself
				if (!strstr(text, "aw88166") || !strstr(text, "device start failed") || !strstr(text, "retry = 4")) continue;
				std::string chip = text;
				size_t colon = chip.find(':');
				chip = colon == std::string::npos ? "aw88166" : chip.substr(0, colon);
				if (onFail) onFail(chip);
			}
		}
	}
}
