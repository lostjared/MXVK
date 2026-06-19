#include "mxvk/argz.hpp"
#include "mxvk/mxvk_exception.hpp"
#include "mxvk/mxvk_model.hpp"

#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <string>
#include <string_view>

#if defined(_WIN32)
#include <io.h>
#else
#include <unistd.h>
#endif

namespace {

    namespace ansi {
        constexpr const char *reset = "\x1b[0m";
        constexpr const char *bold = "\x1b[1m";
        constexpr const char *cyan = "\x1b[1;36m";
        constexpr const char *green = "\x1b[1;32m";
        constexpr const char *yellow = "\x1b[1;33m";
    } // namespace ansi

    void writeAnsi(std::ostream &out, const bool useColor, const char *code) {
        if (useColor) {
            out << code;
        }
    }

    [[nodiscard]] bool supportsColor(std::ostream &out) {
        if (std::getenv("NO_COLOR") != nullptr) {
            return false;
        }

        if (&out == &std::cout) {
#if defined(_WIN32)
            return _isatty(_fileno(stdout)) != 0;
#else
            return isatty(fileno(stdout)) != 0;
#endif
        }

        if (&out == &std::cerr) {
#if defined(_WIN32)
            return _isatty(_fileno(stderr)) != 0;
#else
            return isatty(fileno(stderr)) != 0;
#endif
        }

        return false;
    }

    void writeStyled(std::ostream &out, const bool useColor, const char *code, const std::string_view text) {
        writeAnsi(out, useColor, code);
        out << text;
        writeAnsi(out, useColor, ansi::reset);
    }

    struct ConvertOptions {
        std::string inputPath{};
        std::string textureManifestPath{};
        std::string textureBasePath{};
        std::string outputBase{};
        float scale = 1.0f;
        bool showHelp = false;
    };

    void printUsage(std::ostream &out) {
        const bool useColor = supportsColor(out);

        writeStyled(out, useColor, ansi::bold, "usage:");
        out << ' ';
        writeStyled(out, useColor, ansi::cyan, "mxmod2obj");
        out << ' ';
        writeStyled(out, useColor, ansi::green, "-i");
        out << ' ';
        writeStyled(out, useColor, ansi::yellow, "<model.mxmod|model.mxmod.z>");
        out << " [";
        writeStyled(out, useColor, ansi::green, "-t");
        out << ' ';
        writeStyled(out, useColor, ansi::yellow, "<texture_manifest.txt>");
        out << "] ";
        writeStyled(out, useColor, ansi::green, "-o");
        out << ' ';
        writeStyled(out, useColor, ansi::yellow, "<objectname>");
        out << " [options]\n\n";

        writeStyled(out, useColor, ansi::bold, "required:");
        out << '\n';
        out << "  ";
        writeStyled(out, useColor, ansi::green, "-i");
        out << ' ';
        writeStyled(out, useColor, ansi::yellow, "<path>");
        out << "    Input .mxmod or .mxmod.z model path\n";
        out << "  ";
        writeStyled(out, useColor, ansi::green, "-o");
        out << ' ';
        writeStyled(out, useColor, ansi::yellow, "<name>");
        out << "    Output base name or .obj path. Creates <name>.obj and <name>.mtl\n\n";

        writeStyled(out, useColor, ansi::bold, "optional:");
        out << '\n';
        out << "  ";
        writeStyled(out, useColor, ansi::green, "-t");
        out << ' ';
        writeStyled(out, useColor, ansi::yellow, "<path>");
        out << "    Texture manifest path (.txt/.tex or MTL-like text)\n";
        out << "  ";
        writeStyled(out, useColor, ansi::green, "-b");
        out << ' ';
        writeStyled(out, useColor, ansi::yellow, "<path>");
        out << "    Base directory used to resolve relative texture paths\n";
        out << "  ";
        writeStyled(out, useColor, ansi::green, "-s");
        out << ' ';
        writeStyled(out, useColor, ansi::yellow, "<scale>");
        out << "   Uniform position scale, default 1.0\n";
        out << "  ";
        writeStyled(out, useColor, ansi::green, "-h");
        out << "           Show this help\n";
    }

    [[nodiscard]] std::string makeOBJPath(const std::string &outputBase) {
        std::filesystem::path path(outputBase);
        if (path.extension() == ".obj") {
            return path.string();
        }

        path += ".obj";
        return path.string();
    }

    ConvertOptions parseArgs(int argc, char **argv) {
        ConvertOptions options{};
        Argz<std::string> parser(argc, argv);
        parser.addOptionSingle('h', "Show help")
            .addOptionSingleValue('i', "Input .mxmod or .mxmod.z model")
            .addOptionSingleValue('t', "Texture manifest")
            .addOptionSingleValue('b', "Texture base directory")
            .addOptionSingleValue('o', "Output base name")
            .addOptionSingleValue('s', "Uniform scale");

        Argument<std::string> arg{};
        int code = 0;
        while ((code = parser.proc(arg)) != -1) {
            switch (code) {
            case 'h':
                options.showHelp = true;
                break;
            case 'i':
                options.inputPath = arg.arg_value;
                break;
            case 't':
                options.textureManifestPath = arg.arg_value;
                break;
            case 'b':
                options.textureBasePath = arg.arg_value;
                break;
            case 'o':
                options.outputBase = arg.arg_value;
                break;
            case 's':
                try {
                    size_t consumed = 0;
                    options.scale = std::stof(arg.arg_value, &consumed);
                    if (consumed != arg.arg_value.size()) {
                        throw ArgException<std::string>("Invalid scale value: " + arg.arg_value);
                    }
                } catch (const std::exception &) {
                    throw ArgException<std::string>("Invalid scale value: " + arg.arg_value);
                }
                break;
            default:
                throw ArgException<std::string>("Unsupported argument");
            }
        }

        return options;
    }

} // namespace

int main(int argc, char **argv) {
    try {
        const ConvertOptions options = parseArgs(argc, argv);
        if (options.showHelp) {
            printUsage(std::cout);
            return 0;
        }

        if (options.inputPath.empty() || options.outputBase.empty()) {
            printUsage(std::cerr);
            return 1;
        }

        const std::string objPath = makeOBJPath(options.outputBase);
        mxvk::MXModel model{};
        model.load(options.inputPath, options.textureManifestPath, options.textureBasePath, options.scale);
        model.exportOBJ(objPath);

        std::cout << "wrote " << objPath << " and " << std::filesystem::path(objPath).replace_extension(".mtl").string() << '\n';
        return 0;
    } catch (const ArgException<std::string> &e) {
        std::cerr << "mxmod2obj: argument error: " << e.text() << '\n';
        printUsage(std::cerr);
    } catch (const mxvk::Exception &e) {
        std::cerr << "mxmod2obj: " << e.text() << '\n';
    } catch (const std::exception &e) {
        std::cerr << "mxmod2obj: " << e.what() << '\n';
    }

    return 1;
}
