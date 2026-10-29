#pragma once

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

// Application-level operations shared by the CLI and the GUI. Everything above
// this layer is presentation; everything below is engine.
namespace seicure::job {

struct Options {
    std::wstring out_dir;         // empty: write beside each input
    bool         archive = false; // bundle all inputs into one container
    bool         force   = false; // allow overwriting existing files
};

enum class Event { Info, Success, Error };

using LogFn = std::function<void(Event, const std::wstring&)>;

// Reports progress for the current item. Return false to cancel.
using ProgressFn = std::function<bool(int percent, const std::wstring& label)>;

struct Result {
    int succeeded = 0;
    int failed    = 0;
    bool cancelled = false;
};

Result encrypt(const std::vector<std::wstring>& inputs,
               const std::uint8_t* password, std::size_t password_len,
               const Options& options,
               const LogFn& log, const ProgressFn& progress);

Result decrypt(const std::vector<std::wstring>& inputs,
               const std::uint8_t* password, std::size_t password_len,
               const Options& options,
               const LogFn& log, const ProgressFn& progress);

// Human-readable byte count, e.g. "12.4 KB".
std::wstring format_size(std::uint64_t bytes);

}  // namespace seicure::job
