#pragma once
#include <iostream>
#include <string>

namespace Amplitron {

//Container for all terminal args
struct CliOptions {
    bool exit_early = false;
    bool is_headless = false;
    std::string preset_path;
    std::string input_device;
    std::string output_device;
};

inline CliOptions handle_cli_args(int argc, char* argv[]) {
    CliOptions options;
    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--help" || arg == "-h") {
            std::cout << "Usage: amplitron [options]\n\n"
                      << "Options:\n"
                      << "  -h, --help      Show this help message and exit\n"
                      << "  -v, --version   Show version information and exit\n"
                      << "\nAudio devices are configured via File -> Settings in the GUI.\n"
                      << "Visit https://github.com/sudip-mondal-2002/Amplitron for docs.\n";
            options.exit_early=true;
            return options;
        }
        else if (arg == "--version" || arg == "-v") {
            std::cout << "Amplitron v1.0\n";
            options.exit_early=true;
            return options;
        }
        else if(arg == "--headless" || arg == "--no-gui"){
            options.is_headless = true;
        }
        //i+1<argc prevents segfault if user does not provide sufficient arguments
        else if(arg == "--preset" && i+1 < argc){
            options.preset_path = argv[++i];//grabs next word(the preset path)
        }
        else if(arg == "--input" && i+1 < argc){
            options.input_device = argv[++i];
        }
        else if(arg == "--output" && i+1 < argc){
            options.output_device = argv[++i];
        }
    }
    //Check: can't run headless without a preset.
    if(options.is_headless && options.preset_path.empty()){
        std::cerr << "Error: --headless/--no-gui mode requires a --preset <path> argument.\n";
        options.exit_early = true;
    }
    return options;
}

} // namespace Amplitron