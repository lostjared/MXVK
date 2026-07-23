
/**
 * @file argz.hpp
 * @brief Lightweight, header-only, template command-line argument parser.
 *
 * Supports both short options (@c -x) and long options (@c --name), with or
 * without values, for any string type that satisfies the StringType concept
 * (typically @c std::string or @c std::wstring).
 *
 * Typical usage:
 * @code
 * Argz<std::string> parser(argc, argv);
 * parser.addOptionSingle('h', "Show help")
 *       .addOptionSingleValue('o', "Output file");
 * Argument<std::string> arg;
 * int code;
 * while ((code = parser.proc(arg)) != -1) { ... }
 * @endcode
 *
 * A convenience wrapper proc_args() provides a pre-built parser that handles
 * the options common to all libmx2 applications (-p path, -r resolution, -f fullscreen, …).
 */

#ifndef _ARGZ_HPP_X
#define _ARGZ_HPP_X

#include "mxvk_runtime_options.hpp"
#include <algorithm>
#include <cctype>
#include <charconv>
#include <cmath>
#include <cstddef>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <iomanip>
#include <iostream>
#include <iterator>
#include <ranges>
#include <string>
#include <string_view>
#include <system_error>
#include <type_traits>
#include <unordered_map>
#include <utility>
#include <vector>

#if defined(_WIN32)
#include <io.h>
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <unistd.h>
#endif

/**
 * @concept StringType
 * @brief Models a string-like class with indexing, concatenation, length, and value_type.
 *
 * Both @c std::string and @c std::wstring satisfy this concept.
 * @tparam T The type to constrain.
 */
template <typename T>
concept StringType = std::is_class_v<T> && requires(T type) {
    type.length();
    type[0];
    type += type;
    type = type;
    typename T::value_type;
    typename T::size_type;
    { type.length() } -> std::same_as<typename T::size_type>;
    { type[0] } -> std::same_as<typename T::value_type &>;
    { type += T{} } -> std::same_as<T &>;
    { type = T{} } -> std::same_as<T &>;
};

[[nodiscard]] inline std::string executable_directory(const char *executable) {
#if defined(_WIN32)
    std::vector<char> buffer(32768);
    const DWORD length = GetModuleFileNameA(nullptr, buffer.data(), static_cast<DWORD>(buffer.size()));
    if (length > 0 && length < buffer.size()) {
        return std::filesystem::path(std::string(buffer.data(), length)).parent_path().lexically_normal().string();
    }
#elif defined(__linux__)
    std::vector<char> buffer(4096);
    const ssize_t length = readlink("/proc/self/exe", buffer.data(), buffer.size());
    if (length > 0 && static_cast<std::size_t>(length) < buffer.size()) {
        return std::filesystem::path(std::string(buffer.data(), static_cast<std::size_t>(length))).parent_path().lexically_normal().string();
    }
#endif

    std::error_code error;
    const std::filesystem::path absolute_path = std::filesystem::absolute(executable == nullptr ? "." : executable, error);
    if (!error && absolute_path.has_parent_path()) {
        return absolute_path.parent_path().lexically_normal().string();
    }
    return ".";
}

/** @brief Discriminator for option kind used inside Argument. */
enum class ArgType {
    ARG_SINGLE,
    ARG_SINGLE_VALUE,
    ARG_DOUBLE,
    ARG_DOUBLE_VALUE,
    ARG_NONE
};

/**
 * @struct Argument
 * @brief Describes a single parsed argument entry returned by Argz::proc().
 * @tparam String String type satisfying StringType.
 *
 * After a successful call to Argz::proc() the member @c arg_letter holds the
 * matched code (or @c '-' for bare positional arguments) and @c arg_value holds
 * any associated value string.
 */
template <StringType String>
struct Argument {
    String arg_name;  ///< Long option name (e.g. @c "output").
    int arg_letter;   ///< Short option code (e.g. @c 'o') or unique integer for long-only options.
    String arg_value; ///< Value string supplied after the option, if any.
    ArgType arg_type; ///< Which @c ArgType variant this argument represents.
    String desc;      ///< Human-readable description used in help output.
    ~Argument() = default;
    Argument() : arg_name{}, arg_letter{}, arg_value{}, arg_type{}, desc{} {}
    Argument(const Argument &a) : arg_name{a.arg_name}, arg_letter{a.arg_letter}, arg_value{a.arg_value}, arg_type{a.arg_type}, desc{a.desc} {}
    Argument &operator=(const Argument<String> &a) {
        arg_name = a.arg_name;
        arg_letter = a.arg_letter;
        arg_value = a.arg_value;
        arg_type = a.arg_type;
        desc = a.desc;
        return *this;
    }
    auto operator<=>(const Argument<String> &a) const { return (arg_letter <=> a.arg_letter); }
    bool operator==(const Argument<String> &a) const { return arg_letter == a.arg_letter; }
};

/**
 * @struct ArgumentData
 * @brief Holds the raw @c argv strings converted to the target @c String type.
 * @tparam String String type satisfying StringType.
 */
