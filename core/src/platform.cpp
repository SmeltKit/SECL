// Copyright (C) 2026 ppm203. SPDX-License-Identifier: GPL-3.0-only
// Derived from M2CL; see LICENSE and ADDITIONAL_TERMS.md in the repository
// root.
#include "solar/core.hpp"
#include <array>
#include <fstream>
#include <regex>
#include <set>
#include <sstream>
#include <thread>
#ifdef _WIN32
#include <bcrypt.h>
#include <shellapi.h>
#include <windows.h>
#else
#include <fcntl.h>
#include <signal.h>
#include <spawn.h>
#include <sys/utsname.h>
#include <sys/wait.h>
#include <unistd.h>
extern char **environ;
#ifdef __APPLE__
#include <CommonCrypto/CommonDigest.h>
#include <Security/SecRandom.h>
#else
#include <openssl/sha.h>
#include <sys/random.h>
#endif
#endif
namespace solar {
std::string read_file(const fs::path &p, size_t limit) {
    std::ifstream f(p, std::ios::binary);
    if (!f)
        throw Error("local", "file_unreadable", "无法读取本地文件：" + p.filename().u8string());
    f.seekg(0, std::ios::end);
    auto n = f.tellg();
    if (n < 0 || std::uint64_t(n) > limit)
        throw Error("local", "file_too_large", "本地文件超过大小限制。");
    f.seekg(0);
    std::string s(size_t(n), '\0');
    if (n && !f.read(s.data(), n))
        throw Error("local", "file_read_failed", "本地文件读取失败。");
    return s;
}
static std::string b64(const unsigned char *b, size_t n) {
    const std::string chars = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_";
    std::string r;
    unsigned v = 0, bits = 0;
    for (size_t i = 0; i < n; ++i) {
        v = (v << 8) | b[i];
        bits += 8;
        while (bits >= 6) {
            bits -= 6;
            r += chars[(v >> bits) & 63];
        }
    }
    if (bits)
        r += chars[(v << (6 - bits)) & 63];
    return r;
}
std::string random_url_token() {
    std::array<unsigned char, 32> b{};
#ifdef _WIN32
    if (BCryptGenRandom(nullptr, b.data(), ULONG(b.size()), BCRYPT_USE_SYSTEM_PREFERRED_RNG) != 0)
        throw Error("crypto", "random_failed", "无法获取安全随机数。");
#elif defined(__APPLE__)
    if (SecRandomCopyBytes(kSecRandomDefault, b.size(), b.data()) != errSecSuccess)
        throw Error("crypto", "random_failed", "无法获取安全随机数。");
#else
    size_t n = 0;
    while (n < b.size()) {
        auto r = getrandom(b.data() + n, b.size() - n, 0);
        if (r < 0 && errno == EINTR)
            continue;
        if (r <= 0)
            throw Error("crypto", "random_failed", "无法获取安全随机数。");
        n += size_t(r);
    }
#endif
    return b64(b.data(), b.size());
}
std::string sha256_base64url(const std::string &s) {
    std::array<unsigned char, 32> b{};
#ifdef _WIN32
    BCRYPT_ALG_HANDLE algorithm = nullptr;
    BCRYPT_HASH_HANDLE hash = nullptr;
    if (BCryptOpenAlgorithmProvider(&algorithm, BCRYPT_SHA256_ALGORITHM, nullptr, 0) != 0)
        throw Error("crypto", "sha256_failed", "无法初始化 SHA-256。");
    auto a = BCryptCreateHash(algorithm, &hash, nullptr, 0, nullptr, 0, 0);
    auto h = a == 0 ? BCryptHashData(hash, (PUCHAR)s.data(), ULONG(s.size()), 0) : a;
    auto f = h == 0 ? BCryptFinishHash(hash, b.data(), ULONG(b.size()), 0) : h;
    if (hash)
        BCryptDestroyHash(hash);
    BCryptCloseAlgorithmProvider(algorithm, 0);
    if (f != 0)
        throw Error("crypto", "sha256_failed", "SHA-256 计算失败。");
#elif defined(__APPLE__)
    CC_SHA256(s.data(), CC_LONG(s.size()), b.data());
#else
    SHA256(reinterpret_cast<const unsigned char *>(s.data()), s.size(), b.data());
#endif
    return b64(b.data(), b.size());
}
#ifdef _WIN32
static std::wstring wide(const std::string &s) {
    if (s.empty())
        return {};
    int n = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, s.data(), int(s.size()), nullptr, 0);
    if (!n)
        throw Error("process", "invalid_utf8", "进程参数编码无效。");
    std::wstring w(n, 0);
    MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, s.data(), int(s.size()), w.data(), n);
    return w;
}
static std::wstring quote(const std::wstring &s) {
    std::wstring r = L"\"";
    size_t slashes = 0;
    for (wchar_t c : s) {
        if (c == L'\\') {
            ++slashes;
            continue;
        }
        if (c == L'\"') {
            r.append(2 * slashes + 1, L'\\');
            r += c;
        } else {
            r.append(slashes, L'\\');
            r += c;
        }
        slashes = 0;
    }
    r.append(2 * slashes, L'\\');
    return r + L"\"";
}
static std::wstring command_line(const fs::path &exe, const std::vector<std::string> &args) {
    std::wstring r = quote(exe.wstring());
    for (const auto &a : args)
        r += L" " + quote(wide(a));
    return r;
}
std::string run_capture(const fs::path &exe, const std::vector<std::string> &args, int timeout) {
    SECURITY_ATTRIBUTES sa{sizeof(sa), nullptr, TRUE};
    HANDLE reader = nullptr, writer = nullptr;
    if (!CreatePipe(&reader, &writer, &sa, 0))
        return {};
    SetHandleInformation(reader, HANDLE_FLAG_INHERIT, 0);
    STARTUPINFOW start{};
    start.cb = sizeof(start);
    start.dwFlags = STARTF_USESTDHANDLES;
    start.hStdOutput = writer;
    start.hStdError = writer;
    start.hStdInput = GetStdHandle(STD_INPUT_HANDLE);
    PROCESS_INFORMATION child{};
    auto cmd = command_line(exe, args);
    BOOL ok = CreateProcessW(exe.c_str(), cmd.data(), nullptr, nullptr, TRUE, CREATE_NO_WINDOW,
                             nullptr, nullptr, &start, &child);
    CloseHandle(writer);
    if (!ok) {
        CloseHandle(reader);
        return {};
    }
    std::string out;
    auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeout);
    while (true) {
        DWORD available = 0;
        if (PeekNamedPipe(reader, nullptr, 0, nullptr, &available, nullptr) && available) {
            char b[4096];
            DWORD got = 0;
            if (ReadFile(reader, b, DWORD(std::min<size_t>(available, sizeof(b))), &got, nullptr) &&
                out.size() < 65536)
                out.append(b, got);
        }
        if (WaitForSingleObject(child.hProcess, 10) == WAIT_OBJECT_0)
            break;
        if (std::chrono::steady_clock::now() > deadline) {
            TerminateProcess(child.hProcess, 1);
            break;
        }
    }
    CloseHandle(reader);
    CloseHandle(child.hThread);
    CloseHandle(child.hProcess);
    return out;
}
std::int64_t spawn_game(const Command &c) {
    STARTUPINFOW start{};
    start.cb = sizeof(start);
    PROCESS_INFORMATION child{};
    auto cmd = command_line(c.executable, c.arguments);
    if (!CreateProcessW(c.executable.c_str(), cmd.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW,
                        nullptr, c.directory.c_str(), &start, &child))
        throw Error("launch", "process_failed",
                    "无法创建游戏进程，系统代码 " + std::to_string(GetLastError()));
    CloseHandle(child.hThread);
    return reinterpret_cast<std::int64_t>(child.hProcess);
}
int game_exit(std::int64_t handle) {
    auto h = reinterpret_cast<HANDLE>(handle);
    DWORD status = 0;
    if (!GetExitCodeProcess(h, &status))
        return -2;
    if (status == STILL_ACTIVE)
        return -1;
    CloseHandle(h);
    return int(status);
}
void open_browser(const std::string &url) {
    auto r = ShellExecuteW(nullptr, L"open", wide(url).c_str(), nullptr, nullptr, SW_SHOWNORMAL);
    if (INT_PTR(r) <= 32)
        throw Error("browser", "open_failed", "无法打开浏览器。");
}
#else
static std::vector<char *> argv_for(std::vector<std::string> &owned) {
    std::vector<char *> r;
    for (auto &s : owned)
        r.push_back(s.data());
    r.push_back(nullptr);
    return r;
}
std::string run_capture(const fs::path &exe, const std::vector<std::string> &args, int timeout) {
    int p[2];
    if (pipe(p) != 0)
        return {};
    fcntl(p[0], F_SETFD, FD_CLOEXEC);
    fcntl(p[1], F_SETFD, FD_CLOEXEC);
    posix_spawn_file_actions_t actions;
    posix_spawn_file_actions_init(&actions);
    posix_spawn_file_actions_adddup2(&actions, p[1], STDOUT_FILENO);
    posix_spawn_file_actions_adddup2(&actions, p[1], STDERR_FILENO);
    posix_spawn_file_actions_addclose(&actions, p[0]);
    posix_spawn_file_actions_addclose(&actions, p[1]);
    std::vector<std::string> owned{exe.u8string()};
    owned.insert(owned.end(), args.begin(), args.end());
    auto argv = argv_for(owned);
    pid_t pid = 0;
    int error = posix_spawn(&pid, exe.c_str(), &actions, nullptr, argv.data(), environ);
    posix_spawn_file_actions_destroy(&actions);
    close(p[1]);
    if (error) {
        close(p[0]);
        return {};
    }
    fcntl(p[0], F_SETFL, O_NONBLOCK);
    std::string out;
    auto end = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeout);
    int status = 0;
    for (;;) {
        char b[4096];
        ssize_t got = 0;
        while ((got = read(p[0], b, sizeof(b))) > 0)
            if (out.size() < 65536)
                out.append(b, size_t(got));
        if (waitpid(pid, &status, WNOHANG) == pid)
            break;
        if (std::chrono::steady_clock::now() > end) {
            kill(pid, SIGKILL);
            waitpid(pid, &status, 0);
            break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    char b[4096];
    ssize_t got;
    while ((got = read(p[0], b, sizeof(b))) > 0)
        if (out.size() < 65536)
            out.append(b, size_t(got));
    close(p[0]);
    return out;
}
std::int64_t spawn_game(const Command &c) {
    int p[2];
    if (pipe(p))
        throw Error("launch", "pipe_failed", "无法准备启动进程。");
    fcntl(p[1], F_SETFD, FD_CLOEXEC);
    std::vector<std::string> owned{c.executable.u8string()};
    owned.insert(owned.end(), c.arguments.begin(), c.arguments.end());
    auto argv = argv_for(owned);
    const pid_t pid = fork();
    if (pid == 0) {
        close(p[0]);
        int error = 0;
        if (chdir(c.directory.c_str()) != 0)
            error = errno;
        int null = open("/dev/null", O_RDWR);
        if (null >= 0) {
            dup2(null, 0);
            dup2(null, 1);
            dup2(null, 2);
            if (null > 2)
                close(null);
        }
        if (!error)
            execve(c.executable.c_str(), argv.data(), environ);
        error = error ? error : errno;
        write(p[1], &error, sizeof(error));
        _exit(127);
    }
    close(p[1]);
    if (pid < 0) {
        close(p[0]);
        throw Error("launch", "fork_failed", "无法创建游戏进程。");
    }
    int error = 0;
    auto got = read(p[0], &error, sizeof(error));
    close(p[0]);
    if (got > 0) {
        waitpid(pid, nullptr, 0);
        throw Error("launch", "exec_failed", "无法执行 Java，系统代码 " + std::to_string(error));
    }
    return pid;
}
int game_exit(std::int64_t p) {
    int status = 0;
    auto r = waitpid(pid_t(p), &status, WNOHANG);
    if (r == 0)
        return -1;
    if (r < 0)
        return -2;
    return WIFEXITED(status) ? WEXITSTATUS(status) : 128 + WTERMSIG(status);
}
void open_browser(const std::string &url) {
#ifdef __APPLE__
    auto exe = fs::path("/usr/bin/open");
#else
    auto exe = fs::path("/usr/bin/xdg-open");
#endif
    run_capture(exe, {url}, 3000);
}
#endif
void terminate_process(std::int64_t child) {
#ifdef _WIN32
    auto h = reinterpret_cast<HANDLE>(child);
    TerminateProcess(h, 1);
    WaitForSingleObject(h, 5000);
    CloseHandle(h);
#else
    kill(pid_t(child), SIGKILL);
    waitpid(pid_t(child), nullptr, 0);
#endif
}
void move_to_trash(const fs::path &p) {
#ifdef _WIN32
    auto path = p.wstring();
    path.push_back(L'\0');
    path.push_back(L'\0');
    SHFILEOPSTRUCTW action{};
    action.wFunc = FO_DELETE;
    action.pFrom = path.c_str();
    action.fFlags = FOF_ALLOWUNDO | FOF_NOCONFIRMATION | FOF_NOERRORUI | FOF_SILENT;
    if (SHFileOperationW(&action) != 0 || action.fAnyOperationsAborted)
        throw Error("files", "trash_failed", "无法移到系统回收站，文件未删除。");
#elif defined(__APPLE__)
    run_capture("/usr/bin/trash", {p.u8string()}, 10000);
    if (fs::exists(p))
        throw Error("files", "trash_failed", "无法移到系统废纸篓，文件未删除。");
#else
    run_capture("/usr/bin/gio", {"trash", p.u8string()}, 10000);
    if (fs::exists(p))
        throw Error("files", "trash_failed", "无法移到系统回收站，文件未删除。");
#endif
}
Platform platform() {
    Platform p;
#ifdef _WIN32
    p.os = "windows";
    OSVERSIONINFOW info{};
    info.dwOSVersionInfoSize = sizeof(info);
    using RtlVersion = LONG(WINAPI *)(OSVERSIONINFOW *);
    auto rtl = reinterpret_cast<RtlVersion>(
        GetProcAddress(GetModuleHandleW(L"ntdll.dll"), "RtlGetVersion"));
    if (rtl && rtl(&info) == 0)
        p.version = std::to_string(info.dwMajorVersion) + "." +
                    std::to_string(info.dwMinorVersion) + "." + std::to_string(info.dwBuildNumber);
#elif defined(__APPLE__)
    p.os = "osx";
#else
    p.os = "linux";
#endif
#if defined(__aarch64__) || defined(_M_ARM64)
    p.arch = "aarch64";
#elif defined(__i386__) || defined(_M_IX86)
    p.arch = "x86";
#else
    p.arch = "x86_64";
#endif
#ifndef _WIN32
    utsname u{};
    if (uname(&u) == 0)
        p.version = u.release;
#endif
    return p;
}
static std::string env(const char *n) {
    const char *v = std::getenv(n);
    return v ? v : "";
}
std::string default_game_root() {
#ifdef _WIN32
    return (fs::u8path(env("APPDATA")) / ".minecraft").u8string();
#elif defined(__APPLE__)
    return (fs::u8path(env("HOME")) / "Library/Application Support/minecraft").u8string();
#else
    return (fs::u8path(env("HOME")) / ".minecraft").u8string();
#endif
}
std::string user_data_root() {
    auto override_path = env("SOLAR_DATA_DIR");
    if (!override_path.empty())
        return override_path;
#ifdef _WIN32
    return (fs::u8path(env("LOCALAPPDATA")) / "SolarLauncher").u8string();
#elif defined(__APPLE__)
    return (fs::u8path(env("HOME")) / "Library/Application Support/SolarLauncher").u8string();
#else
    auto x = env("XDG_DATA_HOME");
    return (x.empty() ? fs::u8path(env("HOME")) / ".local/share/solar-launcher"
                      : fs::u8path(x) / "solar-launcher")
        .u8string();
#endif
}
fs::path java_runtime_executable(const fs::path &candidate, const std::string &properties) {
    // macOS /usr/bin/java is a launcher, not a separate runtime. Resolve the
    // executable under the java.home reported by the JVM before deduplicating.
    std::smatch match;
    if (std::regex_search(properties, match,
                          std::regex(R"((?:^|\n)\s*java\.home\s*=\s*([^\r\n]+))"))) {
        auto home = match[1].str();
        home.erase(home.find_last_not_of(" \t") + 1);
        auto runtime = fs::u8path(home) / "bin" /
                       (platform().os == "windows" ? "java.exe" : "java");
        std::error_code ec;
        if (fs::is_regular_file(runtime, ec)) {
            auto real = fs::canonical(runtime, ec);
            if (!ec)
                return real;
        }
    }
    std::error_code ec;
    auto real = fs::canonical(candidate, ec);
    return ec ? candidate : real;
}
Json detect_javas(const std::string &extra) {
    std::set<fs::path> paths;
    auto add = [&](fs::path p) {
        std::error_code e;
        if (fs::is_regular_file(p, e)) {
            auto real = fs::canonical(p, e);
            if (!e)
                paths.insert(real);
        }
    };
    const auto executable = platform().os == "windows" ? "java.exe" : "java";
    if (!extra.empty())
        add(fs::u8path(extra));
    if (!env("JAVA_HOME").empty())
        add(fs::u8path(env("JAVA_HOME")) / "bin" / executable);
    auto path = env("PATH");
    std::stringstream ss(path);
    std::string item;
    while (std::getline(ss, item, platform().os == "windows" ? ';' : ':'))
        if (!item.empty())
            add(fs::u8path(item) / executable);
    std::vector<fs::path> roots;
#ifdef __APPLE__
    roots = {"/Library/Java/JavaVirtualMachines",
             fs::u8path(env("HOME")) / "Library/Java/JavaVirtualMachines", "/opt/homebrew/opt",
             "/usr/local/opt"};
#elif defined(_WIN32)
    roots = {fs::u8path(env("ProgramFiles")) / "Java",
             fs::u8path(env("ProgramFiles")) / "Eclipse Adoptium",
             fs::u8path(env("ProgramFiles")) / "Microsoft",
             fs::u8path(env("ProgramFiles")) / "Zulu"};
#else
    roots = {"/usr/lib/jvm", "/usr/java", "/opt/java"};
#endif
    for (auto &r : roots) {
        std::error_code ec;
        fs::directory_iterator it(r, fs::directory_options::skip_permission_denied, ec);
        if (ec)
            continue;
        int count = 0;
        for (auto &entry : it) {
            if (++count > 200)
                break;
            auto d = entry.path();
            if (r.filename() == "opt" && d.filename().string().find("openjdk") == std::string::npos)
                continue;
            add(d / "bin" / executable);
            add(d / "Contents/Home/bin" / executable);
            add(d / "libexec/openjdk.jdk/Contents/Home/bin" / executable);
        }
    }
    Json result = Json::array();
    std::set<fs::path> runtimes;
    for (auto &p : paths) {
        auto out = run_capture(p, {"-XshowSettings:properties", "-version"});
        std::smatch m;
        if (!std::regex_search(out, m, std::regex(R"(java\.version\s*=\s*([^\r\n ]+))")))
            continue;
        std::string version = m[1];
        std::string vendor = "Java", arch = "unknown";
        if (std::regex_search(out, m, std::regex(R"(java\.vendor\s*=\s*([^\r\n]+))")))
            vendor = m[1];
        if (std::regex_search(out, m, std::regex(R"(os\.arch\s*=\s*([^\r\n ]+))")))
            arch = m[1];
        auto v = version.rfind("1.", 0) == 0 ? version.substr(2) : version;
        int major = 0;
        try {
            major = std::stoi(v);
        } catch (...) {
            continue;
        }
        auto runtime = java_runtime_executable(p, out);
        if (!runtimes.insert(runtime).second)
            continue;
        result.push_back({{"path", runtime.u8string()},
                          {"version", version},
                          {"major", major},
                          {"vendor", vendor},
                          {"arch", arch}});
    }
    return result;
}
} // namespace solar
