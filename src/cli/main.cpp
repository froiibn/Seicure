#include <string>
#include <vector>

#include "job/job.hpp"
#include "platform/console.hpp"
#include "platform/file.hpp"

using namespace seicure;
using platform::print;
using platform::print_error;

namespace {

struct Args {
    std::wstring              command;
    std::vector<std::wstring> inputs;
    job::Options              options;
};

void print_usage() {
    print(
        L"Seicure -- file encryption with metadata hiding\n"
        L"\n"
        L"Usage:\n"
        L"  seicure enc [options] <file>...   encrypt\n"
        L"  seicure dec [options] <file>...   decrypt\n"
        L"\n"
        L"Options:\n"
        L"  -o, --out <dir>   output directory (default: beside the input)\n"
        L"      --archive     bundle all inputs into one container\n"
        L"      --individual  one container per input (default)\n"
        L"      --force       allow overwriting existing files\n"
        L"  -h, --help        show this help\n"
        L"\n"
        L"Encrypted files are named with random hex. The original name, size,\n"
        L"timestamps and attributes are stored inside the ciphertext and\n"
        L"restored on decryption.\n");
}

// Returns false when the arguments are unusable; the reason is already printed.
bool parse_args(const std::vector<std::wstring>& argv, Args& out) {
    if (argv.size() < 2) {
        print_usage();
        return false;
    }

    out.command = argv[1];
    if (out.command == L"-h" || out.command == L"--help") {
        print_usage();
        return false;
    }
    if (out.command != L"enc" && out.command != L"dec") {
        print_error(L"error: unknown command '" + out.command + L"'\n");
        return false;
    }

    for (std::size_t i = 2; i < argv.size(); ++i) {
        const std::wstring& arg = argv[i];
        if (arg == L"-o" || arg == L"--out") {
            if (i + 1 >= argv.size()) {
                print_error(L"error: " + arg + L" needs a directory\n");
                return false;
            }
            out.options.out_dir = argv[++i];
        } else if (arg == L"--archive") {
            out.options.archive = true;
        } else if (arg == L"--individual") {
            out.options.archive = false;
        } else if (arg == L"--force") {
            out.options.force = true;
        } else if (arg == L"-h" || arg == L"--help") {
            print_usage();
            return false;
        } else if (!arg.empty() && arg[0] == L'-') {
            print_error(L"error: unknown option '" + arg + L"'\n");
            return false;
        } else {
            out.inputs.push_back(arg);
        }
    }

    if (out.inputs.empty()) {
        print_error(L"error: no input files\n");
        return false;
    }
    if (!out.options.out_dir.empty() &&
        !platform::directory_exists(out.options.out_dir)) {
        print_error(L"error: output directory does not exist: " +
                    out.options.out_dir + L"\n");
        return false;
    }
    return true;
}

class Progress {
public:
    bool operator()(int percent, const std::wstring& label) {
        if (!interactive_) {
            return true;  // redirected: don't litter the output with updates
        }
        if (percent != last_) {
            last_ = percent;
            print(L"\r  " + label + L"  " + std::to_wstring(percent) + L"%   ");
        }
        return true;  // cancellation is a GUI affordance; the CLI runs to done
    }

    void clear() {
        if (interactive_) {
            print(L"\r                                                        \r");
        }
    }

private:
    int  last_ = -1;
    bool interactive_ = platform::stdout_is_console();
};

}  // namespace

int run(const std::vector<std::wstring>& argv) {
    Args args;
    if (!parse_args(argv, args)) {
        return 2;
    }

    try {
        const bool encrypting = args.command == L"enc";
        // Confirm on encryption only: a typo there is unrecoverable, while a
        // typo on decryption just fails harmlessly.
        const SecureBuffer password =
            platform::read_password(L"Password: ", encrypting);
        if (password.empty()) {
            print_error(L"error: empty password\n");
            return 2;
        }

        Progress progress;
        auto log = [&progress](job::Event kind, const std::wstring& text) {
            progress.clear();
            if (kind == job::Event::Error) {
                print_error(L"  " + text + L"\n");
            } else {
                print(L"  " + text + L"\n");
            }
        };

        const job::Result result =
            encrypting
                ? job::encrypt(args.inputs, password.data(), password.size(),
                               args.options, log, std::ref(progress))
                : job::decrypt(args.inputs, password.data(), password.size(),
                               args.options, log, std::ref(progress));
        progress.clear();

        if (result.failed > 0) {
            print_error(L"\n" + std::to_wstring(result.failed) + L" failed\n");
            return 1;
        }
        return 0;
    } catch (const std::exception& e) {
        print_error(L"error: " + platform::utf8_to_wide(e.what()) + L"\n");
        return 1;
    }
}

// Two entry points, one body. Windows hands over UTF-16 directly; POSIX hands
// over bytes that are UTF-8 by convention, so they are widened here rather
// than anywhere deeper in the program.
#ifdef _WIN32
int wmain(int argc, wchar_t** argv) {
    return run(std::vector<std::wstring>(argv, argv + argc));
}
#else
int main(int argc, char** argv) {
    std::vector<std::wstring> args;
    args.reserve(static_cast<std::size_t>(argc));
    for (int i = 0; i < argc; ++i) {
        args.push_back(platform::utf8_to_wide(argv[i]));
    }
    return run(args);
}
#endif