template <StringType String>
struct ArgumentData {
    std::vector<String> args; ///< Converted argv entries (argv[1] … argv[argc-1]).
    int argc;                 ///< Original argc value.
    ~ArgumentData() = default;
    ArgumentData() = default;
    ArgumentData(const ArgumentData<String> &a) : args{a.args}, argc{a.argc} {}
    ArgumentData &operator=(const ArgumentData<String> &a) {
        if (this == &a) {
            return *this;
        }
        args = a.args;
        argc = a.argc;
        return *this;
    }
    ArgumentData(ArgumentData<String> &&a) : args{std::move(a.args)}, argc{a.argc} {}
    ArgumentData<String> &operator=(ArgumentData<String> &&a) {
        if (this == &a) {
            return *this;
        }
        args = std::move(a.args);
        argc = a.argc;
        return *this;
    }
};

/**
 * @class ArgException
 * @brief Exception thrown by Argz::proc() on unrecognised or malformed options.
 * @tparam String String type satisfying StringType.
 */
template <StringType String>
class ArgException {
  public:
    ArgException() = default;
    ArgException(const String &s) : value{s} {}
    String text() const { return value; }

  private:
    String value;
};

/**
 * @class Argz
 * @brief Template command-line argument parser.
 * @tparam String String type satisfying StringType (usually @c std::string or @c std::wstring).
 *
 * Options are registered with addOption*() before parsing.
 * Call proc() in a loop until it returns @c -1 to iterate over all arguments.
 */
template <StringType String>
class Argz {
  public:
    ~Argz() = default;
    Argz() = default;

    /**
     * @brief Construct and immediately ingest argv.
     * @param argc Argument count from main().
     * @param argv Argument vector from main().
     */
    Argz(int argc, char **argv) { initArgs(argc, argv); }
    Argz(const Argz<String> &a) : arg_data{a.arg_data}, arg_info{a.arg_info}, index{a.index}, cindex{a.cindex} {}

    Argz<String> &operator=(const Argz<String> &a) {
        if (this == &a) {
            return *this;
        }
        arg_data = a.arg_data;
        arg_info = a.arg_info;
        index = a.index;
        cindex = a.cindex;
        return *this;
    }

    Argz(Argz<String> &&a) : arg_data{std::move(a.arg_data)}, arg_info{std::move(a.arg_info)}, index{a.index}, cindex{a.cindex} {}

    Argz<String> &operator=(Argz<String> &&a) {
        if (this == &a) {
            return *this;
        }
        arg_data = std::move(a.arg_data);
        arg_info = std::move(a.arg_info);
        index = a.index;
        cindex = a.cindex;
        return *this;
    }

    /**
     * @brief Ingest argc/argv, converting to the target String type.
     * @param argc Argument count.
     * @param argv Argument vector.
     * @return Reference to @c *this for method chaining.
     */
    Argz<String> &initArgs(int argc, char **argv) {
        arg_data.argc = argc;
        arg_data.args.clear();
        if (argc > 1) {
            arg_data.args.reserve(static_cast<size_t>(argc - 1));
        }
        if constexpr (std::is_same<typename String::value_type, char>::value) {
            for (int i = 1; i < argc; ++i) {
                const char *a = (argv != nullptr) ? argv[i] : nullptr;
                arg_data.args.emplace_back(a != nullptr ? a : "");
            }
            reset();
            return *this;
        }
        if constexpr (std::is_same<typename String::value_type, wchar_t>::value) {
            for (int i = 1; i < argc; ++i) {
                const char *a = (argv != nullptr) ? argv[i] : nullptr;
                String data;
                if (a != nullptr) {
                    for (size_t z = 0; a[z] != 0; ++z) {
                        data += static_cast<typename String::value_type>(a[z]);
                    }
                }
                arg_data.args.push_back(data);
            }
            reset();
            return *this;
        }
        reset();
        return *this;
    }

    /** @brief Reset the internal parse cursor to the beginning of argv. */
    void reset() {
        index = 0;
        cindex = 1;
    }

    /**
     * @brief Register a flag-only short option (e.g. @c -v, @c -h).
     * @param c           Short option character code.
     * @param description Help text.
     * @return Reference to @c *this for chaining.
     */
    Argz<String> &addOptionSingle(const int &c, const String &description) {
        Argument<String> a{};
        a.arg_letter = c;
        a.arg_type = ArgType::ARG_SINGLE;
        a.desc = description;
        arg_info[c] = a;
        return *this;
    }

    /**
     * @brief Register a short option that requires a value argument (e.g. @c -o file).
     * @param c           Short option character code.
     * @param description Help text.
     * @return Reference to @c *this for chaining.
     */
    Argz<String> &addOptionSingleValue(const int &c, const String &description) {
        Argument<String> a{};
        a.arg_letter = c;
        a.arg_type = ArgType::ARG_SINGLE_VALUE;
        a.desc = description;
        arg_info[c] = a;
        return *this;
    }

    /**
     * @brief Register a flag-only long option (e.g. @c --verbose).
     * @param code        Unique integer code associated with this option.
     * @param value       Long option name string (without @c --).
     * @param description Help text.
     * @return Reference to @c *this for chaining.
     */
    Argz<String> &addOptionDouble(const int &code, const String &value, const String &description) {
        Argument<String> a{};
        a.arg_letter = code;
        a.arg_type = ArgType::ARG_DOUBLE;
        a.desc = description;
        a.arg_name = value;
        arg_info[code] = a;
        return *this;
    }

