export module libhello.tests;

import std.core;

export void TestHello(std::string_view text)
{
	std::printf("Hello, test %s!\n", text.data());
}
