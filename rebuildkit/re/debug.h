#pragma once
#include <fmt/format.h>
#include <string>
#include <chrono>

#define RE_TRACE(format, ...) // fmt::print(format, __VA_ARGS__)

namespace re
{
	class PerfProfile
	{
	public:
		PerfProfile(const std::string& name)
			: mName{ name }
		{}

		~PerfProfile()
		{
			if (!mFinished)
				Finish();
			if (!mResultsPrintedOnce)
				Print();
		}

		void Finish();

		void Print();

	private:
		std::string mName;

		std::chrono::high_resolution_clock::time_point mBeginTime = std::chrono::high_resolution_clock::now();
		std::chrono::high_resolution_clock::time_point mEndTime;

		bool mFinished = false;
		bool mResultsPrintedOnce = false;
	};
}
