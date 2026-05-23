#include "common.h"
#include "audio/audio_engine.h"
#include "gui/gui_manager.h"
#include "preset_manager.h"
#include "cli.h"

#include "audio/effects/noise_gate.h"
#include "audio/effects/compressor.h"
#include "audio/effects/overdrive.h"
#include "audio/effects/distortion.h"
#include "audio/effects/equalizer.h"
#include "audio/effects/chorus.h"
#include "audio/effects/delay.h"
#include "audio/effects/reverb.h"
#include "audio/effects/cabinet_sim.h"
#include "audio/effects/amp_simulator.h"

#include <iostream>
#include <csignal>
#include <atomic>
#include <filesystem>
#include <thread>
#include <chrono>

// New includes for Autosave and Recovery
#include "SessionManager.h"
#include "gui/CrashRecoveryUI.h"

#ifdef __EMSCRIPTEN__
#include <emscripten.h>
#endif

#ifdef __ANDROID__
#include <SDL_main.h>
#elif defined(__APPLE__)
#include <TargetConditionals.h>
#if TARGET_OS_IOS
#include <SDL_main.h>
#endif
#endif

static std::atomic<bool> g_running{true};

#ifdef __EMSCRIPTEN__
static Amplitron::GuiManager* g_gui = nullptr;

static void em_main_loop() {
    if (!g_gui || !g_gui->run_frame()) {
        g_running = false;
        emscripten_cancel_main_loop();
    }
}

extern "C" EMSCRIPTEN_KEEPALIVE void on_midi_cc(int channel, int cc, int value) {
    // Validate MIDI ranges before processing
    if (!g_gui) {
        emscripten_log(EM_LOG_WARN, "[MIDI] GUI not initialized, dropping event");
        return;
    }
    
    // Range validation (standard MIDI)
    if (channel < 0 || channel > 15) {
        emscripten_log(EM_LOG_DEBUG, "[MIDI] Invalid channel: %d", channel);
        return;
    }
    if (cc < 0 || cc > 127) {
        emscripten_log(EM_LOG_DEBUG, "[MIDI] Invalid CC number: %d", cc);
        return;
    }
    if (value < 0 || value > 127) {
        emscripten_log(EM_LOG_DEBUG, "[MIDI] Invalid CC value: %d", value);
        return;
    }
    
    // Create MIDI event
    Amplitron::MidiEvent event;
    event.status = static_cast<uint8_t>(0xB0 | (channel & 0x0F));  // CC message
    event.data1 = static_cast<uint8_t>(cc);
    event.data2 = static_cast<uint8_t>(value);
    event.pad = 0;
    
    // Inject into MIDI queue
    g_gui->midi_manager().inject_event(event);
    
    emscripten_log(EM_LOG_DEBUG, "[MIDI] CC %d = %d on channel %d", cc, value, channel);
}

extern "C" EMSCRIPTEN_KEEPALIVE void on_midi_device_connected(const char* device_name) {
    if (!g_gui || !device_name) return;
    
    emscripten_log(EM_LOG_INFO, "[MIDI] Device connected: %s", device_name);
}
#endif

void signal_handler(int /*signal*/) {
    g_running = false;
}

