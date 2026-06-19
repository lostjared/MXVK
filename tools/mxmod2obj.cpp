#include "mxvk/argz.hpp"
#include "mxvk/mxvk_exception.hpp"
#include "mxvk/mxvk_model.hpp"

#include <filesystem>
#include <iostream>
#include <string>

namespace {

    struct ConvertOptions {
        std::string inputPath{};
        std::string textureManifestPath{};
        std::string textureBasePath{};
        std::string outputBase{};
        float scale = 1.0f;
        bool showHelp = false;
    };

    void printUsage(std::ostream &out) {
        out << "usage: mxmod2obj -i <model.mxmod|model.mxmod.z> [-t <texture_manifest.txt>] -o <objectname> [options]\n\n";
        out << "required:\n";
        out << "  -i <path>    Input .mxmod or .mxmod.z model path\n";
        out << "  -o <name>    Output base name or .obj path. Creates <name>.obj and <name>.mtl\n\n";
        out << "optional:\n";
        out << "  -t <path>    Texture manifest path (.txt/.tex or MTL-like text)\n";
        out << "  -b <path>    Base directory used to resolve relative texture paths\n";
        out << "  -s <scale>   Uniform position scale, default 1.0\n";
        out << "  -h           Show this help\n";
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