    /**
     * @brief Register a long option that requires a value argument (e.g. @c --output file).
     * @param code        Unique integer code.
     * @param value       Long option name string.
     * @param description Help text.
     * @return Reference to @c *this for chaining.
     */
    Argz<String> &addOptionDoubleValue(const int &code, const String &value, const String &description) {
        Argument<String> a{};
        a.arg_letter = code;
        a.arg_type = ArgType::ARG_DOUBLE_VALUE;
        a.desc = description;
        a.arg_name = value;
        arg_info[code] = a;
        return *this;
    }

    /**
     * @brief Look up the integer code registered for a long option name.
     * @param value Long option name (without @c --).
     * @return The registered code, or @c -1 if not found.
     */
    int lookUpCode(const String &value) const {
        for (const auto &i : arg_info) {
            if (i.second.arg_name == value) {
                return i.second.arg_letter;
            }
        }
        return -1;
    }

    /**
     * @brief Advance the cursor and parse the next argument.
     * @param a Output: filled with the matched option details.
     * @return The option code on success; @c '-' for a positional argument; @c -1 when exhausted.
     * @throws ArgException<String> On unrecognised options or missing values.
     */
    int proc(Argument<String> &a) {
        if (index < static_cast<int>(arg_data.args.size())) {
            const String &type{arg_data.args[index]};
            if (type.length() > 2 && type[0] == '-' && type[1] == '-') {
                String name{};
                String inline_value{};
                const auto eq_pos = type.find(static_cast<typename String::value_type>('='));
                const bool has_inline_value = (eq_pos != String::npos);
                for (size_t z = 2; z < type.length(); ++z)
                    if (!has_inline_value || z < eq_pos) {
                        name += type[z];
                    } else if (z > eq_pos) {
                        inline_value += type[z];
                    }
                int code = lookUpCode(name);
                if (code != -1) {
                    auto pos = arg_info.find(code);
                    if (pos != arg_info.end()) {
                        if (pos->second.arg_type == ArgType::ARG_DOUBLE) {
                            if (has_inline_value) {
                                if constexpr (std::is_same<typename String::value_type, char>::value) {
                                    throw ArgException<String>("Invalid switch not found!");
                                }
                                if constexpr (std::is_same<typename String::value_type, wchar_t>::value) {
                                    throw ArgException<String>(L"Invalid switch not found!");
                                }
                            }
                            a = pos->second;
                            a.arg_name = name;
                            index++;
                            return code;
                        } else {
                            a = pos->second;
                            a.arg_name = name;
                            if (has_inline_value) {
                                if (inline_value.empty()) {
                                    if constexpr (std::is_same<typename String::value_type, char>::value) {
                                        throw ArgException<String>("Expected Value");
                                    }
                                    if constexpr (std::is_same<typename String::value_type, wchar_t>::value) {
                                        throw ArgException<String>(L"Expected Value");
                                    }
                                }
                                a.arg_value = inline_value;
                                index++;
                                return code;
                            }
                            if (++index < static_cast<int>(arg_data.args.size())) {
                                const String &s{arg_data.args[index]};
                                if (canUseAsOptionValue(s)) {
                                    a.arg_name = name;
                                    a.arg_value = s;
                                    index++;
                                    return code;
                                }
                            }
                            if constexpr (std::is_same<typename String::value_type, char>::value) {
                                throw ArgException<String>("Expected Value");
                            }
                            if constexpr (std::is_same<typename String::value_type, wchar_t>::value) {
                                throw ArgException<String>(L"Expected Value");
                            }
                        }
                    }
                } else {
                    throwUnknownOption(type);
                }
            } else if (type.length() == 1 && type[0] == '-') {
                if constexpr (std::is_same<typename String::value_type, char>::value) {
                    throw ArgException<String>("Expected Value found -");
                }
                if constexpr (std::is_same<typename String::value_type, wchar_t>::value) {
                    throw ArgException<String>(L"Expected Value found -");
                }
            } else if (type.length() > 1 && (type[0] == '-')) {
                const int c{type[cindex]};
                const auto pos{arg_info.find(c)};
                cindex++;
                if (cindex >= static_cast<int>(type.length())) {
                    cindex = 1;
                    index++;
                }
                String name_val{};
                name_val += static_cast<typename String::value_type>(c);
                if (pos != arg_info.end()) {
                    if (pos->second.arg_type == ArgType::ARG_SINGLE) {
                        a = pos->second;
                        a.arg_name = name_val;
                        return c;
                    } else if (pos->second.arg_type == ArgType::ARG_SINGLE_VALUE) {
                        if (index < static_cast<int>(arg_data.args.size())) {
                            const String &s{arg_data.args[index]};
                            if (!canUseAsOptionValue(s)) {
                                if constexpr (std::is_same<typename String::value_type, char>::value) {
                                    throw ArgException<String>("Expected Value found -");
                                }
                                if constexpr (std::is_same<typename String::value_type, wchar_t>::value) {
                                    throw ArgException<String>(L"Expected Value found -");
                                }
                            }
                            if (s.length() > 0) {
                                a = pos->second;
                                a.arg_value = s;
                                a.arg_name = name_val;
                                index++;
                                return c;
                            }
                        } else {
                            if constexpr (std::is_same<typename String::value_type, char>::value) {
                                throw ArgException<String>("Expected Value");
                            }
                            if constexpr (std::is_same<typename String::value_type, wchar_t>::value) {
                                throw ArgException<String>(L"Expected Value");
                            }
                        }
                    } else {
                        if constexpr (std::is_same<typename String::value_type, char>::value) {
                            throw ArgException<String>("Invalid switch not found!");
                        }
                        if constexpr (std::is_same<typename String::value_type, wchar_t>::value) {
                            throw ArgException<String>(L"Invalid switch not found!");
                        }
                    }
                } else {
                    throwUnknownOption(type);
                }
            } else {
                a = Argument<String>();
                a.arg_name = String{};
                a.arg_type = ArgType::ARG_NONE;
                a.arg_name = a.arg_value = arg_data.args.at(index);
                index++;
                return '-';
            }
        }
        return -1;
    }

