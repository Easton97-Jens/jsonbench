#ifndef JSONBENCH_TEST_SUPPORT_H
#define JSONBENCH_TEST_SUPPORT_H

#include "test_c_memory.h"

#include <array>
#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>

#include <sys/stat.h>
#include <unistd.h>

namespace jsonbench::test {

inline constexpr auto close_c_file = [](FILE* file) noexcept { std::fclose(file); };
inline constexpr auto free_c_diagnostic = [](char* value) noexcept {
    jsonbench_test_release_diagnostic(value);
};

class TestError final : public std::runtime_error {
  public:
    using std::runtime_error::runtime_error;
};

class FileDescriptor {
    int value_ = -1;
  public:
    explicit FileDescriptor(int value = -1) noexcept : value_(value) {}
    ~FileDescriptor() { if (value_ >= 0) ::close(value_); }
    FileDescriptor(const FileDescriptor&) = delete;
    FileDescriptor& operator=(const FileDescriptor&) = delete;
    int get() const noexcept { return value_; }
    int release() noexcept { return std::exchange(value_, -1); }
    void reset(int value) noexcept {
        if (value_ >= 0) ::close(value_);
        value_ = value;
    }
    void close() {
        const int value = release();
        if (value >= 0 && ::close(value) != 0)
            throw TestError("temporary file close failed");
    }
};

struct DirectoryCleanup {
    std::string path;
    explicit DirectoryCleanup(std::string value) : path(std::move(value)) {}
    ~DirectoryCleanup() { ::rmdir(path.c_str()); }
    DirectoryCleanup(const DirectoryCleanup&) = delete;
    DirectoryCleanup& operator=(const DirectoryCleanup&) = delete;
    DirectoryCleanup(DirectoryCleanup&&) = delete;
    DirectoryCleanup& operator=(DirectoryCleanup&&) = delete;
};

class PrivateWorkspace {
    DirectoryCleanup directory_{create()};

    static std::string create() {
        // mkdtemp creates the directory atomically with owner-only access.
        // Use the task's working directory, without a public temporary root.
        std::string pattern = "./.jsonbench-tests-XXXXXX";
        if (::mkdtemp(pattern.data()) == nullptr)
            throw TestError("private test workspace creation failed");
        return pattern;
    }

  public:
    PrivateWorkspace() {
        struct stat attributes{};
        if (::chmod(directory_.path.c_str(), S_IRWXU) != 0 ||
            ::stat(directory_.path.c_str(), &attributes) != 0 ||
            !S_ISDIR(attributes.st_mode) || attributes.st_uid != ::geteuid() ||
            (attributes.st_mode & 0777) != S_IRWXU)
            throw TestError("private test workspace permissions failed");
    }
    PrivateWorkspace(const PrivateWorkspace&) = delete;
    PrivateWorkspace& operator=(const PrivateWorkspace&) = delete;
    const std::string& path() const noexcept { return directory_.path; }
};

inline const PrivateWorkspace& workspace() {
    static const PrivateWorkspace instance;
    return instance;
}

struct FileCleanup {
    std::string path;
    bool owned = false;
    explicit FileCleanup(std::string value) : path(std::move(value)) {}
    ~FileCleanup() { if (owned) ::unlink(path.c_str()); }
    FileCleanup(const FileCleanup&) = delete;
    FileCleanup& operator=(const FileCleanup&) = delete;
    FileCleanup(FileCleanup&&) = delete;
    FileCleanup& operator=(FileCleanup&&) = delete;
};

class TemporaryFile {
    FileCleanup file_{workspace().path() + "/fixture-XXXXXX"};
    FileDescriptor descriptor_;
  public:
    TemporaryFile() {
        descriptor_.reset(::mkstemp(file_.path.data()));
        if (descriptor_.get() < 0) throw TestError("temporary file creation failed");
        file_.owned = true;
        if (::fchmod(descriptor_.get(), S_IRUSR | S_IWUSR) != 0)
            throw TestError("temporary file permissions failed");
    }
    TemporaryFile(const TemporaryFile&) = delete;
    TemporaryFile& operator=(const TemporaryFile&) = delete;
    const char* path() const noexcept { return file_.path.c_str(); }
    FileDescriptor& descriptor() noexcept { return descriptor_; }
};

class TemporaryInput {
    TemporaryFile file_;
  public:
    explicit TemporaryInput(std::string_view text) {
        std::size_t offset = 0;
        while (offset < text.size()) {
            const auto count = ::write(file_.descriptor().get(),
                text.data() + offset, text.size() - offset);
            if (count < 0 && errno == EINTR) continue;
            if (count <= 0) throw TestError("temporary input write failed");
            offset += static_cast<std::size_t>(count);
        }
        file_.descriptor().close();
    }
    TemporaryInput(const TemporaryInput&) = delete;
    TemporaryInput& operator=(const TemporaryInput&) = delete;
    const char* path() const noexcept { return file_.path(); }
};

class CapturedOutput {
    TemporaryFile temporary_;
    std::unique_ptr<FILE, decltype(close_c_file)> file_{nullptr, close_c_file};
    FileDescriptor saved_;
    bool active_ = false;
  public:
    CapturedOutput() {
        if (std::fflush(stdout) != 0) throw TestError("stdout flush failed");
        file_.reset(::fdopen(temporary_.descriptor().get(), "w+b"));
        if (!file_) throw TestError("stdout temporary file failed");
        temporary_.descriptor().release(); // fdopen now owns the descriptor.
        saved_.reset(::dup(STDOUT_FILENO));
        if (saved_.get() < 0 || ::dup2(::fileno(file_.get()), STDOUT_FILENO) < 0)
            throw TestError("stdout capture failed");
        active_ = true;
    }
    ~CapturedOutput() {
        if (active_) {
            std::fflush(stdout);
            ::dup2(saved_.get(), STDOUT_FILENO);
        }
    }
    CapturedOutput(const CapturedOutput&) = delete;
    CapturedOutput& operator=(const CapturedOutput&) = delete;
    std::string read() const {
        if (std::fflush(stdout) != 0 || std::fseek(file_.get(), 0, SEEK_SET) != 0)
            throw TestError("captured output rewind failed");
        std::string result;
        std::array<char, 1024> buffer{};
        std::size_t length;
        while ((length = std::fread(buffer.data(), 1, buffer.size(), file_.get())) != 0)
            result.append(buffer.data(), length);
        if (std::ferror(file_.get())) throw TestError("captured output read failed");
        return result;
    }
};

class ErrorSlot {
    using Diagnostic = std::unique_ptr<char, decltype(free_c_diagnostic)>;
    Diagnostic original_{::strdup(""), free_c_diagnostic};
    Diagnostic returned_{nullptr, free_c_diagnostic};
    char* value_ = original_.get();
  public:
    ErrorSlot() {
        if (!original_) throw TestError("diagnostic seed allocation failed");
    }
    ~ErrorSlot() {
        // Replacing the slot must not release the caller's previous allocation.
        // C API results use the same C allocator as the seed and diagnostics.
        if (value_ != original_.get()) returned_.reset(value_);
    }
    ErrorSlot(const ErrorSlot&) = delete;
    ErrorSlot& operator=(const ErrorSlot&) = delete;
    char** slot() noexcept { return &value_; }
    char* get() const noexcept { return value_; }
    bool replaced() const noexcept { return value_ != original_.get(); }
};

template <typename Api>
class ApiParser {
  public:
    typename Api::Handle* value = nullptr;
    ApiParser() {
        ErrorSlot error;
        if (Api::init(&value, error.slot()) != 0 || !value || error.get()) {
            Api::cleanup(value);
            throw TestError(std::string(Api::name()) + " initialization failed");
        }
    }
    ~ApiParser() { Api::cleanup(value); }
    ApiParser(const ApiParser&) = delete;
    ApiParser& operator=(const ApiParser&) = delete;
};

struct ParseResult {
    int status;
    bool replaced;
    bool null_error;
    std::string error;
    std::string output;
};

class TestChecks {
    unsigned int total_ = 0;
    unsigned int failures_ = 0;
    std::string engine_;

