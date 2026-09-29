#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cctype>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iomanip>
#include <iostream>
#include <memory>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

#include <curl/curl.h>
#include <json-c/json.h>
#include <openssl/crypto.h>
#include <openssl/evp.h>
#include <openssl/rand.h>
#include <openssl/sha.h>

#ifdef _WIN32
#include "embedded_ca_bundle.h"
#include <conio.h>
#include <windows.h>
#else
#include <sys/stat.h>
#include <termios.h>
#include <unistd.h>
#endif

namespace fs = std::filesystem;
using namespace std::chrono_literals;

namespace ansi {
constexpr const char* reset   = "\033[0m";
constexpr const char* bold    = "\033[1m";
constexpr const char* dim     = "\033[2m";
constexpr const char* red     = "\033[31m";
constexpr const char* green   = "\033[32m";
constexpr const char* yellow  = "\033[33m";
constexpr const char* blue    = "\033[34m";
constexpr const char* magenta = "\033[35m";
constexpr const char* cyan    = "\033[36m";
constexpr const char* white   = "\033[37m";
constexpr const char* gray    = "\033[90m";
constexpr const char* brightMagenta = "\033[95m";
constexpr const char* brightCyan    = "\033[96m";
}

static void enableAnsiColors() {
#ifdef _WIN32
    HANDLE hOut = GetStdHandle(STD_OUTPUT_HANDLE);
    if (hOut == INVALID_HANDLE_VALUE) return;
    DWORD mode = 0;
    if (!GetConsoleMode(hOut, &mode)) return;
    SetConsoleMode(hOut, mode | ENABLE_VIRTUAL_TERMINAL_PROCESSING);
#endif
}

static std::string executableDirectory() {
#ifdef _WIN32
    std::array<char, 32768> buffer{};
    const DWORD len = GetModuleFileNameA(
        nullptr,
        buffer.data(),
        static_cast<DWORD>(buffer.size())
    );
    if (len > 0 && len < buffer.size()) {
        return fs::path(std::string(buffer.data(), len)).parent_path().string();
    }
#endif
    return {};
}

static void redrawCommandLine(const std::string& prompt, const std::string& line) {
    std::cout << "\r\033[2K" << prompt << line << std::flush;
}

static bool readCommandLine(
    const std::string& prompt,
    std::vector<std::string>& history,
    std::string& out)
{
#ifndef _WIN32
    if (!isatty(STDIN_FILENO)) {
        std::cout << prompt << std::flush;
        return static_cast<bool>(std::getline(std::cin, out));
    }
#endif

    std::string line;
    std::size_t historyIndex = history.size();
    redrawCommandLine(prompt, line);

#ifdef _WIN32
    for (;;) {
        int ch = _getch();
        if (ch == 3) {
            std::cout << "\n";
            throw std::runtime_error("Interrupted");
        }
        if (ch == '\r' || ch == '\n') {
            std::cout << "\n";
            break;
        }
        if (ch == 8 || ch == 127) {
            if (!line.empty()) line.pop_back();
            redrawCommandLine(prompt, line);
            continue;
        }
        if (ch == 0 || ch == 224) {
            const int ext = _getch();
            if (ext == 72) { // up
                if (!history.empty() && historyIndex > 0) {
                    --historyIndex;
                    line = history[historyIndex];
                }
                redrawCommandLine(prompt, line);
            } else if (ext == 80) { // down
                if (historyIndex < history.size()) ++historyIndex;
                line = historyIndex < history.size() ? history[historyIndex] : "";
                redrawCommandLine(prompt, line);
            }
            continue;
        }
        if (ch >= 32 && ch <= 126) {
            line.push_back(static_cast<char>(ch));
            redrawCommandLine(prompt, line);
        }
    }
#else
    termios oldt{};
    if (tcgetattr(STDIN_FILENO, &oldt) != 0) {
        std::cout << prompt << std::flush;
        return static_cast<bool>(std::getline(std::cin, out));
    }

    termios raw = oldt;
    raw.c_lflag &= static_cast<tcflag_t>(~(ICANON | ECHO));
    raw.c_cc[VMIN] = 1;
    raw.c_cc[VTIME] = 0;
    tcsetattr(STDIN_FILENO, TCSANOW, &raw);

    auto restoreTerminal = [&]() {
        tcsetattr(STDIN_FILENO, TCSANOW, &oldt);
    };

    try {
        for (;;) {
            unsigned char ch = 0;
            const ssize_t n = ::read(STDIN_FILENO, &ch, 1);
            if (n <= 0) {
                restoreTerminal();
                std::cout << "\n";
                return false;
            }
            if (ch == 3) { // Ctrl-C
                restoreTerminal();
                std::cout << "\n";
                throw std::runtime_error("Interrupted");
            }
            if (ch == 4 && line.empty()) { // Ctrl-D
                restoreTerminal();
                std::cout << "\n";
                return false;
            }
            if (ch == '\r' || ch == '\n') {
                restoreTerminal();
                std::cout << "\n";
                break;
            }
            if (ch == 127 || ch == 8) {
                if (!line.empty()) line.pop_back();
                redrawCommandLine(prompt, line);
                continue;
            }
            if (ch == 27) { // ESC sequence, arrows
                unsigned char a = 0, b = 0;
                if (::read(STDIN_FILENO, &a, 1) == 1 && a == '[' &&
                    ::read(STDIN_FILENO, &b, 1) == 1) {
                    if (b == 'A') { // up
                        if (!history.empty() && historyIndex > 0) {
                            --historyIndex;
                            line = history[historyIndex];
                        }
                    } else if (b == 'B') { // down
                        if (historyIndex < history.size()) ++historyIndex;
                        line = historyIndex < history.size() ? history[historyIndex] : "";
                    }
                    redrawCommandLine(prompt, line);
                }
                continue;
            }
            if (ch >= 32 && ch <= 126) {
                line.push_back(static_cast<char>(ch));
                redrawCommandLine(prompt, line);
            }
        }
    } catch (...) {
        restoreTerminal();
        throw;
    }
#endif

    out = line;
    if (!line.empty() && (history.empty() || history.back() != line)) {
        history.push_back(line);
        if (history.size() > 200) history.erase(history.begin());
    }
    return true;
}

namespace cfg {
/*
 * = CONFIG UPDATE =
 */

constexpr const char* USER_AGENT = "Venera-CoreW/0.2";

constexpr std::uint32_t WALLET_KDF_ITERATIONS = 600000;
constexpr std::size_t SALT_SIZE = 16;
constexpr std::size_t IV_SIZE = 12;
constexpr std::size_t TAG_SIZE = 16;
constexpr char WALLET_MAGIC[8] = {'V','N','R','K','E','Y','0','2'};
}

struct JsonDeleter {
    void operator()(json_object* p) const noexcept {
        if (p) json_object_put(p);
    }
};
using JsonPtr = std::unique_ptr<json_object, JsonDeleter>;

static std::string trim(std::string s) {
    const auto notSpace = [](unsigned char c) { return !std::isspace(c); };
    s.erase(s.begin(), std::find_if(s.begin(), s.end(), notSpace));
    s.erase(std::find_if(s.rbegin(), s.rend(), notSpace).base(), s.end());
    return s;
}

static std::string lower(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    return s;
}

static std::vector<std::string> splitWords(const std::string& s) {
    std::istringstream in(s);
    std::vector<std::string> out;
    std::string w;
    while (in >> w) out.push_back(w);
    return out;
}

static std::vector<std::string> splitCommand(const std::string& s) {
    return splitWords(s);
}

static bool yesNo(const std::string& prompt, bool defaultYes) {
    std::cout << prompt << (defaultYes ? " [Y/n]: " : " [y/N]: ");
    std::string answer;
    std::getline(std::cin, answer);
    answer = lower(trim(answer));
    if (answer.empty()) return defaultYes;
    return answer == "y" || answer == "yes";
}

