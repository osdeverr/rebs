#include "debug.h"

#include <fmt/color.h>
#include <ulib/string.h>

namespace re
{
	void PerfProfile::Finish()
	{
		mEndTime = std::chrono::high_resolution_clock::now();
		mFinished = true;
	}

	void PerfProfile::Print()
	{
		return;

		const auto style = fmt::emphasis::bold | fg(fmt::color::green_yellow);

		fmt::print(
			style,
			"\n *** {}\n       {} {}\n\n", mName, mFinished ? "finished in" : "running for", ToString()
		);
	}

	ulib::string PerfProfile::ToString() const
	{
		auto point = mFinished ? mEndTime : std::chrono::high_resolution_clock::now();
		auto delta = point - mBeginTime;

		auto ns = std::chrono::duration_cast<std::chrono::nanoseconds>(delta).count();
		auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(delta).count();
		auto s = ms / 1000.f;

		return ulib::format(
			"{}s / {}ms / {}ns", s, ms, ns
		);
	}
}
