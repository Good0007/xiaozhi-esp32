#ifndef _APPLICATION_H_
#define _APPLICATION_H_

#include <freertos/FreeRTOS.h>
#include <freertos/event_groups.h>
#include <freertos/task.h>
#include <esp_timer.h>

#include <string>
#include <mutex>
#include <list>
#include <vector>
#include <condition_variable>
#include <memory>

#include <opus_encoder.h>
#include <opus_decoder.h>
#include <opus_resampler.h>
#include "simple_resampler.h"

#include "protocol.h"
#include "ota.h"
#include "background_task.h"
#include "audio_processor.h"

#if CONFIG_USE_WAKE_WORD_DETECT
#include "wake_word_detect.h"
#endif

#define SCHEDULE_EVENT (1 << 0)
#define AUDIO_INPUT_READY_EVENT (1 << 1)
#define AUDIO_OUTPUT_READY_EVENT (1 << 2)
#define CHECK_NEW_VERSION_DONE_EVENT (1 << 3)

enum class PlayingType {
    None,
    Sound,
    Mp3Stream,
    LocalAudio
};
enum class PlayMode {
    Sequence = 0,
    Random = 1
};
enum DeviceState {
    kDeviceStateUnknown,
    kDeviceStateStarting,
    kDeviceStateWifiConfiguring,
    kDeviceStateIdle,
    kDeviceStateConnecting,
    kDeviceStateListening,
    kDeviceStateSpeaking,
    kDeviceStateUpgrading,
    kDeviceStateActivating,
    kDeviceStateFatalError
};

//创建playInfo对象，保存播放信息（名称、url）
struct PlayInfo {
    //id
    int id = 0;
    std::string name;
    std::string url = "";
    //播放类型
    PlayingType type = PlayingType::None;
    uint8_t tag = 0;  // 0: 音乐 1 : 新闻 2: 交通 3: 财经
};


#define OPUS_FRAME_DURATION_MS 60

class Application {
public:
    static Application& GetInstance() {
        static Application instance;
        return instance;
    }
    // 删除拷贝构造函数和赋值运算符
    Application(const Application&) = delete;
    Application& operator=(const Application&) = delete;

    void Start();
    DeviceState GetDeviceState() const { return device_state_; }
    bool IsVoiceDetected() const { return voice_detected_; }
    void Schedule(std::function<void()> callback);
    void SetDeviceState(DeviceState state);
    void Alert(const char* status, const char* message, const char* emotion = "", const std::string_view& sound = "");
    void DismissAlert();
    void AbortSpeaking(AbortReason reason);
    void ToggleChatState();
    void StartListening();
    void StopListening();
    void UpdateIotStates();
    void Reboot();
    void WakeWordInvoke(const std::string& wake_word);
    void PlaySound(const std::string_view& sound);
    bool CanEnterSleepMode();
    //传入类型和地址
    void ChangePlaying(PlayingType type, std::vector<PlayInfo> &play_list);
    //开始播放
    void StartPlaying(PlayMode mode = PlayMode::Sequence,PlayingType play_type = PlayingType::Mp3Stream, int start_index = 0);
    //加入到播放列表
    void AddToPlayList(PlayInfo &play_info);
    //清空播放列表
    void ClearPlayList();
    //停止播放
    void StopPlaying();
    //获取播放列表
    std::vector<PlayInfo> GetPlayList() const { return play_list_; }
    void ProcessDecodedPcmData(std::vector<int16_t>& pcm_data);
    void PlayLocalAudio(PlayInfo &play_info);
    void PlayOnlineList();
    void PlayStream(PlayInfo &play_info);
    PlayingType GetPlayingType() const;

private:
    Application();
    ~Application();

#if CONFIG_USE_WAKE_WORD_DETECT
    WakeWordDetect wake_word_detect_;
#endif
    std::unique_ptr<AudioProcessor> audio_processor_;
    Ota ota_;
    std::mutex mutex_;
    std::list<std::function<void()>> main_tasks_;
    std::unique_ptr<Protocol> protocol_;
    EventGroupHandle_t event_group_ = nullptr;
    esp_timer_handle_t clock_timer_handle_ = nullptr;
    volatile DeviceState device_state_ = kDeviceStateUnknown;
    ListeningMode listening_mode_ = kListeningModeAutoStop;
#if CONFIG_USE_DEVICE_AEC || CONFIG_USE_SERVER_AEC
    bool realtime_chat_enabled_ = true;
#else
    bool realtime_chat_enabled_ = false;
#endif
    bool aborted_ = false;
    bool voice_detected_ = false;
    bool busy_decoding_audio_ = false;
    int clock_ticks_ = 0;
    TaskHandle_t check_new_version_task_handle_ = nullptr;

    // Audio encode / decode
    TaskHandle_t audio_loop_task_handle_ = nullptr;
    TaskHandle_t play_loop_task_handle_ = nullptr;

    BackgroundTask* background_task_ = nullptr;
    std::chrono::steady_clock::time_point last_output_time_;
    std::list<AudioStreamPacket> audio_decode_queue_;
    std::condition_variable audio_decode_cv_;

    // 新增：用于维护音频包的timestamp队列
    std::list<uint32_t> timestamp_queue_;
    std::mutex timestamp_mutex_;
    std::atomic<uint32_t> last_output_timestamp_ = 0;

    std::unique_ptr<OpusEncoderWrapper> opus_encoder_;
    std::unique_ptr<OpusDecoderWrapper> opus_decoder_;

    OpusResampler input_resampler_;
    OpusResampler reference_resampler_;
    OpusResampler output_resampler_;
    SimpleResampler* resampler_48_ = nullptr;

    void MainEventLoop();
    void OnAudioInput();
    void OnAudioOutput();
    void ReadAudio(std::vector<int16_t>& data, int sample_rate, int samples);
    void ResetDecoder();
    void SetDecodeSampleRate(int sample_rate, int frame_duration);
    void CheckNewVersion();
    void ShowActivationCode();
    void OnClockTimer();
    void SetListeningMode(ListeningMode mode);
    void AudioLoop();
    void PlayingLoop();

    //当前播放索引
    int current_play_index_ = -1;
    int start_play_index_ = 0;
    PlayMode play_mode_ = PlayMode::Sequence;
    //播放列表
    std::vector<PlayInfo> play_list_;
};

#endif // _APPLICATION_H_
