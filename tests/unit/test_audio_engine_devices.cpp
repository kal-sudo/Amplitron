#include <string>
#include <vector>

#include "audio/backend/i_audio_backend.h"
#include "audio/engine/audio_engine.h"
#include "test_framework.h"

using namespace Amplitron;

//Mock hardware simulation 
class MockAudioBackend : public IAudioBackend {
   public:
    bool fail_next_start = false;
    int out_device = 0;
    int in_device = 0;

    bool initialize(IAudioEngine* engine) override { return true; }
    void shutdown() override {}
    
    // Auto-resetting failure mechanism
    bool start() override { 
        if (fail_next_start) {
            fail_next_start = false; 
            return false;
        }
        return true; 
    }
    
    void stop() override {}

    std::vector<AudioDeviceInfo> get_input_devices() const override { return {}; }
    std::vector<AudioDeviceInfo> get_output_devices() const override { return {}; }
    bool set_input_device(int index) override { in_device = index; return true; }
    bool set_output_device(int index) override { out_device = index; return true; }
    std::string get_input_device_name() const override { return "Mock In"; }
    std::string get_output_device_name() const override { return "Mock Out"; }
    int get_sample_rate() const override { return 48000; }
    int get_buffer_size() const override { return 512; }
    int get_input_device() const override { return in_device; }
    int get_output_device() const override { return out_device; }
};

// AudioEngine device selection tests
TEST(audio_engine_output_device_updates_successfully) {
    MockAudioBackend mock_backend; 
    AudioEngine engine;            
    
    engine.replace_backend_for_test(&mock_backend);
    ASSERT_TRUE(engine.initialize());
    ASSERT_TRUE(engine.start()); 

    // Simulate user clicking device index 1
    bool success = engine.set_output_device(1);

    ASSERT_TRUE(success);
    ASSERT_EQ(engine.get_output_device(), 1);
    ASSERT_EQ(engine.get_last_error(), "");

    engine.clear_backend_for_test();
}

TEST(audio_engine_output_device_reverts_on_start_failure) {
    MockAudioBackend mock_backend; 
    AudioEngine engine;            
    
    engine.replace_backend_for_test(&mock_backend);
    ASSERT_TRUE(engine.initialize());
    ASSERT_TRUE(engine.start()); 

    mock_backend.fail_next_start = true;
    
    // Simulate user clicking device index 2
    bool success = engine.set_output_device(2);

    // Assert that the engine caught the failure and reverted to device 0
    ASSERT_TRUE(success);
    ASSERT_EQ(engine.get_output_device(), 0);
    
    std::string error_msg = engine.get_last_error();
    ASSERT_TRUE(error_msg.find("Failed to start") != std::string::npos);

    engine.clear_backend_for_test();
}
