export module libhello.greeting;

import std.core;

export void GreetSomeone(std::string_view template_path, std::string_view name)
{
	std::ifstream t{ template_path.data() };
	std::string str((std::istreambuf_iterator<char>(t)),
		std::istreambuf_iterator<char>());

	std::printf(str.data(), name.data());
}
