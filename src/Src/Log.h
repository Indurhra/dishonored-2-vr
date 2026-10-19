#pragma once
#include <string>
#include <vector>
#include <fmt/format.h>

namespace Log
{
    void Init(const std::wstring& dir);   //!< opens <dir>\DishonoredVR.log (truncates)
    void Write(const std::string& line);
    std::vector<std::string> Tail(size_t n); //!< last n lines (for the overlay)
    template <class... A> void Info(fmt::format_string<A...> f, A&&... a) { Write(fmt::format(f, std::forward<A>(a)...)); }
}
#define LOGI(...) ::Log::Info(__VA_ARGS__)