    static void diagnostic(const ParseResult& result) {
        if (result.status != 0)
            std::fprintf(stderr, "  diagnostic: %s\n", result.error.c_str());
    }

  protected:
    void success3(std::string_view name, const ParseResult& result,
            std::string_view expected, const std::array<const char*, 3>& labels) {
        check(std::string(name) + labels[0], result.status == 0);
        check(std::string(name) + labels[1], result.replaced && result.null_error);
        check(std::string(name) + labels[2], result.output == expected);
        diagnostic(result);
    }
    void success2(std::string_view name, const ParseResult& result, std::string_view expected) {
        check(std::string(name) + ": success and reset diagnostic",
            result.status == 0 && result.replaced && result.null_error);
        check(std::string(name) + ": exact output", result.output == expected);
        diagnostic(result);
    }
    void failure2(std::string_view name, const ParseResult& result, bool failed,
            const std::array<const char*, 2>& labels) {
        check(std::string(name) + labels[0], failed);
        check(std::string(name) + labels[1], result.replaced &&
            !result.null_error && !result.error.empty());
    }
    void failure1(std::string_view name, const ParseResult& result, std::string_view expected) {
        check(std::string(name) + ": failure and owned diagnostic", result.status != 0 &&
            result.replaced && !result.null_error && !result.error.empty());
        if (!expected.empty())
            check(std::string(name) + ": diagnostic identifies limit",
                result.error.find(expected) != std::string::npos);
    }
    int report(std::string_view suite) const {
        std::printf("%.*s: %u checks passed, %u failed\n",
            static_cast<int>(suite.size()), suite.data(), passed(), failures_);
        return failures_ ? 1 : 0;
    }

  public:
    void set_engine(std::string_view engine) { engine_ = engine; }
    void check(std::string_view name, bool passed) {
        ++total_;
        if (passed) return;
        ++failures_;
        const std::string prefix = engine_.empty() ? "" : engine_ + ": ";
        std::fprintf(stderr, "FAIL: %s%.*s\n", prefix.c_str(),
            static_cast<int>(name.size()), name.data());
    }
    unsigned int total() const noexcept { return total_; }
    unsigned int failures() const noexcept { return failures_; }
    unsigned int passed() const noexcept { return total_ - failures_; }
};

template <typename Api>
ParseResult parse_path(typename Api::Handle* parser, const char* path) {
    ErrorSlot error;
    CapturedOutput output;
    const int status = Api::file(parser, path, error.slot());
    return {status, error.replaced(), !error.get(),
        error.get() ? error.get() : "", output.read()};
}

template <typename Api>
ParseResult parse_file(typename Api::Handle* parser, std::string_view text) {
    TemporaryInput input(text);
    return parse_path<Api>(parser, input.path());
}

} // namespace jsonbench::test

#endif