int main(int argc, char* argv[]) {
    //CLI argument parsing
    Amplitron::CliOptions cli_opts = Amplitron::handle_cli_args(argc, argv);
    if(cli_opts.exit_early){
        return 0;
    }

    std::signal(SIGINT, signal_handler);
    std::signal(SIGTERM, signal_handler);

    // Initialize Session Manager
    Amplitron::SessionManager sessionManager("SudipMondal", "Amplitron");

    std::cout << "=== Amplitron v1.0 - Guitar Amp Simulator ===" << std::endl;
    std::cout << "Starting up..." << std::endl;

    // Initialize audio engine
    Amplitron::AudioEngine engine;
    if (!engine.initialize()) {
        std::cerr << "Failed to initialize audio engine!" << std::endl;
        return 1;
    }

    Amplitron::GuiManager gui(engine);

    if(cli_opts.is_headless){
        std::cout << "=== HEADLESS MODE ===" << std::endl;
        std::cout << "Loading preset: " << cli_opts.preset_path << std::endl;
        
        //Safe preset injection
        if (!Amplitron::PresetManager::load_preset(cli_opts.preset_path, engine, nullptr)){
            std::cerr << "Fatal Error: Could not load preset for headless mode." << std::endl;
            engine.shutdown();
            return 1;
        }
        //Hardware routing(i/p)
        if(!cli_opts.input_device.empty()){
            auto devices = engine.get_input_devices();
            std::vector<int> match_indices;

            //Searching and storing all matching devices
            for(size_t i = 0; i < devices.size(); ++i){
                if (devices[i].name.find(cli_opts.input_device) != std::string::npos){
                    match_indices.push_back(i);
                }
            }
            
            if(match_indices.empty()){
                std::cerr << "Warning: Could not find requested input device: '" << cli_opts.input_device << "'" << std::endl;
            } else if (match_indices.size() == 1){
                engine.set_input_device(devices[match_indices[0]].index);
                std::cout << "Input routed to: " << devices[match_indices[0]].name << std::endl;
            } else {
                std::cerr << "Warning: Ambiguous input name '" << cli_opts.input_device << "'. Multiple matches found:" << std::endl;
                for(int idx : match_indices){
                    std::cerr << " -- " << devices[idx].name << std::endl;
                }
                std::cerr << "Auto-selecting the first match: " << devices[match_indices[0]].name << std::endl;
                engine.set_input_device(devices[match_indices[0]].index);
            }
        }
        //hardware routing(o/p)
        if(!cli_opts.output_device.empty()) {
            auto devices = engine.get_output_devices();
            std::vector<int> match_indices;

            for(size_t i = 0;i <devices.size(); ++i){
                if(devices[i].name.find(cli_opts.output_device) != std::string::npos) {
                    match_indices.push_back(i);
                }
            }

            if(match_indices.empty()){
                std::cerr << "Warning: Could not find requested output device: '" << cli_opts.output_device << "'" << std::endl;
            } else if(match_indices.size() == 1){
                engine.set_output_device(devices[match_indices[0]].index);
                std::cout<< "Output routed to: " << devices[match_indices[0]].name << std::endl;
            } else{
                std::cerr << "Warning: Ambiguous output name '" << cli_opts.output_device << "'. Multiple matches found:" <<std::endl;
                for(int idx : match_indices){
                    std::cerr << " -- " << devices[idx].name << std::endl;
                }
                std::cerr << "Auto-selecting the first match: " << devices[match_indices[0]].name << std::endl;
                engine.set_output_device(devices[match_indices[0]].index);
            }
        }

    } else {
    // Create a small, automatically wired, and highly playable circuit
        auto cabinet = std::make_shared<Amplitron::CabinetSim>();
        cabinet->set_enabled(true);

        auto amp = std::make_shared<Amplitron::AmpSimulator>();
        amp->set_enabled(true);

        engine.add_initial_effects({cabinet, amp});

        engine.set_input_gain(0.7f);

        if (sessionManager.hasUnsavedSession()) {
            if (promptRestoreSession()) {
                try {
                    nlohmann::json savedState = sessionManager.loadSession();
                    engine.deserialize(savedState);
                } catch (const nlohmann::json::parse_error& e) {
                    std::cerr << "Autosave file corrupted. Discarding." << std::endl;
                    sessionManager.clearSession();
                }
            } else {
                sessionManager.clearSession();
            }
        }

        if (std::filesystem::exists("presets")) {
            Amplitron::PresetManager::set_presets_dir("presets");
        }

        if (!gui.initialize(1280, 720)) {
            std::cerr << "Failed to initialize GUI!" << std::endl;
            engine.shutdown();
            return 1;
        }
    }
    
    
    if (!engine.start()) {
        std::cerr << "Warning: Could not start audio stream." << std::endl;
    }

    std::cout << "Amplitron is ready. Let's play!" << std::endl;
#ifdef __EMSCRIPTEN__
    g_gui = &gui;
    emscripten_set_main_loop(em_main_loop, 0, 1);
#else
    if (cli_opts.is_headless){
        std::cout << "Audio Engine is running in the background." << std::endl;
        std::cout << "Press Ctrl+C to shut down." << std::endl;
        //headless loop
        while(g_running){
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
    } else{
        //GUI loop
        while(g_running && gui.run_frame()){
            if (sessionManager.shouldSave()){
                sessionManager.saveSession(engine.serialize());
            }
        }
    }
#endif

    // Cleanup
    std::cout << "Shutting down..." << std::endl;
#ifdef __EMSCRIPTEN__
    g_gui = nullptr;
#endif
    if(!cli_opts.is_headless){ 
        gui.shutdown();
    }
    engine.shutdown();

    std::cout << "Goodbye!" << std::endl;
    return 0;
}
