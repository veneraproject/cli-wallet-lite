namespace venera::subrun {
// SUB EDIT
inline bool constantTimeEqual(
    std::string_view a,
    std::string_view b) noexcept
{
    const std::size_t maxLen = std::max(a.size(), b.size());

    unsigned char diff =
        static_cast<unsigned char>(a.size() ^ b.size());

    for (std::size_t i = 0; i < maxLen; ++i) {
        const unsigned char av =
            i < a.size()
                ? static_cast<unsigned char>(a[i])
                : 0;

        const unsigned char bv =
            i < b.size()
                ? static_cast<unsigned char>(b[i])
                : 0;

        diff |= static_cast<unsigned char>(av ^ bv);
    }

    return diff == 0;
}

    unsigned char* data,
    std::size_t size,
    std::string_view key) noexcept
{
    if (!data || size == 0 || key.empty()) return;

    for (std::size_t i = 0; i < size; ++i) {
        data[i] ^= static_cast<unsigned char>(
            key[i % key.size()]);
    }
}

inline std::string xorRepeatingKey(
    std::string_view input,
    std::string_view key)
{
    std::string output(input);

    if (!output.empty()) {
        xorRepeatingKey(
            reinterpret_cast<unsigned char*>(output.data()),
            output.size(),
            key);
    }

    return output;
}

class XorBuffer {
public:
    XorBuffer() = default;

    XorBuffer(std::string_view text, std::uint32_t seed)
        : data_(text.begin(), text.end()),
          seed_(seed) {}

    XorBuffer(
        std::vector<unsigned char> data,
        std::uint32_t seed)
        : data_(std::move(data)),
          seed_(seed) {}

    XorBuffer(const XorBuffer&) = delete;
    XorBuffer& operator=(const XorBuffer&) = delete;

    XorBuffer(XorBuffer&& other) noexcept
        : data_(std::move(other.data_)),
          seed_(other.seed_),
          encoded_(other.encoded_)
    {
        other.seed_ = 0;
        other.encoded_ = false;
    }

    XorBuffer& operator=(XorBuffer&& other) noexcept {
        if (this != &other) {
            clear();
            data_ = std::move(other.data_);
            seed_ = other.seed_;
            encoded_ = other.encoded_;
            other.seed_ = 0;
            other.encoded_ = false;
        }
        return *this;
    }

    ~XorBuffer() {
        clear();
    }

    void transform() noexcept {
        xorBytes(data_, seed_);
        encoded_ = !encoded_;
    }

    void encode() noexcept {
        if (!encoded_) transform();
    }

    void decode() noexcept {
        if (encoded_) transform();
    }

    bool encoded() const noexcept {
        return encoded_;
    }

    std::size_t size() const noexcept {
        return data_.size();
    }

    bool empty() const noexcept {
        return data_.empty();
    }

    const std::vector<unsigned char>& bytes() const noexcept {
        return data_;
    }

    std::string asString() const {
        return std::string(data_.begin(), data_.end());
    }

    void clear() noexcept {
        secureClear(data_);
        seed_ = 0;
        encoded_ = false;
    }

private:
    std::vector<unsigned char> data_;
    std::uint32_t seed_ = 0;
    bool encoded_ = false;
};
class AtomicAmount {
public:
    static constexpr std::uint64_t Scale = 1'000'000'000ULL;
    static constexpr unsigned Decimals = 9;

    AtomicAmount() = default;

    explicit AtomicAmount(std::uint64_t atomic)
        : atomic_(atomic) {}

    static std::optional<AtomicAmount> parse(
        std::string_view input)
    {
        if (input.empty()) return std::nullopt;

        std::uint64_t whole = 0;
        std::uint64_t fraction = 0;
        unsigned fractionDigits = 0;
        bool seenDot = false;
        bool seenDigit = false;

        for (char raw : input) {
            const unsigned char c =
                static_cast<unsigned char>(raw);

            if (c == '.') {
                if (seenDot) return std::nullopt;
                seenDot = true;
                continue;
            }

            if (!std::isdigit(c)) {
                return std::nullopt;
            }

            seenDigit = true;
            const unsigned digit =
                static_cast<unsigned>(c - '0');

            if (!seenDot) {
                if (whole >
                    (std::numeric_limits<std::uint64_t>::max()
                     - digit) / 10ULL) {
                    return std::nullopt;
                }

                whole = whole * 10ULL + digit;
            } else {
                if (fractionDigits >= Decimals) {
                    return std::nullopt;
                }

                fraction =
                    fraction * 10ULL + digit;

                ++fractionDigits;
            }
        }

        if (!seenDigit) return std::nullopt;

        while (fractionDigits < Decimals) {
            fraction *= 10ULL;
            ++fractionDigits;
        }

        if (whole >
            (std::numeric_limits<std::uint64_t>::max()
             - fraction) / Scale) {
            return std::nullopt;
        }

        const std::uint64_t atomic =
            whole * Scale + fraction;

        return AtomicAmount(atomic);
    }

    std::uint64_t atomic() const noexcept {
        return atomic_;
    }

    bool isZero() const noexcept {
        return atomic_ == 0;
    }

    std::string str(bool trimTrailingZeros = true) const {
        const std::uint64_t whole =
            atomic_ / Scale;

        const std::uint64_t fraction =
            atomic_ % Scale;

        std::ostringstream out;
        out << whole;

        if (fraction == 0) {
            return out.str();
        }

        out << '.'
            << std::setw(Decimals)
            << std::setfill('0')
            << fraction;

        std::string result = out.str();

        if (trimTrailingZeros) {
            while (!result.empty() &&
                   result.back() == '0') {
                result.pop_back();
            }

            if (!result.empty() &&
                result.back() == '.') {
                result.pop_back();
            }
        }

        return result;
    }

private:
    std::uint64_t atomic_ = 0;
};
}