    /**
     * @brief Print all registered options to any ostream-like object.
     * @tparam T Output stream type (e.g. @c std::ostream, @c std::wostream).
     * @param cout Destination stream.
     */
    template <typename T>
    void help(T &cout) {
        using char_type = typename std::decay<decltype(*std::declval<T>().rdbuf())>::type::char_type;
        const bool use_color = supportsColor(cout);
        auto write_ansi = [&](const char *seq) {
            if (!use_color) {
                return;
            }
            if constexpr (std::is_same_v<char_type, char>) {
                cout << seq;
            } else if constexpr (std::is_same_v<char_type, wchar_t>) {
                for (const char *p = seq; *p != '\0'; ++p) {
                    cout << static_cast<char_type>(*p);
                }
            }
        };
        auto write_padding = [&](size_t count) {
            for (size_t i = 0; i < count; ++i) {
                cout << static_cast<char_type>(' ');
            }
        };
        struct HelpRow {
            String token;
            String desc;
            bool is_short;
        };
        std::vector<Argument<String>> v;
        std::vector<Argument<String>> v2;
        for (const auto &i : arg_info) {
            if (i.second.arg_type == ArgType::ARG_SINGLE || i.second.arg_type == ArgType::ARG_SINGLE_VALUE)
                v.push_back(i.second);
            else if (i.second.arg_type == ArgType::ARG_DOUBLE || i.second.arg_type == ArgType::ARG_DOUBLE_VALUE)
                v2.push_back(i.second);
        }
        std::ranges::sort(v);
        std::ranges::sort(v2);
        std::vector<HelpRow> rows;
        rows.reserve(v.size() + v2.size());
        for (const auto &a : v) {
            String token;
            token += static_cast<char_type>('-');
            token += static_cast<char_type>(a.arg_letter);
            rows.push_back(HelpRow{std::move(token), a.desc, true});
        }
        for (const auto &a : v2) {
            String token;
            token += static_cast<char_type>('-');
            token += static_cast<char_type>('-');
            token += a.arg_name;
            rows.push_back(HelpRow{std::move(token), a.desc, false});
        }
        size_t token_width = 0;
        for (const auto &row : rows) {
            token_width = std::max(token_width, static_cast<size_t>(row.token.length()));
        }
        const size_t desc_column = token_width + 2;
        for (const auto &row : rows) {
            write_ansi(row.is_short ? "\x1b[1;36m" : "\x1b[1;35m");
            cout << row.token;
            write_ansi("\x1b[0m");
            if (desc_column > row.token.length()) {
                write_padding(desc_column - row.token.length());
            } else {
                write_padding(2);
            }
            write_ansi("\x1b[90m");
            cout << row.desc;
            write_ansi("\x1b[0m");
            cout << '\n';
        }
    }
    /** @return Number of argv entries consumed so far. */
    const size_t count() const { return index; }

  protected:
    ArgumentData<String> arg_data;
    std::unordered_map<int, Argument<String>> arg_info;

  private:
    template <typename Stream>
    static bool supportsColor(const Stream &stream) {
        if constexpr (std::is_same_v<Stream, std::ostream>) {
            if (&stream != &std::cout) {
                return false;
            }
#if defined(_WIN32)
            return _isatty(_fileno(stdout));
#else
            return isatty(fileno(stdout));
#endif
        } else if constexpr (std::is_same_v<Stream, std::wostream>) {
            if (&stream != &std::wcout) {
                return false;
            }
#if defined(_WIN32)
            return _isatty(_fileno(stdout));
#else
            return isatty(fileno(stdout));
#endif
        } else {
            return false;
        }
    }

    [[noreturn]] void throwUnknownOption(const String &token) const {
        if constexpr (std::is_same<typename String::value_type, char>::value) {
            String value = "Error argument: ";
            value += token;
            value += " switch not found";
            throw ArgException<String>(value);
        }
        if constexpr (std::is_same<typename String::value_type, wchar_t>::value) {
            String value = L"Error argument: ";
            value += token;
            value += L" switch not found";
            throw ArgException<String>(value);
        }
    }

