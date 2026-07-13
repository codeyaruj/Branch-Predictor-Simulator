#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <stdexcept>
#include <string>
#include <system_error>
#include <utility>
#include <vector>

namespace {

class TestFailure final : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

#define CHECK(condition)                                                      \
    do {                                                                      \
        if (!(condition)) {                                                   \
            throw TestFailure(std::string("CHECK failed: ") + #condition +   \
                              " at line " + std::to_string(__LINE__));        \
        }                                                                     \
    } while (false)

std::string read_file(const std::filesystem::path& path) {
    std::ifstream input(path, std::ios::binary);
    if (!input) {
        throw TestFailure("unable to read test file '" + path.string() + "'");
    }
    const std::string contents{std::istreambuf_iterator<char>(input),
                               std::istreambuf_iterator<char>()};
    std::string normalized;
    normalized.reserve(contents.size());
    for (std::size_t index = 0U; index < contents.size(); ++index) {
        if (contents[index] == '\r' && index + 1U < contents.size() &&
            contents[index + 1U] == '\n') {
            continue;
        }
        normalized += contents[index];
    }
    return normalized;
}

void write_file(const std::filesystem::path& path, const std::string& contents) {
    std::ofstream output(path, std::ios::binary);
    if (!output) {
        throw TestFailure("unable to create test file '" + path.string() + "'");
    }
    output << contents;
    output.close();
    if (!output) {
        throw TestFailure("unable to write test file '" + path.string() + "'");
    }
}

std::string shell_quote(const std::string& value) {
#ifdef _WIN32
    if (value.find('"') != std::string::npos) {
        throw TestFailure("test command argument contains a double quote");
    }
    return '"' + value + '"';
#else
    std::string quoted{"'"};
    for (const char character : value) {
        if (character == '\'') {
            quoted += "'\\''";
        } else {
            quoted += character;
        }
    }
    quoted += '\'';
    return quoted;
#endif
}

class TemporaryDirectory {
public:
    TemporaryDirectory() {
        const auto timestamp =
            std::chrono::high_resolution_clock::now().time_since_epoch().count();
        const auto marker = reinterpret_cast<std::uintptr_t>(this);
        path_ = std::filesystem::temp_directory_path() /
                ("branchsim-cli-test-" + std::to_string(timestamp) + "-" +
                 std::to_string(marker));
        if (!std::filesystem::create_directory(path_)) {
            throw TestFailure("unable to create CLI test directory");
        }
    }

    TemporaryDirectory(const TemporaryDirectory&) = delete;
    TemporaryDirectory& operator=(const TemporaryDirectory&) = delete;

    ~TemporaryDirectory() {
        std::error_code ignored;
        std::filesystem::remove_all(path_, ignored);
    }

    [[nodiscard]] const std::filesystem::path& path() const noexcept {
        return path_;
    }

private:
    std::filesystem::path path_;
};

struct CommandResult {
    int status{0};
    std::string standard_output;
    std::string standard_error;
};

class CliRunner {
public:
    CliRunner(std::filesystem::path executable,
              const std::filesystem::path& temporary_directory)
        : executable_(std::move(executable)),
          temporary_directory_(temporary_directory) {}