static std::string readHidden(const std::string& prompt) {
    std::cout << prompt << std::flush;
    std::string result;
#ifdef _WIN32
    while (true) {
        int ch = _getch();
        if (ch == '\r' || ch == '\n') break;
        if (ch == 8) {
            if (!result.empty()) result.pop_back();
            continue;
        }
        if (ch == 3) throw std::runtime_error("Interrupted");
        if (ch >= 32 && ch <= 126) result.push_back(static_cast<char>(ch));
    }
    std::cout << "\n";
#else
    termios oldt{};
    if (tcgetattr(STDIN_FILENO, &oldt) != 0) {
        std::getline(std::cin, result);
        return result;
    }
    termios newt = oldt;
    newt.c_lflag &= static_cast<tcflag_t>(~ECHO);
    tcsetattr(STDIN_FILENO, TCSANOW, &newt);
    std::getline(std::cin, result);
    tcsetattr(STDIN_FILENO, TCSANOW, &oldt);
    std::cout << "\n";
#endif
    return result;
}

static std::string walletPath(std::string name) {
    name = trim(name);
    if (name.size() < 5 || lower(name.substr(name.size() - 5)) != ".keys") {
        name += ".keys";
    }
    return name;
}

static void cleanse(std::string& s) {
    if (!s.empty()) OPENSSL_cleanse(s.data(), s.size());
    s.clear();
    s.shrink_to_fit();
}

static void writeU32(std::ofstream& out, std::uint32_t v) {
    unsigned char b[4] = {
        static_cast<unsigned char>((v >> 24) & 0xff),
        static_cast<unsigned char>((v >> 16) & 0xff),
        static_cast<unsigned char>((v >> 8) & 0xff),
        static_cast<unsigned char>(v & 0xff)
    };
    out.write(reinterpret_cast<const char*>(b), 4);
}

static std::uint32_t readU32(std::ifstream& in) {
    unsigned char b[4]{};
    in.read(reinterpret_cast<char*>(b), 4);
    if (!in) throw std::runtime_error("Invalid wallet file header");
    return (static_cast<std::uint32_t>(b[0]) << 24) |
           (static_cast<std::uint32_t>(b[1]) << 16) |
           (static_cast<std::uint32_t>(b[2]) << 8) |
           static_cast<std::uint32_t>(b[3]);
}

static std::array<unsigned char, 32> deriveWalletKey(
    const std::string& password,
    const unsigned char* salt,
    std::uint32_t iterations)
{
    std::array<unsigned char, 32> key{};
    if (password.empty()) throw std::runtime_error("Wallet password cannot be empty");
    if (PKCS5_PBKDF2_HMAC(
            password.c_str(), static_cast<int>(password.size()),
            salt, static_cast<int>(cfg::SALT_SIZE),
            static_cast<int>(iterations), EVP_sha256(),
            static_cast<int>(key.size()), key.data()) != 1) {
        throw std::runtime_error("Unable to derive wallet encryption key");
    }
    return key;
}

static void encryptWalletFile(
    const std::string& filename,
    const std::string& seed,
    const std::string& password)
{
    std::array<unsigned char, cfg::SALT_SIZE> salt{};
    std::array<unsigned char, cfg::IV_SIZE> iv{};
    std::array<unsigned char, cfg::TAG_SIZE> tag{};

    if (RAND_bytes(salt.data(), static_cast<int>(salt.size())) != 1 ||
        RAND_bytes(iv.data(), static_cast<int>(iv.size())) != 1) {
        throw std::runtime_error("Unable to generate secure random bytes");
    }

    auto key = deriveWalletKey(password, salt.data(), cfg::WALLET_KDF_ITERATIONS);
    EVP_CIPHER_CTX* raw = EVP_CIPHER_CTX_new();
    if (!raw) throw std::runtime_error("Unable to create cipher context");
    std::unique_ptr<EVP_CIPHER_CTX, decltype(&EVP_CIPHER_CTX_free)> ctx(raw, EVP_CIPHER_CTX_free);

    if (EVP_EncryptInit_ex(ctx.get(), EVP_aes_256_gcm(), nullptr, nullptr, nullptr) != 1 ||
        EVP_CIPHER_CTX_ctrl(ctx.get(), EVP_CTRL_GCM_SET_IVLEN, static_cast<int>(iv.size()), nullptr) != 1 ||
        EVP_EncryptInit_ex(ctx.get(), nullptr, nullptr, key.data(), iv.data()) != 1) {
        OPENSSL_cleanse(key.data(), key.size());
        throw std::runtime_error("Unable to initialize wallet encryption");
    }

    std::vector<unsigned char> ciphertext(seed.size() + EVP_MAX_BLOCK_LENGTH);
    int len = 0;
    int total = 0;
    if (EVP_EncryptUpdate(
            ctx.get(), ciphertext.data(), &len,
            reinterpret_cast<const unsigned char*>(seed.data()),
            static_cast<int>(seed.size())) != 1) {
        OPENSSL_cleanse(key.data(), key.size());
        throw std::runtime_error("Wallet encryption failed");
    }
    total += len;

    if (EVP_EncryptFinal_ex(ctx.get(), ciphertext.data() + total, &len) != 1) {
        OPENSSL_cleanse(key.data(), key.size());
        throw std::runtime_error("Wallet encryption finalization failed");
    }
    total += len;

    if (EVP_CIPHER_CTX_ctrl(
            ctx.get(), EVP_CTRL_GCM_GET_TAG,
            static_cast<int>(tag.size()), tag.data()) != 1) {
        OPENSSL_cleanse(key.data(), key.size());
        throw std::runtime_error("Unable to create wallet authentication tag");
    }

    std::ofstream out(filename, std::ios::binary | std::ios::trunc);
    if (!out) {
        OPENSSL_cleanse(key.data(), key.size());
        throw std::runtime_error("Unable to create wallet file: " + filename);
    }

    out.write(cfg::WALLET_MAGIC, sizeof(cfg::WALLET_MAGIC));
    writeU32(out, cfg::WALLET_KDF_ITERATIONS);
    out.write(reinterpret_cast<const char*>(salt.data()), static_cast<std::streamsize>(salt.size()));
    out.write(reinterpret_cast<const char*>(iv.data()), static_cast<std::streamsize>(iv.size()));
    out.write(reinterpret_cast<const char*>(tag.data()), static_cast<std::streamsize>(tag.size()));
    out.write(reinterpret_cast<const char*>(ciphertext.data()), total);
    out.close();

    OPENSSL_cleanse(key.data(), key.size());
    OPENSSL_cleanse(ciphertext.data(), ciphertext.size());

#ifndef _WIN32
    chmod(filename.c_str(), S_IRUSR | S_IWUSR);
#endif
}