    bool isSignedNumericToken(const String &s) const {
        if (s.empty()) {
            return false;
        }

        const auto ch_minus = static_cast<typename String::value_type>('-');
        const auto ch_plus = static_cast<typename String::value_type>('+');
        const auto ch_dot = static_cast<typename String::value_type>('.');
        const auto ch_e = static_cast<typename String::value_type>('e');
        const auto ch_E = static_cast<typename String::value_type>('E');
        const auto ch_0 = static_cast<typename String::value_type>('0');
        const auto ch_9 = static_cast<typename String::value_type>('9');

        size_t i = 0;
        if (s[i] == ch_minus || s[i] == ch_plus) {
            ++i;
        }
        if (i >= s.length()) {
            return false;
        }

        bool has_digit = false;
        bool has_dot = false;
        for (; i < s.length(); ++i) {
            const auto ch = s[i];
            if (ch >= ch_0 && ch <= ch_9) {
                has_digit = true;
                continue;
            }
            if (ch == ch_dot && !has_dot) {
                has_dot = true;
                continue;
            }
            if ((ch == ch_e || ch == ch_E) && has_digit) {
                size_t j = i + 1;
                if (j < s.length() && (s[j] == ch_minus || s[j] == ch_plus)) {
                    ++j;
                }
                if (j >= s.length()) {
                    return false;
                }
                bool exp_digit = false;
                for (; j < s.length(); ++j) {
                    if (s[j] >= ch_0 && s[j] <= ch_9) {
                        exp_digit = true;
                    } else {
                        return false;
                    }
                }
                return exp_digit;
            }
            return false;
        }
        return has_digit;
    }

    bool isRecognizedOptionToken(const String &s) const {
        if (s.length() <= 1 || s[0] != static_cast<typename String::value_type>('-')) {
            return false;
        }

        if (s.length() > 2 && s[1] == static_cast<typename String::value_type>('-')) {
            String name{};
            for (size_t z = 2; z < s.length(); ++z) {
                name += s[z];
            }
            return lookUpCode(name) != -1;
        }

        for (size_t z = 1; z < s.length(); ++z) {
            if (arg_info.find(static_cast<int>(s[z])) == arg_info.end()) {
                return false;
            }
        }
        return true;
    }

    bool canUseAsOptionValue(const String &s) const {
        if (s.empty()) {
            return false;
        }
        if (s[0] != static_cast<typename String::value_type>('-')) {
            return true;
        }
        if (isSignedNumericToken(s)) {
            return true;
        }
        return !isRecognizedOptionToken(s);
    }

    int index = 0, cindex = 1;
};

/**
 * @struct FramebufferDimensions
 * @brief Parsed software framebuffer dimensions.
 */
struct FramebufferDimensions {
    int width = 1280; ///< Software framebuffer width in pixels.
    int height = 720; ///< Software framebuffer height in pixels.
};

/**
 * @struct Arguments
 * @brief Plain data structure returned by proc_args() with all common libmx2 CLI options.
 */
struct Arguments {
    std::string executable_name = "mxvk"; ///< Executable basename derived from argv[0].
    int width = 1280;                     ///< Viewport width in pixels (default: 1280).
    int height = 720;                     ///< Viewport height in pixels (default: 720).
    bool resolutionSpecified = false;     ///< Whether -r/--resolution was provided.
    std::string path = ".";               ///< Asset root; proc_args() defaults it to the executable directory.
    bool fullscreen = false;              ///< Whether fullscreen mode was requested.
    bool fast = false;                    ///< Whether fast mode was requested (@c --fast).
    std::string filename;                 ///< Optional input filename (@c --filename).
    std::string model;                    ///< Optional model filename (@c --model).
    std::string output;                   ///< Optional output filename (@c --output).
    std::string crf;                      ///< Optional CRF value (@c --crf).
    std::string encodePreset;             ///< Optional encoder preset (@c --encode-preset).
    std::string encodeTune;               ///< Optional encoder tune (@c --encode-tune).
    std::string encodeCodec;              ///< Optional encoder codec policy/name (@c --encode-codec).
    bool encodeRealtime = false;          ///< Enable low-latency encoder settings (@c --encode-realtime).
    bool mxwriteBlockWhenFull = false;    ///< Make MXWrite block instead of dropping frames (@c --mxwrite-block).
    bool repeat = false;                  ///< Enable repeat behavior such as playback looping or wrapped textures.
    bool binary = false;                  ///< Use binary glyphs only (@c --binary).
    bool enable_crt = false;              ///< Enable CRT post-processing at startup (@c --enable-crt).
    bool enable_vsync = false;            ///< Enable FIFO present mode / v-sync (@c --enable-vsync).
    bool enable_screenshot = false;       ///< Enable F10 screenshot capture (@c --enable-screenshot).
    bool disable_sound = false;           ///< Disable application background music (@c --disable-sound).
    bool nowarpfix = false;               ///< Disable perspective-correct texture mapping (@c --nowarpfix).
    bool disable_mipmap = false;          ///< Disable mipmap generation and selection (@c --disable-mipmap).
    float mip_bias = 0.0f;                ///< Mipmap level-of-detail bias requested by @c --mip-bias.
    FramebufferDimensions framebuffer;    ///< Software framebuffer size requested by @c --framebuffer.
    double fps = 0.0;                     ///< Optional FPS override (@c --fps); non-positive means unspecified.
    int font_size = 22;                   ///< Matrix rain font size in pixels (@c --font-size).
    std::string font_path;                ///< Optional font file path (@c --font-path).
    std::string color;                    ///< Optional rain RGB tint (@c --color).
    std::string texture;                  ///< Optional texture file path (@c --texture).
    std::string shaderPath;               ///< Optional SPV shader folder path (@c -S / @c --shader-path).
    std::string fragmentPath;             ///< Optional fragment shader SPV path (@c --fragment).
    int camera_index = 0;                 ///< Optional camera index
    int index = 0;                        ///< Optional acidcam filter mode index.
    int shader_index = 0;                 ///< Optional initial shader entry index.
    std::string resource;                 ///< Resource file
    std::string resource_path;            ///< Resource path
};