    CommandResult run(const std::vector<std::string>& arguments) {
        ++sequence_;
        const auto output_path = temporary_directory_ /
                                 ("stdout-" + std::to_string(sequence_) + ".txt");
        const auto error_path = temporary_directory_ /
                                ("stderr-" + std::to_string(sequence_) + ".txt");

        std::string command = shell_quote(executable_.string());
        for (const auto& argument : arguments) {
            command += " " + shell_quote(argument);
        }
        command += " > " + shell_quote(output_path.string());
        command += " 2> " + shell_quote(error_path.string());

        const int status = std::system(command.c_str());
        return CommandResult{status, read_file(output_path), read_file(error_path)};
    }

private:
    std::filesystem::path executable_;
    std::filesystem::path temporary_directory_;
    std::size_t sequence_{0U};
};

void exact_single_summary(CliRunner& cli, const std::string& simple_trace) {
    const CommandResult result = cli.run({simple_trace});
    CHECK(result.status == 0);
    CHECK(result.standard_error.empty());
    CHECK(result.standard_output ==
          "Predictor: Two-Bit\n"
          "Total branches: 12\n"
          "Taken branches: 7\n"
          "Not-taken branches: 5\n"
          "Correct predictions: 7\n"
          "Incorrect predictions: 5\n"
          "Prediction accuracy: 58.33%\n"
          "Misprediction rate: 41.67%\n"
          "Table entries: 1024\n"
          "Table memory usage: 2048 bits (256 bytes)\n");
}

void exact_comparison(CliRunner& cli, const std::string& simple_trace) {
    const CommandResult result = cli.run({simple_trace, "--compare"});
    CHECK(result.status == 0);
    CHECK(result.standard_error.empty());
    CHECK(result.standard_output ==
          "Predictor                  Correct     Incorrect      Accuracy\n"
          "Always Taken                     7             5        58.33%\n"
          "Always Not Taken                 5             7        41.67%\n"
          "One-Bit                          6             6        50.00%\n"
          "Two-Bit                          7             5        58.33%\n"
          "GShare                           5             7        41.67%\n");
}

void exact_verbose_output(CliRunner& cli,
                          const std::filesystem::path& temporary_directory) {
    const auto trace = temporary_directory / "verbose.trace";
    write_file(trace, "0xA T\n0xA N\n");
    const CommandResult result =
        cli.run({trace.string(), "--predictor", "gshare", "--table-bits", "0",
                 "--history-bits", "3", "--verbose"});
    CHECK(result.status == 0);
    CHECK(result.standard_error.empty());
    CHECK(result.standard_output ==
          "PC=0x0000000A actual=T predicted=N result=MISS index=0 state=1->2 "
          "history=000->001\n"
          "PC=0x0000000A actual=N predicted=T result=MISS index=0 state=2->1 "
          "history=001->010\n"
          "Predictor: GShare\n"
          "Total branches: 2\n"
          "Taken branches: 1\n"
          "Not-taken branches: 1\n"
          "Correct predictions: 0\n"
          "Incorrect predictions: 2\n"
          "Prediction accuracy: 0.00%\n"
          "Misprediction rate: 100.00%\n"
          "Table entries: 1\n"
          "Table memory usage: 2 bits (1 byte)\n"
          "History bits: 3\n"
          "Final global history: 010\n");
}

void csv_export(CliRunner& cli, const std::string& simple_trace,
                const std::filesystem::path& temporary_directory) {
    const auto csv = temporary_directory / "single.csv";
    const CommandResult result = cli.run(
        {simple_trace, "--predictor", "one-bit", "--csv", csv.string()});
    CHECK(result.status == 0);
    CHECK(result.standard_error.empty());
    CHECK(read_file(csv) ==
          "predictor,total,correct,incorrect,accuracy,misprediction_rate\n"
          "One-Bit,12,6,6,50.00,50.00\n");
}

void rejected_inputs(CliRunner& cli, const std::string& simple_trace,
                     const std::filesystem::path& temporary_directory) {
    CommandResult result =
        cli.run({simple_trace, "--compare", "--verbose"});
    CHECK(result.status != 0);
    CHECK(result.standard_error.find("--compare cannot be combined with --verbose") !=
          std::string::npos);

    result = cli.run({simple_trace, "--csv", "--verbose"});
    CHECK(result.status != 0);
    CHECK(result.standard_error.find("--csv requires a value") !=
          std::string::npos);

    const auto malformed = temporary_directory / "malformed.trace";
    write_file(malformed, "# line one\n\nnot-a-branch\n");
    result = cli.run({malformed.string()});
    CHECK(result.status != 0);
    CHECK(result.standard_error.find("line 3") != std::string::npos);
}

void input_overwrite_is_rejected(
    CliRunner& cli, const std::string& simple_trace,
    const std::filesystem::path& temporary_directory) {
    const auto input = temporary_directory / "protected.trace";
    const std::string original = read_file(simple_trace);
    write_file(input, original);

    const CommandResult result =
        cli.run({input.string(), "--csv", input.string()});
    CHECK(result.status != 0);
    CHECK(result.standard_error.find("must not overwrite") != std::string::npos);
    CHECK(read_file(input) == original);
}

void help_without_trace(CliRunner& cli) {
    const CommandResult result = cli.run({"--help"});
    CHECK(result.status == 0);
    CHECK(result.standard_error.empty());
    CHECK(result.standard_output.find("Usage:\n") != std::string::npos);
    CHECK(result.standard_output.find("--compare") != std::string::npos);
}

}  // namespace

int main(int argc, char* argv[]) {
    if (argc != 3) {
        std::cerr << "usage: test_cli <branchsim-executable> <source-directory>\n";
        return 2;
    }

    TemporaryDirectory temporary_directory;
    CliRunner cli(std::filesystem::absolute(argv[1]), temporary_directory.path());
    const std::string simple_trace =
        (std::filesystem::absolute(argv[2]) / "traces/simple.trace").string();

    std::size_t passed = 0U;
    const auto run = [&passed](const std::string& name, const auto& test) {
        try {
            test();
            ++passed;
        } catch (const std::exception& error) {
            std::cerr << "FAIL: " << name << ": " << error.what() << '\n';
        }
    };

    run("single summary", [&] { exact_single_summary(cli, simple_trace); });
    run("comparison", [&] { exact_comparison(cli, simple_trace); });
    run("verbose output", [&] {
        exact_verbose_output(cli, temporary_directory.path());
    });
    run("CSV export", [&] {
        csv_export(cli, simple_trace, temporary_directory.path());
    });
    run("rejected inputs", [&] {
        rejected_inputs(cli, simple_trace, temporary_directory.path());
    });
    run("input overwrite protection", [&] {
        input_overwrite_is_rejected(cli, simple_trace,
                                    temporary_directory.path());
    });
    run("help", [&] { help_without_trace(cli); });

    constexpr std::size_t expected_tests = 7U;
    if (passed != expected_tests) {
        std::cerr << passed << '/' << expected_tests << " CLI tests passed\n";
        return 1;
    }

    std::cout << "All " << passed << " CLI tests passed\n";
    return 0;
}