static std::string decryptWalletFile(
    const std::string& filename,
    const std::string& password)
{
    std::ifstream in(filename, std::ios::binary);
    if (!in) throw std::runtime_error("Wallet file not found: " + filename);

    char magic[8]{};
    in.read(magic, 8);
    if (!in || std::memcmp(magic, cfg::WALLET_MAGIC, 8) != 0) {
        throw std::runtime_error("Not a Venera CLI v0.2 wallet file");
    }

    const std::uint32_t iterations = readU32(in);
    if (iterations < 10000 || iterations > 5000000) {
        throw std::runtime_error("Invalid wallet KDF parameters");
    }

    std::array<unsigned char, cfg::SALT_SIZE> salt{};
    std::array<unsigned char, cfg::IV_SIZE> iv{};
    std::array<unsigned char, cfg::TAG_SIZE> tag{};

    in.read(reinterpret_cast<char*>(salt.data()), static_cast<std::streamsize>(salt.size()));
    in.read(reinterpret_cast<char*>(iv.data()), static_cast<std::streamsize>(iv.size()));
    in.read(reinterpret_cast<char*>(tag.data()), static_cast<std::streamsize>(tag.size()));
    if (!in) throw std::runtime_error("Truncated wallet file");

    std::vector<unsigned char> ciphertext(
        (std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    if (ciphertext.empty()) throw std::runtime_error("Wallet file has no encrypted payload");

    auto key = deriveWalletKey(password, salt.data(), iterations);
    EVP_CIPHER_CTX* raw = EVP_CIPHER_CTX_new();
    if (!raw) {
        OPENSSL_cleanse(key.data(), key.size());
        throw std::runtime_error("Unable to create cipher context");
    }
    std::unique_ptr<EVP_CIPHER_CTX, decltype(&EVP_CIPHER_CTX_free)> ctx(raw, EVP_CIPHER_CTX_free);

    if (EVP_DecryptInit_ex(ctx.get(), EVP_aes_256_gcm(), nullptr, nullptr, nullptr) != 1 ||
        EVP_CIPHER_CTX_ctrl(ctx.get(), EVP_CTRL_GCM_SET_IVLEN, static_cast<int>(iv.size()), nullptr) != 1 ||
        EVP_DecryptInit_ex(ctx.get(), nullptr, nullptr, key.data(), iv.data()) != 1) {
        OPENSSL_cleanse(key.data(), key.size());
        throw std::runtime_error("Unable to initialize wallet decryption");
    }

    std::vector<unsigned char> plaintext(ciphertext.size() + EVP_MAX_BLOCK_LENGTH);
    int len = 0;
    int total = 0;
    if (EVP_DecryptUpdate(
            ctx.get(), plaintext.data(), &len,
            ciphertext.data(), static_cast<int>(ciphertext.size())) != 1) {
        OPENSSL_cleanse(key.data(), key.size());
        OPENSSL_cleanse(plaintext.data(), plaintext.size());
        throw std::runtime_error("Wallet decryption failed");
    }
    total += len;

    if (EVP_CIPHER_CTX_ctrl(
            ctx.get(), EVP_CTRL_GCM_SET_TAG,
            static_cast<int>(tag.size()), tag.data()) != 1) {
        OPENSSL_cleanse(key.data(), key.size());
        OPENSSL_cleanse(plaintext.data(), plaintext.size());
        throw std::runtime_error("Wallet authentication setup failed");
    }

    const int finalResult = EVP_DecryptFinal_ex(ctx.get(), plaintext.data() + total, &len);
    OPENSSL_cleanse(key.data(), key.size());

    if (finalResult <= 0) {
        OPENSSL_cleanse(plaintext.data(), plaintext.size());
        throw std::runtime_error("Incorrect password or corrupted wallet file");
    }
    total += len;

    std::string seed(reinterpret_cast<char*>(plaintext.data()), static_cast<std::size_t>(total));
    OPENSSL_cleanse(plaintext.data(), plaintext.size());
    return seed;
}

struct HttpResponse {
    long status = 0;
    std::string body;
};

static size_t writeCallback(char* ptr, size_t size, size_t nmemb, void* userdata) {
    const size_t bytes = size * nmemb;
    static_cast<std::string*>(userdata)->append(ptr, bytes);
    return bytes;
}

class HttpClient {
public:
    HttpClient() {
        if (curl_global_init(CURL_GLOBAL_DEFAULT) != CURLE_OK) {
            throw std::runtime_error("curl_global_init failed");
        }
        curl_ = curl_easy_init();
        if (!curl_) {
            curl_global_cleanup();
            throw std::runtime_error("curl_easy_init failed");
        }

        // INTER
        curl_easy_setopt(curl_, CURLOPT_COOKIEFILE, "");
        curl_easy_setopt(curl_, CURLOPT_USERAGENT, cfg::USER_AGENT);
        curl_easy_setopt(curl_, CURLOPT_FOLLOWLOCATION, 1L);
        curl_easy_setopt(curl_, CURLOPT_MAXREDIRS, 5L);
        curl_easy_setopt(curl_, CURLOPT_SSL_VERIFYPEER, 1L);
        curl_easy_setopt(curl_, CURLOPT_SSL_VERIFYHOST, 2L);

#ifdef _WIN32
        // STATIC BUILT
#if LIBCURL_VERSION_NUM >= 0x074D00
        struct curl_blob caBlob {
            const_cast<unsigned char*>(VENERA_CA_BUNDLE),
            VENERA_CA_BUNDLE_LEN,
            CURL_BLOB_COPY
        };
        curl_easy_setopt(curl_, CURLOPT_CAINFO_BLOB, &caBlob);
#endif
#ifdef CURLSSLOPT_NATIVE_CA
        // SSL
        curl_easy_setopt(
            curl_,
            CURLOPT_SSL_OPTIONS,
            static_cast<long>(CURLSSLOPT_NATIVE_CA)
        );
#endif
#endif

        curl_easy_setopt(curl_, CURLOPT_ACCEPT_ENCODING, "");
        curl_easy_setopt(curl_, CURLOPT_CONNECTTIMEOUT, 20L);
        curl_easy_setopt(curl_, CURLOPT_TIMEOUT, 240L);
        curl_easy_setopt(curl_, CURLOPT_NOSIGNAL, 1L);
#ifdef CURL_HTTP_VERSION_2TLS
        curl_easy_setopt(curl_, CURLOPT_HTTP_VERSION, CURL_HTTP_VERSION_2TLS);
#endif
    }

    ~HttpClient() {
        if (curl_) curl_easy_cleanup(curl_);
        curl_global_cleanup();
    }

    HttpClient(const HttpClient&) = delete;
    HttpClient& operator=(const HttpClient&) = delete;

    HttpResponse get(const std::string& path) {
        return perform("GET", path, "", nullptr);
    }

    HttpResponse postJson(const std::string& path, const std::string& body = "{}") {
        return perform("POST", path, body, "application/json");
    }

    HttpResponse postRaw(const std::string& path, const std::string& body, const char* contentType) {
        return perform("POST", path, body, contentType);
    }

private:
    CURL* curl_ = nullptr;

    HttpResponse perform(
        const std::string& method,
        const std::string& path,
        const std::string& body,
        const char* contentType)
    {
        HttpResponse response;
        const std::string url = std::string(cfg::BASE_URL) + path;
        struct curl_slist* headers = nullptr;

        headers = curl_slist_append(headers, "Accept: application/json");
        if (contentType) {
            const std::string ct = std::string("Content-Type: ") + contentType;
            headers = curl_slist_append(headers, ct.c_str());
        }

        curl_easy_setopt(curl_, CURLOPT_URL, url.c_str());
        curl_easy_setopt(curl_, CURLOPT_HTTPHEADER, headers);
        curl_easy_setopt(curl_, CURLOPT_WRITEFUNCTION, writeCallback);
        curl_easy_setopt(curl_, CURLOPT_WRITEDATA, &response.body);

        if (method == "GET") {
            // HELPER BASIC
            curl_easy_setopt(curl_, CURLOPT_POSTFIELDS, nullptr);
            curl_easy_setopt(curl_, CURLOPT_POSTFIELDSIZE, 0L);
            curl_easy_setopt(curl_, CURLOPT_POST, 0L);
            curl_easy_setopt(curl_, CURLOPT_UPLOAD, 0L);
            curl_easy_setopt(curl_, CURLOPT_NOBODY, 0L);
            curl_easy_setopt(curl_, CURLOPT_HTTPGET, 1L);
        } else {
            curl_easy_setopt(curl_, CURLOPT_HTTPGET, 0L);
            curl_easy_setopt(curl_, CURLOPT_UPLOAD, 0L);
            curl_easy_setopt(curl_, CURLOPT_NOBODY, 0L);
            curl_easy_setopt(curl_, CURLOPT_POST, 1L);
            curl_easy_setopt(curl_, CURLOPT_POSTFIELDS, body.data());
            curl_easy_setopt(curl_, CURLOPT_POSTFIELDSIZE, static_cast<long>(body.size()));
        }

        const CURLcode rc = curl_easy_perform(curl_);
        curl_slist_free_all(headers);

        if (rc != CURLE_OK) {
            throw std::runtime_error(std::string("HTTPS request failed: ") + curl_easy_strerror(rc));
        }

        curl_easy_getinfo(curl_, CURLINFO_RESPONSE_CODE, &response.status);
        return response;
    }
};

static JsonPtr parseJson(const std::string& body) {
    json_tokener* tok = json_tokener_new();
    if (!tok) return JsonPtr(nullptr);
    json_object* obj = json_tokener_parse_ex(tok, body.c_str(), static_cast<int>(body.size()));
    const auto err = json_tokener_get_error(tok);
    json_tokener_free(tok);
    if (err != json_tokener_success || !obj) return JsonPtr(nullptr);
    return JsonPtr(obj);
}

static std::string jsonString(json_object* root, const char* key, const std::string& fallback = "") {
    if (!root || json_object_get_type(root) != json_type_object) return fallback;
    json_object* value = nullptr;
    if (!json_object_object_get_ex(root, key, &value) || !value) return fallback;
    if (json_object_get_type(value) == json_type_string) return json_object_get_string(value);
    if (json_object_get_type(value) == json_type_null) return fallback;
    return json_object_to_json_string_ext(value, JSON_C_TO_STRING_PLAIN);
}

static bool jsonBoolish(json_object* root, const char* key, bool fallback = false) {
    if (!root || json_object_get_type(root) != json_type_object) return fallback;
    json_object* value = nullptr;
    if (!json_object_object_get_ex(root, key, &value) || !value) return fallback;
    switch (json_object_get_type(value)) {
        case json_type_boolean: return json_object_get_boolean(value) != 0;
        case json_type_int: return json_object_get_int64(value) != 0;
        case json_type_double: return json_object_get_double(value) != 0.0;
        case json_type_string: {
            const std::string s = lower(json_object_get_string(value));
            return s == "1" || s == "true" || s == "yes";
        }
        default: return fallback;
    }
}

static double jsonDouble(json_object* root, const char* key, double fallback = 0.0) {
    if (!root || json_object_get_type(root) != json_type_object) return fallback;
    json_object* value = nullptr;
    if (!json_object_object_get_ex(root, key, &value) || !value) return fallback;
    if (json_object_get_type(value) == json_type_string) {
        try { return std::stod(json_object_get_string(value)); }
        catch (...) { return fallback; }
    }
    return json_object_get_double(value);
}

static std::string jsonValueText(json_object* root, const char* key, const std::string& fallback = "0") {
    if (!root || json_object_get_type(root) != json_type_object) return fallback;
    json_object* value = nullptr;
    if (!json_object_object_get_ex(root, key, &value) || !value ||
        json_object_get_type(value) == json_type_null) {
        return fallback;
    }
    if (json_object_get_type(value) == json_type_string) {
        return json_object_get_string(value);
    }
    return json_object_to_json_string_ext(value, JSON_C_TO_STRING_PLAIN);
}

static std::int64_t jsonInt64(json_object* root, const char* key, std::int64_t fallback = 0) {
    if (!root || json_object_get_type(root) != json_type_object) return fallback;
    json_object* value = nullptr;
    if (!json_object_object_get_ex(root, key, &value) || !value) return fallback;
    if (json_object_get_type(value) == json_type_string) {
        try { return std::stoll(json_object_get_string(value)); }
        catch (...) { return fallback; }
    }
    return json_object_get_int64(value);
}

static std::string makeJsonObject(const std::function<void(json_object*)>& builder) {
    JsonPtr obj(json_object_new_object());
    if (!obj) throw std::runtime_error("Unable to allocate JSON object");
    builder(obj.get());
    return json_object_to_json_string_ext(obj.get(), JSON_C_TO_STRING_PLAIN);
}

static std::string quoteJsonString(const std::string& value) {
    JsonPtr s(json_object_new_string_len(value.c_str(), static_cast<int>(value.size())));
    if (!s) throw std::runtime_error("Unable to encode seed phrase");
    return json_object_to_json_string_ext(s.get(), JSON_C_TO_STRING_PLAIN);
}

class IndeterminateBar {
public:
    explicit IndeterminateBar(std::string label) : label_(std::move(label)) {
        running_ = true;
        worker_ = std::thread([this] { loop(); });
    }

    ~IndeterminateBar() {
        if (running_) finish(false);
    }

    void finish(bool ok) {
        running_ = false;
        if (worker_.joinable()) worker_.join();
        std::cout << "\r" << std::left << std::setw(22) << label_ << " [";
        std::cout << (ok ? "========================" : "------------------------");
        std::cout << "] " << (ok ? "done" : "failed") << "          \n";
    }

private:
    std::string label_;
    std::atomic<bool> running_{false};
    std::thread worker_;

    void loop() {
        constexpr int width = 24;
        int pos = 0;
        int dir = 1;
        while (running_) {
            std::string bar(width, '.');
            for (int i = 0; i < 5; ++i) {
                int p = pos + i;
                if (p >= 0 && p < width) bar[static_cast<std::size_t>(p)] = '=';
            }
            std::cout << "\r" << std::left << std::setw(22) << label_ << " [" << bar << "]" << std::flush;
            pos += dir;
            if (pos >= width - 5) dir = -1;
            if (pos <= 0) dir = 1;
            std::this_thread::sleep_for(90ms);
        }
    }
};

static std::string formatAtomic(std::int64_t atomic) {
    const bool negative = atomic < 0;
    std::uint64_t v = negative ? static_cast<std::uint64_t>(-atomic) : static_cast<std::uint64_t>(atomic);
    const std::uint64_t whole = v / 1000000000ULL;
    const std::uint64_t frac = v % 1000000000ULL;
    std::ostringstream out;
    if (negative) out << '-';
    out << whole;
    if (frac != 0) {
        out << '.' << std::setw(9) << std::setfill('0') << frac;
        std::string s = out.str();
        while (!s.empty() && s.back() == '0') s.pop_back();
        return s;
    }
    return out.str();
}

static std::string formatDate(std::int64_t unixTime) {
    if (unixTime <= 0) return "-";
    std::time_t t = static_cast<std::time_t>(unixTime);
    std::tm tm{};
#ifdef _WIN32
    localtime_s(&tm, &t);
#else
    localtime_r(&t, &tm);
#endif
    char buf[32]{};
    std::strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", &tm);
    return buf;
}

static std::string transactionType(json_object* tx) {
    std::string type = lower(jsonString(tx, "type"));
    if (type == "in") return "receive";
    if (type == "out") type = "send";
    if (type == "burn") return "burn";

    json_object* raw = nullptr;
    if (json_object_object_get_ex(tx, "transaction_type", &raw) && raw &&
        json_object_get_type(raw) != json_type_null) {
        if (json_object_get_type(raw) == json_type_int) {
            const int code = json_object_get_int(raw);
            if (code == 5) return "burn";
            if (code == 6) return "stake";
            if (code == 7) return "return";
        } else {
            const std::string t = lower(json_object_get_string(raw));
            if (t == "5" || t == "burn") return "burn";
            if (t == "6" || t == "stake") return "stake";
            if (t == "7" || t == "return") return "return";
        }
    }
    return type.empty() ? "unknown" : type;
}

static std::string extractTxHash(json_object* root) {
    static const char* keys[] = {"tx_hash", "hash", "txid", "transaction_hash"};
    for (const char* key : keys) {
        const std::string value = jsonString(root, key);
        if (!value.empty()) return value;
    }

    json_object* hashes = nullptr;
    if (root && json_object_get_type(root) == json_type_object &&
        json_object_object_get_ex(root, "tx_hashes", &hashes) && hashes &&
        json_object_get_type(hashes) == json_type_array && json_object_array_length(hashes) > 0) {
        json_object* first = json_object_array_get_idx(hashes, 0);
        if (first && json_object_get_type(first) == json_type_string) return json_object_get_string(first);
    }
    return "";
}

struct Balances {
    double vnr = 0.0;
    double usdt = 0.0;
    double usdc = 0.0;
    std::string vnrAddress;
    std::string evmAddress;
};

class VeneraCli {
public:
    explicit VeneraCli(std::string seed) : seed_(std::move(seed)) {}

    ~VeneraCli() {
        cleanse(seed_);
    }

    bool openWallet() {
        if (!startRpcSession()) return false;
        if (!restoreSeed()) return false;
        if (!checkAndVerify2FA()) return false;
        // BE UPDATE CORE SYNC INDICATOR
        syncWallet(true);
        auto balances = fetchBalances(false, false);
        if (balances) printBalances(*balances);
        return true;
    }

    void commandLoop() {
        printHelp();
        std::vector<std::string> commandHistory;
        while (true) {
            std::string line;
            std::cout << "\n";
            const std::string prompt = std::string(ansi::bold) + ansi::brightMagenta + "venera" + ansi::reset + "> ";
            if (!readCommandLine(prompt, commandHistory, line)) break;
            line = trim(line);
            if (line.empty()) continue;

            const auto args = splitCommand(line);
            if (args.empty()) continue;
            const std::string cmd = lower(args[0]);

            try {
                if (cmd == "exit" || cmd == "quit") {
                    break;
                } else if (cmd == "help" || cmd == "?") {
                    printHelp();
                } else if (cmd == "balance" || cmd == "balances") {
                    // BE UPDATE
                    //
                    auto b = fetchBalances(false, true);
                    if (b) printBalances(*b);
                } else if (cmd == "receive") {
                    receive(args);
                } else if (cmd == "send") {
                    send(args);
                } else if (cmd == "burn") {
                    burn(args);
                } else if (cmd == "burnstats" || cmd == "burn-stats" || cmd == "bp" || cmd == "yield") {
                    burnStats();
                } else if (cmd == "collectyield" || cmd == "collect-yield" || cmd == "collect") {
                    collectYield();
                } else if (cmd == "stake") {
                    stake(args);
                } else if (cmd == "history") {
                    history();
                } else if (cmd == "refresh") {
                    syncWallet(true);
                    auto b = fetchBalances(false, false);
                    if (b) printBalances(*b);
                } else {
                    std::cout << "Unknown command. Type 'help'.\n";
                }
            } catch (const std::exception&) {
                std::cerr << ansi::red << "An error occurred" << ansi::reset << "\n";
            }
        }

        try { http_.postJson(cfg::LOGOUT, "{}"); } catch (...) {}
        std::cout << "Wallet closed.\n";
    }

private:
    HttpClient http_;
    std::string seed_;
    std::string walletAddress_;
    bool has2FA_ = false;
    bool twoFAVerified_ = false;

    bool startRpcSession() {
        const std::string body = makeJsonObject([](json_object* o) {
            json_object_object_add(o, "validate", json_object_new_string("yes"));
        });

        std::cout << ansi::dim << "Loading..." << ansi::reset << "\n";
        HttpResponse r = http_.postJson(cfg::SESSION_START, body);
        if (r.status != 200) {
            std::cerr << ansi::red << "An error occurred" << ansi::reset << "\n";
            return false;
        }

        auto root = parseJson(r.body);
        if (!root || jsonString(root.get(), "status") != "RPC started") {
            std::cerr << "Unexpected backend response.\n";
            return false;
        }

        return true;
    }

    bool restoreSeed() {
        if (splitWords(seed_).size() != 25) {
            std::cerr << "Seed phrase must contain exactly 25 words.\n";
            return false;
        }

        IndeterminateBar bar("Restoring wallet");
        HttpResponse r;
        try {
            r = http_.postRaw(cfg::RESTORE, quoteJsonString(seed_), "application/octet-stream");
        } catch (...) {
            bar.finish(false);
            throw;
        }
        const bool ok = r.status == 200;
        bar.finish(ok);

        if (!ok) {
            std::cerr << ansi::red << "An error occurred" << ansi::reset << "\n";
            return false;
        }

        auto root = parseJson(r.body);
        if (!root || jsonString(root.get(), "status") != "success") {
            std::cerr << ansi::red << "An error occurred" << ansi::reset << "\n";
            return false;
        }

        walletAddress_ = jsonString(root.get(), "address");
        return !walletAddress_.empty();
    }

    bool checkAndVerify2FA() {
        HttpResponse r = http_.postJson(cfg::TWOFA_VALIDATION, "{}");
        if (r.status != 200) {
            std::cerr << ansi::red << "An error occurred" << ansi::reset << "\n";
            return false;
        }

        auto root = parseJson(r.body);
        if (!root) {
            std::cerr << "Invalid 2FA status response.\n";
            return false;
        }

        has2FA_ = jsonBoolish(root.get(), "2fa_enabled", false);
        if (!has2FA_) {
            twoFAVerified_ = true;
            return true;
        }

        std::cout << "2FA enabled for this wallet.\n";
        for (;;) {
            std::cout << "OTP Code (6 digits, or 'q' to cancel): ";
            std::string otp;
            std::getline(std::cin, otp);
            otp = trim(otp);
            if (lower(otp) == "q") return false;
            if (otp.size() != 6 || !std::all_of(otp.begin(), otp.end(), [](unsigned char c){ return std::isdigit(c); })) {
                std::cout << "OTP must be exactly 6 digits.\n";
                continue;
            }

            const std::string body = makeJsonObject([&](json_object* o) {
                json_object_object_add(o, "code", json_object_new_string(otp.c_str()));
            });
            r = http_.postJson(cfg::TWOFA_VERIFY, body);
            auto verify = parseJson(r.body);
            if (r.status == 200 && verify && jsonBoolish(verify.get(), "success", false)) {
                twoFAVerified_ = true;
                std::cout << "2FA verified.\n";
                return true;
            }
            std::cout << "Invalid OTP code.\n";
        }
    }

    bool reconnect() {
        std::cout << "Session expired. Reconnecting wallet...\n";
        has2FA_ = false;
        twoFAVerified_ = false;
        if (!startRpcSession()) return false;
        if (!restoreSeed()) return false;
        return checkAndVerify2FA();
    }

    HttpResponse withRecovery(const std::function<HttpResponse()>& request) {
        HttpResponse r = request();
        if (r.status != 401) return r;
        if (!reconnect()) return r;
        return request();
    }

    bool syncWallet(bool verbose) {
        std::unique_ptr<IndeterminateBar> bar;
        if (verbose) bar = std::make_unique<IndeterminateBar>("Synchronizing");

        bool ok = true;
        try {
            HttpResponse r = withRecovery([&] { return http_.postJson(cfg::BLOCKCHAIN_SYNC, "{}"); });
            if (r.status != 200 && r.status != 202 && r.status != 204) ok = false;

            HttpResponse core = withRecovery([&] { return http_.postJson(cfg::SYNC_CORE, "{}"); });
            if (core.status != 200) ok = false;
        } catch (...) {
            if (bar) bar->finish(false);
            throw;
        }

        if (bar) bar->finish(ok);
        return ok;
    }

    std::optional<Balances> fetchBalances(bool syncFirst, bool showLoading = true) {
        if (syncFirst) syncWallet(false);

        std::unique_ptr<IndeterminateBar> bar;
        if (showLoading) bar = std::make_unique<IndeterminateBar>("Loading");

        HttpResponse r;
        try {
            r = withRecovery([&] { return http_.get(cfg::BALANCE); });
        } catch (...) {
            if (bar) bar->finish(false);
            throw;
        }

        if (bar) bar->finish(r.status == 200);

        if (r.status != 200) {
            std::cerr << ansi::red << "An error occurred" << ansi::reset << "\n";
            return std::nullopt;
        }

        auto root = parseJson(r.body);
        if (!root) {
            std::cerr << "Invalid balance response.\n";
            return std::nullopt;
        }

        Balances b;
        b.vnr = jsonDouble(root.get(), "vnr_balance");
        b.usdt = jsonDouble(root.get(), "usdt_balance");
        b.usdc = jsonDouble(root.get(), "usdc_balance");
        b.vnrAddress = jsonString(root.get(), "wallet_address");
        b.evmAddress = jsonString(root.get(), "usdt_address");
        if (!b.vnrAddress.empty()) walletAddress_ = b.vnrAddress;
        return b;
    }

    static void printBalances(const Balances& b) {
        std::cout << "\n" << ansi::bold << ansi::brightMagenta << "Balances" << ansi::reset << "\n"
                  << "  " << ansi::bold << "VNR " << ansi::reset << "  " << std::fixed << std::setprecision(9) << b.vnr << "\n"
                  << "  " << ansi::green << "USDT" << ansi::reset << "  " << std::fixed << std::setprecision(8) << b.usdt << "\n"
                  << "  " << ansi::cyan << "USDC" << ansi::reset << "  " << std::fixed << std::setprecision(8) << b.usdc << "\n";
    }

    void receive(const std::vector<std::string>& args) {
        auto b = fetchBalances(false);
        if (!b) return;

        const std::string asset = args.size() >= 2 ? lower(args[1]) : "all";
        if (asset == "vnr" || asset == "all") {
            std::cout << "VNR receive address:\n" << b->vnrAddress << "\n";
        }
        if (asset == "usdt" || asset == "usdc" || asset == "all") {
            std::cout << "USDT/USDC (BSC) receive address:\n"
                      << (b->evmAddress.empty() ? "(not available)" : b->evmAddress) << "\n";
        }
        if (asset != "vnr" && asset != "usdt" && asset != "usdc" && asset != "all") {
            std::cout << "Usage: receive [vnr|usdt|usdc]\n";
        }
    }

    void send(const std::vector<std::string>& args) {
        std::string asset = args.size() >= 2 ? lower(args[1]) : "";
        std::string recipient = args.size() >= 3 ? args[2] : "";
        std::string amount = args.size() >= 4 ? args[3] : "";

        if (asset.empty()) {
            std::cout << "Asset (VNR/USDT/USDC): ";
            std::getline(std::cin, asset);
            asset = lower(trim(asset));
        }
        if (recipient.empty()) {
            std::cout << "Recipient: ";
            std::getline(std::cin, recipient);
            recipient = trim(recipient);
        }
        if (amount.empty()) {
            std::cout << "Amount: ";
            std::getline(std::cin, amount);
            amount = trim(amount);
        }

        if (asset != "vnr" && asset != "usdt" && asset != "usdc") {
            std::cout << "Supported assets: VNR, USDT, USDC\n";
            return;
        }
        if (recipient.empty() || amount.empty()) {
            std::cout << "Recipient and amount are required.\n";
            return;
        }
        try {
            if (std::stod(amount) <= 0) throw std::runtime_error("Amount must be greater than zero");
        } catch (...) {
            std::cout << "Invalid amount.\n";
            return;
        }

        std::cout << "\n" << ansi::bold << "Transfer confirmation" << ansi::reset << "\n"
                  << "  Asset:     " << asset << "\n"
                  << "  Amount:    " << amount << "\n"
                  << "  Recipient: " << recipient << "\n";
        if (!yesNo("Send this transfer?", false)) {
            std::cout << ansi::dim << "Transfer cancelled." << ansi::reset << "\n";
            return;
        }

        std::string path;
        std::string body;
        if (asset == "vnr") {
            path = cfg::SEND_VNR;
            body = makeJsonObject([&](json_object* o) {
                json_object_object_add(o, "sendaddress", json_object_new_string(recipient.c_str()));
                json_object_object_add(o, "sendamount", json_object_new_string(amount.c_str()));
            });
        } else {
            path = asset == "usdt" ? cfg::SEND_USDT : cfg::SEND_USDC;
            body = makeJsonObject([&](json_object* o) {
                json_object_object_add(o, "recipient", json_object_new_string(recipient.c_str()));
                json_object_object_add(o, "amount", json_object_new_string(amount.c_str()));
            });
        }

        IndeterminateBar bar("Sending transfer");
        HttpResponse r;
        try {
            r = withRecovery([&] { return http_.postJson(path, body); });
        } catch (...) {
            bar.finish(false);
            throw;
        }
        bar.finish(r.status >= 200 && r.status < 300);
        auto root = parseJson(r.body);
        const std::string hash = root ? extractTxHash(root.get()) : "";
        if (r.status >= 200 && r.status < 300 && !hash.empty()) {
            std::cout << ansi::green << "Transfer submitted" << ansi::reset << "  " << asset << "\n"
                      << ansi::dim << "TX hash: " << ansi::reset << hash << "\n";
        } else {
            std::cerr << ansi::red << "An error occurred" << ansi::reset << "\n";
        }
    }

    void burn(const std::vector<std::string>& args) {
        std::string amount = args.size() >= 2 ? args[1] : "";
        if (amount.empty()) {
            std::cout << "VNR amount to burn: ";
            std::getline(std::cin, amount);
            amount = trim(amount);
        }
        if (amount.empty()) return;
        try {
            if (std::stod(amount) <= 0) throw std::runtime_error("invalid");
        } catch (...) {
            std::cout << "Invalid burn amount.\n";
            return;
        }

        std::cout << "\n" << ansi::yellow << ansi::bold << "Burn confirmation" << ansi::reset << "\n"
                  << "  Amount: " << amount << " VNR\n";
        if (!yesNo("Burn this VNR?", false)) {
            std::cout << ansi::dim << "Burn cancelled." << ansi::reset << "\n";
            return;
        }

        const std::string body = makeJsonObject([&](json_object* o) {
            json_object_object_add(o, "amount", json_object_new_string(amount.c_str()));
        });

        IndeterminateBar bar("Burning VNR");
        HttpResponse r;
        try {
            r = withRecovery([&] { return http_.postJson(cfg::BURN, body); });
        } catch (...) {
            bar.finish(false);
            throw;
        }
        bar.finish(r.status >= 200 && r.status < 300);
        auto root = parseJson(r.body);
        const std::string hash = root ? extractTxHash(root.get()) : "";
        if (r.status >= 200 && r.status < 300 && !hash.empty()) {
            std::cout << ansi::yellow << "Burn submitted" << ansi::reset << "\n"
                      << ansi::dim << "TX hash: " << ansi::reset << hash << "\n";
        } else {
            std::cerr << ansi::red << "An error occurred" << ansi::reset << "\n";
        }
    }

    void burnStats() {
        IndeterminateBar bar("Loading");
        HttpResponse r;
        try {
            r = withRecovery([&] { return http_.get(cfg::BURN_STATS); });
        } catch (...) {
            bar.finish(false);
            throw;
        }
        bar.finish(r.status == 200);

        if (r.status != 200) {
            std::cerr << ansi::red << "An error occurred" << ansi::reset << "\n";
            return;
        }

        auto root = parseJson(r.body);
        if (!root || json_object_get_type(root.get()) != json_type_object) {
            std::cerr << ansi::red << "Invalid burn stats response.\n" << ansi::reset;
            return;
        }

        const double totalBurned = jsonDouble(root.get(), "total_burned", 0.0);
        const std::int64_t burnTransactions = jsonInt64(root.get(), "burn_transactions", 0);
        const double burnPointsRaw = jsonDouble(root.get(), "burn_points", 0.0);
        const std::uint64_t burnPoints = burnPointsRaw > 0.0
            ? static_cast<std::uint64_t>(std::floor(burnPointsRaw))
            : 0;
        const double accumulated = jsonDouble(root.get(), "accumulated_usdt", 0.0);
        const double earnedTotal = jsonDouble(root.get(), "yield_earned_total_usdt", 0.0);
        const double pointsPerVnr = jsonDouble(root.get(), "current_points_per_vnr", 0.0);
        const double rolling7d = jsonDouble(root.get(), "rolling_7d_burned_vnr", 0.0);
        const std::int64_t expiryDays = jsonInt64(root.get(), "expiry_days", 0);

        std::cout << "\n" << ansi::bold << ansi::yellow << "Burn-to-Earn" << ansi::reset << "\n"
                  << "  Burn Points         " << ansi::bold << burnPoints << ansi::reset << " BP\n"
                  << "  Available Yield     " << ansi::green << std::fixed << std::setprecision(8) << accumulated << " USDT" << ansi::reset << "\n"
                  << "  Yield Earned Total  " << std::setprecision(8) << earnedTotal << " USDT\n"
                  << "\n" << ansi::dim
                  << "  Total Burned        " << std::setprecision(9) << totalBurned << " VNR\n"
                  << "  Burn Transactions   " << burnTransactions << "\n"
                  << "  Current BP / VNR    " << std::setprecision(8) << pointsPerVnr << "\n"
                  << "  Rolling 7d Burn     " << std::setprecision(9) << rolling7d << " VNR\n"
                  << "  BP Expiry           " << expiryDays << " days" << ansi::reset << "\n";

        if (accumulated > 0) {
            std::cout << ansi::dim << "  Use 'collectyield' to collect available USDT."
                      << ansi::reset << "\n";
        }
    }

    void collectYield() {
        if (!yesNo("Collect available USDT yield?", false)) {
            std::cout << ansi::dim << "Yield collection cancelled." << ansi::reset << "\n";
            return;
        }

        IndeterminateBar bar("Collecting yield");
        HttpResponse r;
        try {
            r = withRecovery([&] { return http_.postJson(cfg::COLLECT_YIELD, "{}"); });
        } catch (...) {
            bar.finish(false);
            throw;
        }

        auto root = parseJson(r.body);
        const std::string hash = root ? extractTxHash(root.get()) : "";
        const bool accepted = (r.status >= 200 && r.status < 300);
        bar.finish(accepted);

        if (r.status == 202) {
            std::cout << ansi::yellow << "Collection broadcast, confirmation pending." << ansi::reset << "\n";
            if (!hash.empty()) {
                std::cout << ansi::dim << "TX hash: " << ansi::reset << hash << "\n";
            }
            return;
        }

        if (!accepted || !root || !jsonBoolish(root.get(), "success", false)) {
            std::cerr << ansi::red << "An error occurred" << ansi::reset << "\n";
            return;
        }

        const double amount = jsonDouble(root.get(), "amount_usdt", 0.0);
        const std::string to = jsonString(root.get(), "to");
        std::cout << ansi::green << ansi::bold << "Yield collected" << ansi::reset
                  << "  " << std::fixed << std::setprecision(8) << amount << " USDT\n";
        if (!to.empty()) {
            std::cout << ansi::dim << "To:      " << ansi::reset << to << "\n";
        }
        if (!hash.empty()) {
            std::cout << ansi::dim << "TX hash: " << ansi::reset << hash << "\n";
        }
    }

    void stake(const std::vector<std::string>& args) {
        std::string amount = args.size() >= 2 ? args[1] : "";
        std::string daysText = args.size() >= 3 ? args[2] : "";

        if (amount.empty()) {
            std::cout << "VNR amount to stake: ";
            std::getline(std::cin, amount);
            amount = trim(amount);
        }
        if (daysText.empty()) {
            std::cout << "Lock period (30/60/120/360 days): ";
            std::getline(std::cin, daysText);
            daysText = trim(daysText);
        }

        int days = 0;
        double stakeAmount = 0.0;
        try { days = std::stoi(daysText); } catch (...) { days = 0; }
        if (days != 30 && days != 60 && days != 120 && days != 360) {
            std::cout << "Lock period must be 30, 60, 120, or 360 days.\n";
            return;
        }
        try {
            stakeAmount = std::stod(amount);
            if (stakeAmount <= 0) throw std::runtime_error("invalid");
        } catch (...) {
            std::cout << "Invalid stake amount.\n";
            return;
        }

        std::cout << "\n" << ansi::cyan << ansi::bold << "Stake confirmation" << ansi::reset << "\n"
                  << "  Amount: " << amount << " VNR\n"
                  << "  Lock:   " << days << " days\n";
        if (!yesNo("Create this stake?", false)) {
            std::cout << ansi::dim << "Stake cancelled." << ansi::reset << "\n";
            return;
        }

        // BE UPDATED
        const std::string body = makeJsonObject([&](json_object* o) {
            json_object_object_add(o, "amount", json_object_new_double(stakeAmount));
            json_object_object_add(o, "lock", json_object_new_int(days));
        });

        IndeterminateBar bar("Creating stake");
        HttpResponse r;
        try {
            r = withRecovery([&] { return http_.postJson(cfg::STAKE_ENDPOINT, body); });
        } catch (...) {
            bar.finish(false);
            throw;
        }
        const bool ok = r.status >= 200 && r.status < 300;
        bar.finish(ok);

        auto root = parseJson(r.body);
        const std::string hash = root ? extractTxHash(root.get()) : "";
        if (ok && root && !hash.empty()) {
            double amountOut = 0.0;
            double amountSTotal = 0.0;
            double amountOutTotal = 0.0;
            std::int64_t timestampOut = 0;

            json_object* stakeObj = nullptr;
            if (json_object_object_get_ex(root.get(), "stake", &stakeObj) && stakeObj &&
                json_object_get_type(stakeObj) == json_type_object) {
                amountOut = jsonDouble(stakeObj, "amount_out", 0.0);
                timestampOut = jsonInt64(stakeObj, "timestamp_out", 0);
            }

            json_object* totalsObj = nullptr;
            if (json_object_object_get_ex(root.get(), "totals", &totalsObj) && totalsObj &&
                json_object_get_type(totalsObj) == json_type_object) {
                amountSTotal = jsonDouble(totalsObj, "amount_s_total", 0.0);
                amountOutTotal = jsonDouble(totalsObj, "amount_out_total", 0.0);
            }

            std::cout << ansi::cyan << ansi::bold << "Stake submitted" << ansi::reset << "\n"
                      << "  Staked:       " << std::fixed << std::setprecision(9) << stakeAmount << " VNR\n"
                      << "  Lock:         " << days << " days\n";
            if (amountOut > 0.0) {
                std::cout << "  Amount out:   " << std::setprecision(9) << amountOut << " VNR\n";
            }
            if (timestampOut > 0) {
                std::cout << "  Unlock time:  " << formatDate(timestampOut) << "\n";
            }
            if (amountSTotal > 0.0 || amountOutTotal > 0.0) {
                std::cout << ansi::dim
                          << "  Total staked:  " << std::setprecision(9) << amountSTotal << " VNR\n"
                          << "  Total output:  " << std::setprecision(9) << amountOutTotal << " VNR"
                          << ansi::reset << "\n";
            }
            std::cout << ansi::dim << "TX hash: " << ansi::reset << hash << "\n";
        } else {
            std::cerr << ansi::red << "An error occurred" << ansi::reset << "\n";
        }
    }

    void history() {
        IndeterminateBar bar("Loading");
        HttpResponse r;
        try {
            r = withRecovery([&] { return http_.get(cfg::HISTORY); });
        } catch (...) {
            bar.finish(false);
            throw;
        }
        bar.finish(r.status == 200 || r.status == 404);
        if (r.status == 404) {
            std::cout << "No transactions found.\n";
            return;
        }
        if (r.status != 200) {
            std::cerr << ansi::red << "An error occurred" << ansi::reset << "\n";
            return;
        }

        auto root = parseJson(r.body);
        if (!root || json_object_get_type(root.get()) != json_type_array) {
            std::cerr << "Invalid history response.\n";
            return;
        }

        const std::size_t total = json_object_array_length(root.get());
        const std::size_t count = std::min<std::size_t>(100, total);
        if (count == 0) {
            std::cout << "No transactions found.\n";
            return;
        }

        std::cout << "\n" << ansi::bold << ansi::brightMagenta << "Last " << count << " transactions" << ansi::reset << "\n";
        std::cout << std::left
                  << std::setw(10) << "TYPE"
                  << std::setw(20) << "AMOUNT (VNR)"
                  << std::setw(21) << "DATE"
                  << "HASH\n";
        std::cout << ansi::dim << std::string(118, '-') << ansi::reset << "\n";

        for (std::size_t i = 0; i < count; ++i) {
            json_object* tx = json_object_array_get_idx(root.get(), i);
            if (!tx || json_object_get_type(tx) != json_type_object) continue;
            const auto amount = jsonInt64(tx, "amount", 0);
            const auto timestamp = jsonInt64(tx, "timestamp", 0);
            const std::string hash = jsonString(tx, "tx_hash", "-");
            const std::string type = transactionType(tx);

            const char* color = ansi::white;
            if (type == "receive" || type == "return") color = ansi::green;
            else if (type == "send") color = ansi::red;
            else if (type == "burn") color = ansi::yellow;
            else if (type == "stake") color = ansi::cyan;

            std::cout << color << std::left
                      << std::setw(10) << type
                      << std::setw(20) << formatAtomic(amount)
                      << ansi::reset
                      << std::setw(21) << formatDate(timestamp)
                      << ansi::dim << hash << ansi::reset << "\n";
        }
    }

    static void printHelp() {
        std::cout << "\n" << ansi::bold << ansi::brightMagenta << "Commands" << ansi::reset << "\n"
                  << "  " << ansi::bold << "balance" << ansi::reset << "                         Show VNR / USDT / USDC balances\n"
                  << "  " << ansi::bold << "receive" << ansi::reset << " [vnr|usdt|usdc]         Show receive address\n"
                  << "  " << ansi::bold << "send" << ansi::reset << " [asset] [address] [amount]   Send VNR / USDT / USDC\n"
                  << "  " << ansi::bold << "stake" << ansi::reset << " [amount] [30|60|120|360]   Stake VNR\n"
                  << "  " << ansi::yellow << ansi::bold << "burn" << ansi::reset << " [amount]                    Burn VNR\n"
                  << "  " << ansi::yellow << ansi::bold << "burnstats" << ansi::reset << "                       Burned VNR, BP and USDT yield\n"
                  << "  " << ansi::green << ansi::bold << "collectyield" << ansi::reset << "                    Collect available USDT yield\n"
                  << "  " << ansi::bold << "history" << ansi::reset << "                         Last 100 transactions\n"
                  << "  " << ansi::bold << "refresh" << ansi::reset << "                         Refresh wallet from chain\n"
                  << "  " << ansi::bold << "help" << ansi::reset << "                            Show commands\n"
                  << "  " << ansi::bold << "exit" << ansi::reset << "                            Close wallet\n"
                  << ansi::dim << "  Tip: use Up/Down arrows to browse command history." << ansi::reset << "\n";
    }
};

static void printBanner() {
    std::cout << "\n" << ansi::brightMagenta << ansi::bold
              << "==============================================================\n"
              << "                     Venera CLI v0.2.1\n"
              << "=============================================================="
              << ansi::reset << "\n";
}

static bool saveSeedInteractive(const std::string& seed, bool defaultYes) {
    if (!yesNo("Save encrypted wallet file?", defaultYes)) return true;

    std::cout << "Wallet file name: ";
    std::string filename;
    std::getline(std::cin, filename);
    filename = walletPath(filename);
    if (filename == ".keys") {
        std::cerr << "Invalid wallet file name.\n";
        return false;
    }

    if (fs::exists(filename) && !yesNo("File already exists. Overwrite?", false)) {
        return false;
    }

    std::string password = readHidden("Wallet password: ");
    std::string confirm = readHidden("Confirm password: ");
    if (password.empty()) {
        cleanse(confirm);
        std::cerr << "Wallet password cannot be empty.\n";
        return false;
    }
    if (password != confirm) {
        cleanse(password);
        cleanse(confirm);
        std::cerr << "Passwords do not match.\n";
        return false;
    }

    try {
        encryptWalletFile(filename, seed, password);
        std::cout << "Wallet saved to " << filename << "\n";
    } catch (...) {
        cleanse(password);
        cleanse(confirm);
        throw;
    }

    cleanse(password);
    cleanse(confirm);
    return true;
}

static std::optional<std::string> loadSeedFromFile(const std::string& input) {
    const std::string filename = walletPath(input);
    if (!fs::exists(filename)) {
        std::cerr << "Wallet file not found: " << filename << "\n";
        return std::nullopt;
    }

    for (int attempt = 1; attempt <= 3; ++attempt) {
        std::string password = readHidden("Wallet password: ");
        try {
            std::string seed = decryptWalletFile(filename, password);
            cleanse(password);
            if (splitWords(seed).size() != 25) {
                cleanse(seed);
                throw std::runtime_error("Decrypted wallet does not contain a 25-word seed");
            }
            return seed;
        } catch (const std::exception& e) {
            cleanse(password);
            std::cerr << e.what() << "\n";
            if (attempt == 3) return std::nullopt;
        }
    }
    return std::nullopt;
}

static std::optional<std::string> createWalletOnServer() {
    HttpClient http;

    const std::string startBody = makeJsonObject([](json_object* o) {
        json_object_object_add(o, "validate", json_object_new_string("yes"));
    });

    std::cout << ansi::dim << "Loading..." << ansi::reset << "\n";
    HttpResponse start = http.postJson(cfg::SESSION_START, startBody);
    if (start.status != 200) {
        std::cerr << ansi::red << "An error occurred" << ansi::reset << "\n";
        return std::nullopt;
    }

    IndeterminateBar bar("Creating wallet");
    HttpResponse created;
    try {
        created = http.postJson(cfg::CREATE, "{}");
    } catch (...) {
        bar.finish(false);
        throw;
    }
    bar.finish(created.status == 200);

    if (created.status != 200) {
        std::cerr << ansi::red << "An error occurred" << ansi::reset << "\n";
        return std::nullopt;
    }

    auto root = parseJson(created.body);
    if (!root || jsonString(root.get(), "success") != "yes") {
        std::cerr << ansi::red << "An error occurred" << ansi::reset << "\n";
        return std::nullopt;
    }

    std::string seed = jsonString(root.get(), "mnemonic_key");
    if (splitWords(seed).size() != 25) {
        cleanse(seed);
        std::cerr << "Backend returned an invalid mnemonic.\n";
        return std::nullopt;
    }

    return seed;
}

int main() {
    try {
        enableAnsiColors();
        printBanner();
        std::cout << "Wallet file name, 25-word seed phrase, or 'create':\n> ";
        std::string input;
        std::getline(std::cin, input);
        input = trim(input);
        if (input.empty()) {
            std::cerr << "No wallet input provided.\n";
            return 1;
        }

        std::string seed;

        if (lower(input) == "create") {
            auto generated = createWalletOnServer();
            if (!generated) return 1;
            seed = std::move(*generated);

            std::cout << "\nNEW VENERA WALLET\n"
                      << "Write down this 25-word seed and keep it offline:\n\n"
                      << seed << "\n\n";

            if (!saveSeedInteractive(seed, true)) {
                std::cerr << "Wallet was created but not saved locally. Keep the seed above safe.\n";
            }
        } else if (splitWords(input).size() == 25) {
            seed = input;
            if (!saveSeedInteractive(seed, false)) {
                std::cerr << "Wallet seed will only remain in memory for this session.\n";
            }
        } else {
            auto loaded = loadSeedFromFile(input);
            if (!loaded) return 1;
            seed = std::move(*loaded);
            std::cout << "Encrypted wallet file unlocked.\n";
        }

        if (splitWords(seed).size() != 25) {
            cleanse(seed);
            std::cerr << "Seed phrase must contain exactly 25 words.\n";
            return 1;
        }

        VeneraCli app(std::move(seed));
        if (!app.openWallet()) return 1;
        app.commandLoop();
        return 0;

    } catch (const std::exception&) {
        std::cerr << ansi::red << "An error occurred" << ansi::reset << "\n";
        return 1;
    }
}