[[nodiscard]] inline int parse_arg_int(const std::string &text, const std::string &option_name) {
    int value = 0;
    const char *begin = text.data();
    const char *end = begin + text.size();
    const std::from_chars_result result = std::from_chars(begin, end, value);
    if (result.ec != std::errc{} || result.ptr != end) {
        throw ArgException<std::string>("Invalid numeric value for " + option_name + ": " + text);
    }
    return value;
}

[[nodiscard]] inline double parse_arg_double(const std::string &text, const std::string &option_name) {
    double value = 0.0;
    const char *begin = text.data();
    const char *end = begin + text.size();
    const std::from_chars_result result = std::from_chars(begin, end, value);
    if (result.ec != std::errc{} || result.ptr != end || !std::isfinite(value)) {
        throw ArgException<std::string>("Invalid numeric value for " + option_name + ": " + text);
    }
    return value;
}

[[nodiscard]] inline std::string parse_executable_name(const char *argv0) {
    if (argv0 == nullptr || argv0[0] == '\0') {
        return "mxvk";
    }

    std::string name = argv0;
    const std::string::size_type separator = name.find_last_of("/\\");
    if (separator != std::string::npos) {
        name.erase(0, separator + 1);
    }

    constexpr std::string_view exe_extension = ".exe";
    if (name.size() >= exe_extension.size()) {
        const std::string::size_type extension_pos = name.size() - exe_extension.size();
        const bool has_exe_extension = std::ranges::equal(name.begin() + static_cast<std::ptrdiff_t>(extension_pos),
                                                          name.end(),
                                                          exe_extension.begin(),
                                                          exe_extension.end(),
                                                          [](char left, char right) {
                                                              return std::tolower(static_cast<unsigned char>(left)) == std::tolower(static_cast<unsigned char>(right));
                                                          });
        if (has_exe_extension) {
            name.erase(extension_pos);
        }
    }

    return name.empty() ? "mxvk" : name;
}

/**
 * @brief Parse standard libmx2 command-line options from main()'s argv.
 *
 * Registers and processes the following options:
 * | Flag | Long form          | Description                                  |
 * |------|--------------------|----------------------------------------------|
 * | -h   |                    | Print help and exit                          |
 * | -p   | --path             | Asset directory path                         |
 * | -r   | --resolution       | Resolution as WxH (e.g. 1920x1080)           |
 * | -f   | --fullscreen       | Enable fullscreen                            |
 * |      | --fast             | Enable fast mode                             |
 * |      | --filename         | Input filename                               |
 * |      | --model            | Model filename                               |
 * | -o   | --output           | Output filename                              |
 * | -c   | --crf              | Constant Rate Factor                         |
 * |      | --encode-preset    | Encoder preset                               |
 * |      | --encode-tune      | Encoder tune                                 |
 * |      | --encode-codec     | Encoder codec policy or name                 |
 * |      | --encode-realtime  | Enable realtime/low-latency encoding         |
 * |      | --mxwrite-block    | Block MXWrite when its queue is full          |
 * |      | --repeat           | Enable playback or texture repetition        |
 * |      | --binary           | Use binary glyphs only                        |
 * |      | --enable-crt       | Enable CRT post-processing at startup         |
 * |      | --enable-vsync     | Enable FIFO present mode / v-sync             |
 * |      | --enable-screenshot| Enable F10 screenshot capture                 |
 * |      | --disable-sound    | Disable application background music          |
 * |      | --nowarpfix        | Disable perspective-correct texture mapping    |
 * |      | --disable-mipmap   | Disable mipmap generation and selection        |
 * |      | --mip-bias         | Mipmap level-of-detail bias                    |
 * |      | --framebuffer      | Software framebuffer size as WxH               |
 * |      | --fps              | Override capture FPS                          |
 * | -z   | --font-size        | Matrix rain font size                         |
 * | -j   | --font-path        | Matrix rain font file path                    |
 * | -C   | --color            | Matrix rain RGB tint (\#RRGGBB or R,G,B)      |
 * |      | --texture          | Texture file                                 |
 * |      | --textures         | Texture file alias                           |
 * | -S   | --shader-path      | SPV shader folder (must contain index.txt)   |
 * |      | --fragment         | Fragment shader SPV path                     |
 * | -i   | --index            | Acidcam filter mode index                    |
 * |      | --shader-index     | Initial shader entry index                   |
 *
 * @param argc Reference to argc from main().
 * @param argv argv from main().
 * @return Populated Arguments struct; on parse error, returns a default-valued struct.
 */
inline Arguments proc_args(int &argc, char **argv) {
    Arguments args;
    const std::string executable_name = parse_executable_name(argc > 0 ? argv[0] : nullptr);
    Argz<std::string> parser(argc, argv);
    parser.addOptionSingle('h', "Display help message")
        .addOptionSingle('v', "Print version")
        .addOptionSingleValue('p', "assets path")
        .addOptionDoubleValue('P', "path", "assets path")
        .addOptionSingleValue('r', "Resolution WidthxHeight")
        .addOptionDoubleValue('R', "resolution", "Resolution WidthxHeight")
        .addOptionSingle('f', "fullscreen")
        .addOptionDouble('F', "fullscreen", "fullscreen")
        .addOptionDouble(301, "fast", "fast")
        .addOptionDoubleValue(256, "filename", "input filename")
        .addOptionDoubleValue(324, "model", "model filename (.obj, .mxmod, or .mxmod.z)")
        .addOptionSingleValue('o', "output filename")
        .addOptionDoubleValue(304, "output", "output filename")
        .addOptionSingleValue('c', "crf value")
        .addOptionDoubleValue(305, "crf", "crf value")
        .addOptionDoubleValue(306, "encode-preset", "encoder preset")
        .addOptionDoubleValue(307, "encode-tune", "encoder tune")
        .addOptionDoubleValue(308, "encode-codec", "encoder codec policy or name")
        .addOptionDouble(309, "encode-realtime", "encoder realtime mode")
        .addOptionDouble(314, "mxwrite-block", "block MXWrite when its queue is full")
        .addOptionDouble(310, "repeat", "repeat playback or wrapped textures")
        .addOptionDouble(315, "binary", "use binary glyphs only")
        .addOptionDouble(319, "enable-crt", "enable CRT post-processing at startup")
        .addOptionDouble(320, "enable-vsync", "enable FIFO present mode / v-sync")
        .addOptionDouble(322, "enable-screenshot", "enable F10 screenshot capture")
        .addOptionDouble(325, "disable-sound", "disable background music")
        .addOptionDouble(326, "nowarpfix", "3dmath - disable perspective-correct texture mapping")
        .addOptionDoubleValue(327, "framebuffer", "3dmath - software framebuffer size WidthxHeight")
        .addOptionDouble(328, "disable-mipmap", "3dmath - disable mipmap generation and selection")
        .addOptionDoubleValue(329, "mip-bias", "3dmath - mipmap level-of-detail bias (-16 to 16)")
        .addOptionDoubleValue(321, "fps", "capture FPS override")
        .addOptionSingleValue('z', "matrix rain font size")
        .addOptionDoubleValue(316, "font-size", "matrix rain font size")
        .addOptionSingleValue('j', "matrix rain font file path")
        .addOptionDoubleValue(317, "font-path", "matrix rain font file path")
        .addOptionSingleValue('C', "matrix rain RGB tint (#RRGGBB or R,G,B)")
        .addOptionDoubleValue(318, "color", "matrix rain RGB tint (#RRGGBB or R,G,B)")
        .addOptionDoubleValue(302, "resource", "resource file")
        .addOptionDoubleValue(303, "resource_path", "resource data path")
        .addOptionDoubleValue(257, "texture", "texture file (.png or .tex)")
        .addOptionDoubleValue(323, "textures", "texture file (.png or .tex)")
        .addOptionSingleValue('S', "shader SPV folder path (contains index.txt)")
        .addOptionDoubleValue(258, "shader-path", "shader SPV folder path (contains index.txt)")
        .addOptionDoubleValue(313, "fragment", "fragment shader SPV path")
        .addOptionSingleValue('i', "acidcam filter mode index")
        .addOptionDoubleValue(311, "index", "acidcam filter mode index")
        .addOptionDoubleValue(312, "shader-index", "initial shader entry index")
        .addOptionDoubleValue(300, "camera", "camera index");

    Argument<std::string> arg;
    std::string path;
    int value = 0;
    int tw = 1280, th = 720;
    bool fullscreen = false;
    bool fast = false;
    bool resolutionSpecified = false;
    std::string filename;
    std::string model;
    std::string output;
    std::string crf;
    std::string encodePreset;
    std::string encodeTune;
    std::string encodeCodec;
    bool encodeRealtime = false;
    bool mxwriteBlockWhenFull = false;
    bool repeat = false;
    bool binary = false;
    bool enable_crt = false;
    bool enable_vsync = false;
    bool enable_screenshot = false;
    bool disable_sound = false;
    bool nowarpfix = false;
    bool disable_mipmap = false;
    float mip_bias = 0.0f;
    FramebufferDimensions framebuffer;
    double fps = 0.0;
    int font_size = 22;
    std::string font_path;
    std::string color;
    std::string texture;
    std::string shaderPath;
    std::string fragmentPath;
    int camera_index = 0;
    int index = 0;
    int shader_index = 0;
    std::string resource;
    std::string resource_path;
    while ((value = parser.proc(arg)) != -1) {
        switch (value) {
        case 303:
            resource_path = arg.arg_value;
            break;
        case 302:
            resource = arg.arg_value;
            break;
        case 256:
            filename = arg.arg_value;
            break;
        case 324:
            model = arg.arg_value;
            break;
        case 'o':
        case 304:
            output = arg.arg_value;
            break;
        case 'c':
        case 305:
            crf = arg.arg_value;
            break;
        case 306:
            encodePreset = arg.arg_value;
            break;
        case 307:
            encodeTune = arg.arg_value;
            break;
        case 308:
            encodeCodec = arg.arg_value;
            break;
        case 309:
            encodeRealtime = true;
            break;
        case 314:
            mxwriteBlockWhenFull = true;
            break;
        case 310:
            repeat = true;
            break;
        case 315:
            binary = true;
            break;
        case 319:
            enable_crt = true;
            break;
        case 320:
            enable_vsync = true;
            break;
        case 322:
            enable_screenshot = true;
            break;
        case 325:
            disable_sound = true;
            break;
        case 326:
            nowarpfix = true;
            break;
        case 327: {
            const std::string::size_type separator = arg.arg_value.find('x');
            if (separator == std::string::npos) {
                throw ArgException<std::string>("Invalid framebuffer size, expected WidthxHeight: " + arg.arg_value);
            }
            framebuffer.width = parse_arg_int(arg.arg_value.substr(0, separator), "--framebuffer width");
            framebuffer.height = parse_arg_int(arg.arg_value.substr(separator + 1), "--framebuffer height");
            if (framebuffer.width <= 0 || framebuffer.height <= 0) {
                throw ArgException<std::string>("Invalid framebuffer size: " + arg.arg_value);
            }
        } break;
        case 328:
            disable_mipmap = true;
            break;
        case 329: {
            const double parsed_mip_bias = parse_arg_double(arg.arg_value, "--mip-bias");
            if (parsed_mip_bias < -16.0 || parsed_mip_bias > 16.0) {
                throw ArgException<std::string>("Mip bias must be between -16 and 16: " + arg.arg_value);
            }
            mip_bias = static_cast<float>(parsed_mip_bias);
        } break;
        case 321:
            fps = parse_arg_double(arg.arg_value, "--fps");
            if (fps <= 0.0) {
                throw ArgException<std::string>("Invalid numeric value for --fps: " + arg.arg_value);
            }
            break;
        case 'z':
        case 316:
            font_size = parse_arg_int(arg.arg_value, "--font-size");
            if (font_size <= 0) {
                throw ArgException<std::string>("Invalid numeric value for --font-size: " + arg.arg_value);
            }
            break;
        case 'j':
        case 317:
            font_path = arg.arg_value;
            break;
        case 'C':
        case 318:
            color = arg.arg_value;
            break;
        case 257:
        case 323:
            texture = arg.arg_value;
            break;
        case 'S':
        case 258:
            shaderPath = arg.arg_value;
            break;
        case 313:
            fragmentPath = arg.arg_value;
            break;
        case 'h':
        case 'v':
            parser.help(std::cout);
            exit(EXIT_SUCCESS);
            break;
        case 'p':
        case 'P':
            path = arg.arg_value;
            break;
        case 'r':
        case 'R': {
            auto pos = arg.arg_value.find("x");
            if (pos == std::string::npos) {
                throw ArgException<std::string>("Invalid resolution for --resolution, expected WidthxHeight: " + arg.arg_value);
            }
            std::string left, right;
            left = arg.arg_value.substr(0, pos);
            right = arg.arg_value.substr(pos + 1);
            tw = parse_arg_int(left, "--resolution width");
            th = parse_arg_int(right, "--resolution height");
            if (tw <= 0 || th <= 0) {
                throw ArgException<std::string>("Invalid numeric value for --resolution: " + arg.arg_value);
            }
            resolutionSpecified = true;
        } break;
        case 'f':
        case 'F':
            fullscreen = true;
            break;
        case 301:
            fast = true;
            break;
        case 300:
            camera_index = parse_arg_int(arg.arg_value, "--camera");
            break;
        case 'i':
        case 311:
            index = parse_arg_int(arg.arg_value, "--index");
            break;
        case 312:
            shader_index = parse_arg_int(arg.arg_value, "--shader-index");
            break;
        }
    }
    if (path.empty()) {
        path = executable_directory(argc > 0 ? argv[0] : nullptr);
        std::cerr << "mx: No path provided; using executable directory: " << path << '\n';
    } else {
        std::error_code error;
        const std::filesystem::path absolute_path = std::filesystem::absolute(path, error);
        if (!error) {
            path = absolute_path.lexically_normal().string();
        }
    }
    args.executable_name = executable_name;
    mxvk::setDefaultExecutableName(executable_name);
    args.width = tw;
    args.height = th;
    args.resolutionSpecified = resolutionSpecified;
    args.path = path;
    args.fullscreen = fullscreen;
    args.fast = fast;
    args.filename = filename;
    args.model = model;
    args.output = output;
    args.crf = crf;
    args.encodePreset = encodePreset;
    args.encodeTune = encodeTune;
    args.encodeCodec = encodeCodec;
    args.encodeRealtime = encodeRealtime;
    args.mxwriteBlockWhenFull = mxwriteBlockWhenFull;
    args.repeat = repeat;
    args.binary = binary;
    args.enable_crt = enable_crt;
    args.enable_vsync = enable_vsync;
    args.enable_screenshot = enable_screenshot;
    args.disable_sound = disable_sound;
    args.nowarpfix = nowarpfix;
    args.disable_mipmap = disable_mipmap;
    args.mip_bias = mip_bias;
    args.framebuffer = framebuffer;
    mxvk::setDefaultEnableScreenshot(enable_screenshot);
    args.fps = fps;
    args.font_size = font_size;
    args.font_path = font_path;
    args.color = color;
    args.texture = texture;
    args.shaderPath = shaderPath;
    args.fragmentPath = fragmentPath;
    args.camera_index = camera_index;
    args.index = index;
    args.shader_index = shader_index;
    args.resource = resource;
    args.resource_path = resource_path;
    return args;
}

#endif
