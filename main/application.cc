#include "application.h"
#include "board.h"
#include "display.h"
#include "system_info.h"
#include "ml307_ssl_transport.h"
#include "audio_codec.h"
#include "mqtt_protocol.h"
#include "websocket_protocol.h"
#include "font_awesome_symbols.h"
#include "iot/thing_manager.h"
#include "assets/lang_config.h"

#if CONFIG_USE_AUDIO_PROCESSOR
#include "afe_audio_processor.h"
#else
#include "dummy_audio_processor.h"
#endif

#include <cstring>
#include <esp_log.h>
#include <cJSON.h>
#include <driver/gpio.h>
#include <arpa/inet.h>

#include "audio_codecs/mp3_decoder_wrapper.h"
#include "http/http_net_stream.h"
#include "http/raing_buffer.h"

#define TAG "Application"

PlayingType playing_type_ = PlayingType::None;

static const char* const STATE_STRINGS[] = {
    "unknown",
    "starting",
    "configuring",
    "idle",
    "connecting",
    "listening",
    "speaking",
    "upgrading",
    "activating",
    "fatal_error",
    "invalid_state"
};

Application::Application() {
    event_group_ = xEventGroupCreate();
    background_task_ = new BackgroundTask(4096 * 8);

#if CONFIG_USE_AUDIO_PROCESSOR
    audio_processor_ = std::make_unique<AfeAudioProcessor>();
#else
    audio_processor_ = std::make_unique<DummyAudioProcessor>();
#endif

    esp_timer_create_args_t clock_timer_args = {
        .callback = [](void* arg) {
            Application* app = (Application*)arg;
            app->OnClockTimer();
        },
        .arg = this,
        .dispatch_method = ESP_TIMER_TASK,
        .name = "clock_timer",
        .skip_unhandled_events = true
    };
    esp_timer_create(&clock_timer_args, &clock_timer_handle_);
    esp_timer_start_periodic(clock_timer_handle_, 1000000);
}

Application::~Application() {
    if (clock_timer_handle_ != nullptr) {
        esp_timer_stop(clock_timer_handle_);
        esp_timer_delete(clock_timer_handle_);
    }
    if (background_task_ != nullptr) {
        delete background_task_;
    }
    vEventGroupDelete(event_group_);
}

// application.cc
PlayingType Application::GetPlayingType() const {
    return playing_type_;
}

void Application::CheckNewVersion() {
    const int MAX_RETRY = 10;
    int retry_count = 0;
    int retry_delay = 10; // 初始重试延迟为10秒

    while (true) {
        SetDeviceState(kDeviceStateActivating);
        auto display = Board::GetInstance().GetDisplay();
        display->SetStatus(Lang::Strings::CHECKING_NEW_VERSION);

        if (!ota_.CheckVersion()) {
            retry_count++;
            if (retry_count >= MAX_RETRY) {
                ESP_LOGE(TAG, "Too many retries, exit version check");
                return;
            }

            char buffer[128];
            snprintf(buffer, sizeof(buffer), Lang::Strings::CHECK_NEW_VERSION_FAILED, retry_delay, ota_.GetCheckVersionUrl().c_str());
            Alert(Lang::Strings::ERROR, buffer, "sad", Lang::Sounds::P3_EXCLAMATION);

            ESP_LOGW(TAG, "Check new version failed, retry in %d seconds (%d/%d)", retry_delay, retry_count, MAX_RETRY);
            for (int i = 0; i < retry_delay; i++) {
                vTaskDelay(pdMS_TO_TICKS(1000));
                if (device_state_ == kDeviceStateIdle) {
                    break;
                }
            }
            retry_delay *= 2; // 每次重试后延迟时间翻倍
            continue;
        }
        retry_count = 0;
        retry_delay = 10; // 重置重试延迟时间

        if (ota_.HasNewVersion()) {
            Alert(Lang::Strings::OTA_UPGRADE, Lang::Strings::UPGRADING, "happy", Lang::Sounds::P3_UPGRADE);

            vTaskDelay(pdMS_TO_TICKS(3000));

            SetDeviceState(kDeviceStateUpgrading);
            
            display->SetIcon(FONT_AWESOME_DOWNLOAD);
            std::string message = std::string(Lang::Strings::NEW_VERSION) + ota_.GetFirmwareVersion();
            display->SetChatMessage("system", message.c_str());

            auto& board = Board::GetInstance();
            board.SetPowerSaveMode(false);
#if CONFIG_USE_WAKE_WORD_DETECT
            wake_word_detect_.StopDetection();
#endif
            // 预先关闭音频输出，避免升级过程有音频操作
            auto codec = board.GetAudioCodec();
            codec->EnableInput(false);
            codec->EnableOutput(false);
            {
                std::lock_guard<std::mutex> lock(mutex_);
                audio_decode_queue_.clear();
            }
            background_task_->WaitForCompletion();
            delete background_task_;
            background_task_ = nullptr;
            vTaskDelay(pdMS_TO_TICKS(1000));

            ota_.StartUpgrade([display](int progress, size_t speed) {
                char buffer[64];
                snprintf(buffer, sizeof(buffer), "%d%% %zuKB/s", progress, speed / 1024);
                display->SetChatMessage("system", buffer);
            });

            // If upgrade success, the device will reboot and never reach here
            display->SetStatus(Lang::Strings::UPGRADE_FAILED);
            ESP_LOGI(TAG, "Firmware upgrade failed...");
            vTaskDelay(pdMS_TO_TICKS(3000));
            Reboot();
            return;
        }

        // No new version, mark the current version as valid
        ota_.MarkCurrentVersionValid();
        if (!ota_.HasActivationCode() && !ota_.HasActivationChallenge()) {
            xEventGroupSetBits(event_group_, CHECK_NEW_VERSION_DONE_EVENT);
            // Exit the loop if done checking new version
            break;
        }

        display->SetStatus(Lang::Strings::ACTIVATION);
        // Activation code is shown to the user and waiting for the user to input
        if (ota_.HasActivationCode()) {
            ShowActivationCode();
        }

        // This will block the loop until the activation is done or timeout
        for (int i = 0; i < 10; ++i) {
            ESP_LOGI(TAG, "Activating... %d/%d", i + 1, 10);
            esp_err_t err = ota_.Activate();
            if (err == ESP_OK) {
                xEventGroupSetBits(event_group_, CHECK_NEW_VERSION_DONE_EVENT);
                break;
            } else if (err == ESP_ERR_TIMEOUT) {
                vTaskDelay(pdMS_TO_TICKS(3000));
            } else {
                vTaskDelay(pdMS_TO_TICKS(10000));
            }
            if (device_state_ == kDeviceStateIdle) {
                break;
            }
        }
    }
}

void Application::ShowActivationCode() {
    auto& message = ota_.GetActivationMessage();
    auto& code = ota_.GetActivationCode();

    struct digit_sound {
        char digit;
        const std::string_view& sound;
    };
    static const std::array<digit_sound, 10> digit_sounds{{
        digit_sound{'0', Lang::Sounds::P3_0},
        digit_sound{'1', Lang::Sounds::P3_1}, 
        digit_sound{'2', Lang::Sounds::P3_2},
        digit_sound{'3', Lang::Sounds::P3_3},
        digit_sound{'4', Lang::Sounds::P3_4},
        digit_sound{'5', Lang::Sounds::P3_5},
        digit_sound{'6', Lang::Sounds::P3_6},
        digit_sound{'7', Lang::Sounds::P3_7},
        digit_sound{'8', Lang::Sounds::P3_8},
        digit_sound{'9', Lang::Sounds::P3_9}
    }};

    // This sentence uses 9KB of SRAM, so we need to wait for it to finish
    Alert(Lang::Strings::ACTIVATION, message.c_str(), "happy", Lang::Sounds::P3_ACTIVATION);

    for (const auto& digit : code) {
        auto it = std::find_if(digit_sounds.begin(), digit_sounds.end(),
            [digit](const digit_sound& ds) { return ds.digit == digit; });
        if (it != digit_sounds.end()) {
            PlaySound(it->sound);
        }
    }
}

void Application::Alert(const char* status, const char* message, const char* emotion, const std::string_view& sound) {
    ESP_LOGW(TAG, "Alert %s: %s [%s]", status, message, emotion);
    auto display = Board::GetInstance().GetDisplay();
    display->SetStatus(status);
    display->SetEmotion(emotion);
    display->SetChatMessage("system", message);
    if (!sound.empty()) {
        ResetDecoder();
        PlaySound(sound);
    }
}

void Application::DismissAlert() {
    if (device_state_ == kDeviceStateIdle) {
        auto display = Board::GetInstance().GetDisplay();
        display->SetStatus(Lang::Strings::STANDBY);
        display->SetEmotion("neutral");
        display->SetChatMessage("system", "");
    }
}

void Application::PlaySound(const std::string_view& sound) {
    std::unique_lock<std::mutex> lock(mutex_);
    if (playing_type_ != PlayingType::None) {
        ESP_LOGW(TAG, "Another audio is playing, ignore PlaySound");
        return;
    }
    playing_type_ = PlayingType::Sound;
    lock.unlock();
    // Wait for the previous sound to finish
    {
        std::unique_lock<std::mutex> lock(mutex_);
        audio_decode_cv_.wait(lock, [this]() {
            return audio_decode_queue_.empty();
        });
    }
    background_task_->WaitForCompletion();

    // The assets are encoded at 16000Hz, 60ms frame duration
    SetDecodeSampleRate(16000, 60);
    const char* data = sound.data();
    size_t size = sound.size();
    for (const char* p = data; p < data + size; ) {
        auto p3 = (BinaryProtocol3*)p;
        p += sizeof(BinaryProtocol3);

        auto payload_size = ntohs(p3->payload_size);
        AudioStreamPacket packet;
        packet.payload.resize(payload_size);
        memcpy(packet.payload.data(), p3->payload, payload_size);
        p += payload_size;

        std::lock_guard<std::mutex> lock(mutex_);
        audio_decode_queue_.emplace_back(std::move(packet));
    }
    lock.lock();
    playing_type_ = PlayingType::None;
}

void Application::StopPlaying() {
    ESP_LOGI(TAG, "Stop playing");
    if (playing_type_ != PlayingType::None) {
        std::unique_lock<std::mutex> lock(mutex_);
        playing_type_ = PlayingType::None;
        vTaskDelay(pdMS_TO_TICKS(100));
    }
}

void Application::changePlaying(PlayingType type, PlayInfo &play_info) {
    {
        std::unique_lock<std::mutex> lock(mutex_);
        playing_type_ = PlayingType::None; // 通知播放循环退出
    }
    background_task_->WaitForCompletion();
    vTaskDelay(pdMS_TO_TICKS(500));  
    ESP_LOGI(TAG, "Change playing type: %d, url: %s", (int)type, play_info.url.c_str());
    {
        std::unique_lock<std::mutex> lock(mutex_);
        audio_decode_cv_.wait(lock, [this] {
            return audio_decode_queue_.empty();
        });
        SetDeviceState(kDeviceStateIdle);
        this->current_playing_url_ = play_info.url;
        //转成list
        this->play_list_.clear();
        this->play_list_.push_back(play_info);
        playing_type_ = type;
    }

}

bool is_mp3_frame_header(const uint8_t* data) {
    // MPEG1 Layer III: 0xFF Ex (E0~EF)
    return data[0] == 0xFF && (data[1] & 0xE0) == 0xE0;
}

void Application::PlayOnlineList() {
    //循环播放列表 循环 play_list_ 调用 playStream播放全部歌曲
    for (const auto& play_info : play_list_) {
        if (play_info.url.empty()) {
            ESP_LOGW(TAG, "Empty URL in play list");
            continue;
        }
        if (playing_type_ != PlayingType::Mp3Stream) {
            ESP_LOGW(TAG, "Not in MP3 stream mode, aborting playback");
            break;
        }
        PlayStream(const_cast<PlayInfo&>(play_info));
    }
}

void Application::PlayStream(PlayInfo &play_info) {
    std::string url = play_info.url;
    ESP_LOGI(TAG, "playStream: %s", url.c_str());
    auto display = Board::GetInstance().GetDisplay();
    display->SetStatus("正在播放");
    display->SetChatMessage("system", ("当前播放: " + play_info.name).c_str());

    // 创建缓冲区和解码器
    std::unique_ptr<RingBuffer> ring_buffer = std::make_unique<RingBuffer>(16 * 1024);
    std::unique_ptr<Mp3StreamDecoderWrapper> mp3_decoder = std::make_unique<Mp3StreamDecoderWrapper>();
    auto stream = OpenNetworkStream(url);
    if (!stream) {
        ESP_LOGE(TAG, "Failed to open mp3 stream");
        display->SetChatMessage("system", "播放失败，请稍后再试");
        display->SetEmotion("sad");
        return;
    }
    //休眠一段时间，等待缓冲
    vTaskDelay(pdMS_TO_TICKS(1000));
    const size_t READ_BUFFER_SIZE = 4096;
    std::vector<uint8_t> temp_buffer(READ_BUFFER_SIZE);
    std::vector<int16_t> pcm_buffer;
    pcm_buffer.reserve(4096);

    int consecutive_errors = 0;
    try {
                //设置首次缓冲标志位
        bool first_buffer = true;
        while (playing_type_== PlayingType::Mp3Stream) {
            // 读取网络数据
            int read_bytes = stream->Read(temp_buffer.data(), temp_buffer.size());
            if (read_bytes > 0) {
                ring_buffer->Write(temp_buffer.data(), read_bytes);
                consecutive_errors = 0;
                //ESP_LOGI(TAG, "Read %d bytes from stream", read_bytes);
            } else if (read_bytes == 0) {
                consecutive_errors++;
                vTaskDelay(pdMS_TO_TICKS(100));
                continue;
            } else {
                ESP_LOGI(TAG, "Stream ended");
                break;
            }
            //首次读取，多缓冲一些数据,8k
            if (first_buffer) {
                //跳过
                if (ring_buffer->Size() < 8 * 1024) {
                    //跳过循环
                    continue;
                } else {
                    ESP_LOGI(TAG, "First buffer, read %d bytes", read_bytes);
                    uint8_t id3_header[10];
                    ring_buffer->Peek(id3_header, 10);
                    if (memcmp(id3_header, "ID3", 3) == 0) {
                        int tag_size = ((id3_header[6] & 0x7F) << 21) |
                                    ((id3_header[7] & 0x7F) << 14) |
                                    ((id3_header[8] & 0x7F) << 7) |
                                    (id3_header[9] & 0x7F);
                        ring_buffer->Pop(10 + tag_size);
                        ESP_LOGI(TAG, "Skip ID3 tag, size: %d", tag_size);
                    }
                    first_buffer = false;
                }
            }

            // 解码并播放
            int process_iterations = 0;
            while (ring_buffer->Size() >= 1152) {
                if (playing_type_== PlayingType::None) {
                    break;
                }
                process_iterations++;
                pcm_buffer.clear();
                size_t peek_len = std::min<size_t>(ring_buffer->Size(), 2048);
                temp_buffer.resize(peek_len);
                size_t peeked = ring_buffer->Peek(temp_buffer.data(), temp_buffer.size());
                if (peeked == 0) break;

                bool decoded = false;
                try {
                    decoded = mp3_decoder->DecodeFrame(temp_buffer.data(), peeked, pcm_buffer);
                } catch (const std::exception& e) {
                    ESP_LOGE(TAG, "MP3 decoding exception: %s", e.what());
                    ring_buffer->Pop(std::min<size_t>(512, ring_buffer->Size()));
                    continue;
                }
                size_t consumed = mp3_decoder->LastFrameBytes();
                if (consumed > 0) {
                    ring_buffer->Pop(consumed);
                } else {
                    ESP_LOGW(TAG, "No MP3 frame found, skipping %zu bytes", peek_len);
                    if (ring_buffer->Size() < 128) {
                        ring_buffer->Pop(ring_buffer->Size());
                        break;
                    }
                    size_t skip_bytes = std::min<size_t>(512, ring_buffer->Size());
                    ring_buffer->Pop(skip_bytes);
                }
                if (decoded && !pcm_buffer.empty()) {
                    //ESP_LOGI(TAG, "Decoded %zu PCM samples", pcm_buffer.size());
                    // 处理PCM数据
                    ProcessDecodedPcmData(pcm_buffer);
                }
            }
        }
    } catch (const std::exception& e) {
        ESP_LOGE(TAG, "Exception in PlayOnlineStream: %s", e.what());
    }

    try {
        if (stream) {
            stream->Close();
        }
    } catch (const std::exception& e) {
        ESP_LOGE(TAG, "Error closing stream: %s", e.what());
    }
    ESP_LOGI(TAG, "Online stream playback ended");
}

static constexpr const char* PLAYING = "正在播放";
void Application::PlayMp3Stream() {
    if (current_playing_url_.empty() || playing_type_!= PlayingType::Mp3Stream) {
        ESP_LOGW(TAG, "Another audio is playing, ignore PlayMp3Stream");
        return;
    }
    ESP_LOGI(TAG, "PlayMp3Stream: %s", current_playing_url_.c_str());
    //屏幕上输出当前播放的节目
    auto display = Board::GetInstance().GetDisplay();
    display->SetStatus(PLAYING);
    display->SetChatMessage("system", ("当前播放:" + play_list_[0].name).c_str());

    // 将大型缓冲区从栈移到堆上
    std::unique_ptr<RingBuffer> ring_buffer = std::make_unique<RingBuffer>(16 * 1024);
    std::unique_ptr<Mp3StreamDecoderWrapper> mp3_decoder = std::make_unique<Mp3StreamDecoderWrapper>();
    // 打开网络流
    auto stream = OpenNetworkStream(current_playing_url_);
    if (!stream) {
        ESP_LOGE(TAG, "Failed to open mp3 stream");
        playing_type_ = PlayingType::None;
        //播放失败
        display->SetChatMessage("system", "播放失败，请稍后再试");
        display->SetEmotion("sad");
        return;
    }
    //休眠一段时间，等待缓冲
    vTaskDelay(pdMS_TO_TICKS(1000));
    
    // 减小临时缓冲区大小，预分配合适大小以减少重新分配
    const size_t READ_BUFFER_SIZE = 4094;
    std::vector<uint8_t> temp_buffer(READ_BUFFER_SIZE);
    std::vector<int16_t> pcm_buffer;
    pcm_buffer.reserve(4096);  // 预分配一个合理大小，减少重新分配
    
    int consecutive_errors = 0;
    try {

        while (playing_type_== PlayingType::Mp3Stream) {
            // 使用非阻塞读取并加入超时检查
            int read_bytes = stream->Read(temp_buffer.data(), temp_buffer.size());  
            // 读取错误或EOF处理
            if (read_bytes <= 0) {
                consecutive_errors++;
                ESP_LOGI(TAG, "Read error or EOF, read_bytes: %d, consecutive_errors: %d", read_bytes, consecutive_errors);
                //休眠100ms
                vTaskDelay(pdMS_TO_TICKS(100));
                continue;
            }
            // 成功读取，重置错误计数器
            consecutive_errors = 0;
            size_t written = ring_buffer->Write(temp_buffer.data(), read_bytes);
            if (written < (size_t)read_bytes) {
                ESP_LOGW(TAG, "RingBuffer full, dropped %d bytes", (int)(read_bytes - written));
            }
            // 分批处理缓冲区数据
            int process_iterations = 0;
            const int MAX_PROCESS_ITERATIONS = 30;  // 防止无限循环
            
            while (ring_buffer->Size() > 512 && playing_type_== PlayingType::Mp3Stream && process_iterations < MAX_PROCESS_ITERATIONS) {
                process_iterations++;
                
                pcm_buffer.clear(); // 只清空不释放内存
                size_t peek_len = std::min<size_t>(ring_buffer->Size(), 2048);
                temp_buffer.resize(peek_len);
                
                size_t peeked = ring_buffer->Peek(temp_buffer.data(), temp_buffer.size());
                if (peeked == 0) break;
                
                bool decoded = false;
                try {
                    decoded = mp3_decoder->DecodeFrame(temp_buffer.data(), peeked, pcm_buffer);
                    // 如果解码成功但没有数据，可能是头帧或其他元数据
                    if (decoded && pcm_buffer.empty()) {
                        ESP_LOGD(TAG, "MP3 decode succeeded but no PCM data (metadata frame)");
                    }
                    // 只记录有意义的解码结果，减少日志噪音
                    if (decoded && !pcm_buffer.empty()) {
                        //SP_LOGI(TAG, "MP3 decode succeeded, length: %zu, decoded samples: %zu",  peeked, pcm_buffer.size());
                    } else if (!decoded) {
                        // 只在调试级别记录失败，避免日志过多
                        ESP_LOGD(TAG, "MP3 decode failed, length: %zu", peeked);
                    }
                } catch (const std::exception& e) {
                    ESP_LOGE(TAG, "MP3 decoding exception: %s", e.what());
                    ring_buffer->Pop(std::min<size_t>(64, ring_buffer->Size()));
                    continue;
                }
                size_t consumed = mp3_decoder->LastFrameBytes();
                if (consumed > 0) {
                    ring_buffer->Pop(consumed);
                } else {
                     // 处理流结束 - 如果缓冲区很小且无法解码，可能是流的末尾
                    if (ring_buffer->Size() < 128) {
                        ESP_LOGI(TAG, "Reached end of stream with %zu bytes remaining, clearing buffer", ring_buffer->Size());
                        ring_buffer->Pop(ring_buffer->Size()); // 清空剩余数据
                        break;
                    }
                    // 正常情况下，跳过一小部分数据
                    size_t skip_bytes = std::min<size_t>(64, ring_buffer->Size());
                    ring_buffer->Pop(skip_bytes);
                    ESP_LOGW(TAG, "No frame detected, skipping %zu bytes", skip_bytes);
                }
                // 统计连续解码失败的次数
                static int consecutive_decode_failures = 0;
                if (!decoded || pcm_buffer.empty()) {
                    consecutive_decode_failures++;
                    // 每处理10个失败帧，让出一下CPU
                    if (consecutive_decode_failures % 10 == 0) {
                        vTaskDelay(pdMS_TO_TICKS(1));
                    }
                } else {
                    // 解码成功，重置失败计数器
                    consecutive_decode_failures = 0;
                }
                // 只有当真正解码出PCM数据时才处理
                if (decoded && !pcm_buffer.empty()) {
                    ProcessDecodedPcmData(pcm_buffer);
                    //输出队列长度
                    //ESP_LOGI(TAG, "Audio decode queue size: %zu", audio_decode_queue_.size());
                }
            }
            // 流结束处理 - 如果解码完成后环形缓冲区中还有少量数据
            if (ring_buffer->Size() > 0 && ring_buffer->Size() < 64) {
                ESP_LOGI(TAG, "Stream ended with %zu bytes remaining, clearing buffer", ring_buffer->Size());
                ring_buffer->Pop(ring_buffer->Size());
            }
            if (ring_buffer->Size() > (ring_buffer->Capacity() * 0.8)) {
                vTaskDelay(pdMS_TO_TICKS(10)); // 当缓冲区接近满时短暂暂停读取
            }
            //vTaskDelay(pdMS_TO_TICKS(10)); 
        }
    } catch (const std::exception& e) {
        ESP_LOGE(TAG, "Exception in PlayMp3Stream: %s", e.what());
    }
    
    // 确保资源正确关闭
    try {
        if (stream) {
            stream->Close();  // 如果有显式关闭方法则调用
        }
    } catch (const std::exception& e) {
        ESP_LOGE(TAG, "Error closing stream: %s", e.what());
    }
    
    ESP_LOGI(TAG, "Mp3 stream playback ended");
    playing_type_ = PlayingType::None;
}

constexpr size_t OPTIMAL_WRITE_SAMPLES = 240; // 10ms@24kHz

// 48kHz → 24kHz 2:1 降采样（丢弃一半样本或做均值）：
std::vector<int16_t> Downsample2x(const std::vector<int16_t>& input) {
    std::vector<int16_t> output;
    output.reserve(input.size() / 2);
    for (size_t i = 0; i + 1 < input.size(); i += 2) {
        // 简单平均，减少高频失真
        output.push_back((input[i] / 2) + (input[i + 1] / 2));
    }
    return output;
}

// 添加辅助函数处理解码后的PCM数据
void Application::ProcessDecodedPcmData(std::vector<int16_t>& pcm_data) {
    auto codec = Board::GetInstance().GetAudioCodec();
    std::vector<int16_t> resampled = Downsample2x(pcm_data);
    size_t offset = 0;
    //ESP_LOGI(TAG, "Resampled %zu samples", resampled.size());
    while (offset < resampled.size()) {
        size_t chunk_size = std::min(OPTIMAL_WRITE_SAMPLES, resampled.size() - offset);
        std::vector<int16_t> chunk(resampled.begin() + offset, 
                                   resampled.begin() + offset + chunk_size);
        std::unique_lock<std::mutex> lock(mutex_);
        if (!audio_decode_queue_.empty()) {
            ESP_LOGI(TAG, "Audio queue not empty, pausing MP3 playback to play queue audio");
            // 直接 wait，不要 unlock
            audio_decode_cv_.wait(lock, [this] {
                return audio_decode_queue_.empty();
            });
            continue;
        }
        codec->OutputData(chunk);
        offset += chunk_size;
    }
}


void Application::PlayLocalAudio() {
}

void Application::ToggleChatState() {
    if (device_state_ == kDeviceStateActivating) {
        SetDeviceState(kDeviceStateIdle);
        return;
    }

    if (!protocol_) {
        ESP_LOGE(TAG, "Protocol not initialized");
        return;
    }

    if (device_state_ == kDeviceStateIdle) {
        Schedule([this]() {
            SetDeviceState(kDeviceStateConnecting);
            if (!protocol_->OpenAudioChannel()) {
                return;
            }

            SetListeningMode(realtime_chat_enabled_ ? kListeningModeRealtime : kListeningModeAutoStop);
        });
    } else if (device_state_ == kDeviceStateSpeaking) {
        Schedule([this]() {
            AbortSpeaking(kAbortReasonNone);
        });
    } else if (device_state_ == kDeviceStateListening) {
        Schedule([this]() {
            protocol_->CloseAudioChannel();
        });
    }
}

void Application::StartListening() {
    if (device_state_ == kDeviceStateActivating) {
        SetDeviceState(kDeviceStateIdle);
        return;
    }

    if (!protocol_) {
        ESP_LOGE(TAG, "Protocol not initialized");
        return;
    }
    
    if (device_state_ == kDeviceStateIdle) {
        Schedule([this]() {
            if (!protocol_->IsAudioChannelOpened()) {
                SetDeviceState(kDeviceStateConnecting);
                if (!protocol_->OpenAudioChannel()) {
                    return;
                }
            }

            SetListeningMode(kListeningModeManualStop);
        });
    } else if (device_state_ == kDeviceStateSpeaking) {
        Schedule([this]() {
            AbortSpeaking(kAbortReasonNone);
            SetListeningMode(kListeningModeManualStop);
        });
    }
}

void Application::StopListening() {
    const std::array<int, 3> valid_states = {
        kDeviceStateListening,
        kDeviceStateSpeaking,
        kDeviceStateIdle,
    };
    // If not valid, do nothing
    if (std::find(valid_states.begin(), valid_states.end(), device_state_) == valid_states.end()) {
        return;
    }

    Schedule([this]() {
        if (device_state_ == kDeviceStateListening) {
            protocol_->SendStopListening();
            SetDeviceState(kDeviceStateIdle);
        }
    });
}

void Application::Start() {
    auto& board = Board::GetInstance();
    SetDeviceState(kDeviceStateStarting);

    /* Setup the display */
    auto display = board.GetDisplay();

    /* Setup the audio codec */
    auto codec = board.GetAudioCodec();
    opus_decoder_ = std::make_unique<OpusDecoderWrapper>(codec->output_sample_rate(), 1, OPUS_FRAME_DURATION_MS);
    opus_encoder_ = std::make_unique<OpusEncoderWrapper>(16000, 1, OPUS_FRAME_DURATION_MS);
    if (realtime_chat_enabled_) {
        ESP_LOGI(TAG, "Realtime chat enabled, setting opus encoder complexity to 0");
        opus_encoder_->SetComplexity(0);
    } else if (board.GetBoardType() == "ml307") {
        ESP_LOGI(TAG, "ML307 board detected, setting opus encoder complexity to 5");
        opus_encoder_->SetComplexity(5);
    } else {
        ESP_LOGI(TAG, "WiFi board detected, setting opus encoder complexity to 3");
        opus_encoder_->SetComplexity(3);
    }

    if (codec->input_sample_rate() != 16000) {
        input_resampler_.Configure(codec->input_sample_rate(), 16000);
        reference_resampler_.Configure(codec->input_sample_rate(), 16000);
    }
    codec->Start();

    xTaskCreatePinnedToCore([](void* arg) {
        Application* app = (Application*)arg;
        app->AudioLoop();
        vTaskDelete(NULL);
    }, "audio_loop", 4096 * 2, this, 8, &audio_loop_task_handle_, realtime_chat_enabled_ ? 1 : 0);

    
    xTaskCreatePinnedToCore([](void* arg) {
        Application* app = (Application*)arg;
        app->PlayingLoop();
        vTaskDelete(NULL);
    }, "playing_loop", 4096 * 8, this, 8, &play_loop_task_handle_, 0);
    

    /* Wait for the network to be ready */
    board.StartNetwork();

    // Check for new firmware version or get the MQTT broker address
    CheckNewVersion();

    // Initialize the protocol
    display->SetStatus(Lang::Strings::LOADING_PROTOCOL);

    if (ota_.HasMqttConfig()) {
        protocol_ = std::make_unique<MqttProtocol>();
    } else if (ota_.HasWebsocketConfig()) {
        protocol_ = std::make_unique<WebsocketProtocol>();
    } else {
        ESP_LOGW(TAG, "No protocol specified in the OTA config, using MQTT");
        protocol_ = std::make_unique<MqttProtocol>();
    }

    protocol_->OnNetworkError([this](const std::string& message) {
        SetDeviceState(kDeviceStateIdle);
        Alert(Lang::Strings::ERROR, message.c_str(), "sad", Lang::Sounds::P3_EXCLAMATION);
    });
    protocol_->OnIncomingAudio([this](AudioStreamPacket&& packet) {
        //if (device_state_ == kDeviceStateIdle) {
        //    ESP_LOGW(TAG, "Audio packet received while idle, ignoring");
        //    return;
        //}
        const int max_packets_in_queue = 600 / OPUS_FRAME_DURATION_MS;
        std::lock_guard<std::mutex> lock(mutex_);
        if (audio_decode_queue_.size() < max_packets_in_queue) {
            audio_decode_queue_.emplace_back(std::move(packet));
        }
    });
    protocol_->OnAudioChannelOpened([this, codec, &board]() {
        board.SetPowerSaveMode(false);
        if (protocol_->server_sample_rate() != codec->output_sample_rate()) {
            ESP_LOGW(TAG, "Server sample rate %d does not match device output sample rate %d, resampling may cause distortion",
                protocol_->server_sample_rate(), codec->output_sample_rate());
        }
        SetDecodeSampleRate(protocol_->server_sample_rate(), protocol_->server_frame_duration());
        auto& thing_manager = iot::ThingManager::GetInstance();
        protocol_->SendIotDescriptors(thing_manager.GetDescriptorsJson());
        std::string states;
        if (thing_manager.GetStatesJson(states, false)) {
            protocol_->SendIotStates(states);
        }
    });
    protocol_->OnAudioChannelClosed([this, &board]() {
        board.SetPowerSaveMode(true);
        Schedule([this]() {
            auto display = Board::GetInstance().GetDisplay();
            display->SetChatMessage("system", "");
            SetDeviceState(kDeviceStateIdle);
        });
    });
    protocol_->OnIncomingJson([this, display](const cJSON* root) {
        // Parse JSON data
        auto type = cJSON_GetObjectItem(root, "type");
        if (strcmp(type->valuestring, "tts") == 0) {
            auto state = cJSON_GetObjectItem(root, "state");
            if (strcmp(state->valuestring, "start") == 0) {
                Schedule([this]() {
                    aborted_ = false;
                    //device_state_ == kDeviceStateIdle ||  这里去掉防止播放mp3被打断
                    if (device_state_ == kDeviceStateListening) {
                        SetDeviceState(kDeviceStateSpeaking);
                    }
                });
            } else if (strcmp(state->valuestring, "stop") == 0) {
                Schedule([this]() {
                    background_task_->WaitForCompletion();
                    if (device_state_ == kDeviceStateSpeaking) {
                        if (listening_mode_ == kListeningModeManualStop) {
                            SetDeviceState(kDeviceStateIdle);
                        } else {
                            SetDeviceState(kDeviceStateListening);
                        }
                    }
                });
            } else if (strcmp(state->valuestring, "sentence_start") == 0) {
                auto text = cJSON_GetObjectItem(root, "text");
                if (text != NULL) {
                    ESP_LOGI(TAG, "<< %s", text->valuestring);
                    Schedule([this, display, message = std::string(text->valuestring)]() {
                        display->SetChatMessage("assistant", message.c_str());
                    });
                }
            }
        } else if (strcmp(type->valuestring, "stt") == 0) {
            if (device_state_ == kDeviceStateIdle) {
                ESP_LOGW(TAG, "STT packet received while idle, ignoring");
                return;
            }
            auto text = cJSON_GetObjectItem(root, "text");
            if (text != NULL) {
                ESP_LOGI(TAG, ">> %s", text->valuestring);
                Schedule([this, display, message = std::string(text->valuestring)]() {
                    display->SetChatMessage("user", message.c_str());
                });
            }
        } else if (strcmp(type->valuestring, "llm") == 0) {
            auto emotion = cJSON_GetObjectItem(root, "emotion");
            if (emotion != NULL) {
                Schedule([this, display, emotion_str = std::string(emotion->valuestring)]() {
                    display->SetEmotion(emotion_str.c_str());
                });
            }
        } else if (strcmp(type->valuestring, "iot") == 0) {
            auto commands = cJSON_GetObjectItem(root, "commands");
            if (commands != NULL) {
                auto& thing_manager = iot::ThingManager::GetInstance();
                for (int i = 0; i < cJSON_GetArraySize(commands); ++i) {
                    auto command = cJSON_GetArrayItem(commands, i);
                    thing_manager.Invoke(command);
                }
            }
        } else if (strcmp(type->valuestring, "system") == 0) {
            auto command = cJSON_GetObjectItem(root, "command");
            if (command != NULL) {
                ESP_LOGI(TAG, "System command: %s", command->valuestring);
                if (strcmp(command->valuestring, "reboot") == 0) {
                    // Do a reboot if user requests a OTA update
                    Schedule([this]() {
                        Reboot();
                    });
                } else {
                    ESP_LOGW(TAG, "Unknown system command: %s", command->valuestring);
                }
            }
        } else if (strcmp(type->valuestring, "alert") == 0) {
            auto status = cJSON_GetObjectItem(root, "status");
            auto message = cJSON_GetObjectItem(root, "message");
            auto emotion = cJSON_GetObjectItem(root, "emotion");
            if (status != NULL && message != NULL && emotion != NULL) {
                Alert(status->valuestring, message->valuestring, emotion->valuestring, Lang::Sounds::P3_VIBRATION);
            } else {
                ESP_LOGW(TAG, "Alert command requires status, message and emotion");
            }
        }
    });
    bool protocol_started = protocol_->Start();

    audio_processor_->Initialize(codec, realtime_chat_enabled_);
    audio_processor_->OnOutput([this](std::vector<int16_t>&& data) {
        background_task_->Schedule([this, data = std::move(data)]() mutable {
            if (protocol_->IsAudioChannelBusy()) {
                return;
            }
            opus_encoder_->Encode(std::move(data), [this](std::vector<uint8_t>&& opus) {
                AudioStreamPacket packet;
                packet.payload = std::move(opus);
                packet.timestamp = last_output_timestamp_;
                last_output_timestamp_ = 0;
                Schedule([this, packet = std::move(packet)]() {
                    protocol_->SendAudio(packet);
                });
            });
        });
    });
    audio_processor_->OnVadStateChange([this](bool speaking) {
        if (device_state_ == kDeviceStateListening) {
            Schedule([this, speaking]() {
                if (speaking) {
                    voice_detected_ = true;
                } else {
                    voice_detected_ = false;
                }
                auto led = Board::GetInstance().GetLed();
                led->OnStateChanged();
            });
        }
    });

#if CONFIG_USE_WAKE_WORD_DETECT
    wake_word_detect_.Initialize(codec);
    wake_word_detect_.OnWakeWordDetected([this](const std::string& wake_word) {
        Schedule([this, &wake_word]() {
            if (device_state_ == kDeviceStateIdle) {
                SetDeviceState(kDeviceStateConnecting);
                wake_word_detect_.EncodeWakeWordData();

                if (!protocol_ || !protocol_->OpenAudioChannel()) {
                    wake_word_detect_.StartDetection();
                    return;
                }
                
                AudioStreamPacket packet;
                // Encode and send the wake word data to the server
                while (wake_word_detect_.GetWakeWordOpus(packet.payload)) {
                    protocol_->SendAudio(packet);
                }
                // Set the chat state to wake word detected
                protocol_->SendWakeWordDetected(wake_word);
                ESP_LOGI(TAG, "Wake word detected: %s", wake_word.c_str());
                SetListeningMode(realtime_chat_enabled_ ? kListeningModeRealtime : kListeningModeAutoStop);
            } else if (device_state_ == kDeviceStateSpeaking) {
                AbortSpeaking(kAbortReasonWakeWordDetected);
            } else if (device_state_ == kDeviceStateActivating) {
                SetDeviceState(kDeviceStateIdle);
            }
        });
    });
    wake_word_detect_.StartDetection();
#endif

    // Wait for the new version check to finish
    xEventGroupWaitBits(event_group_, CHECK_NEW_VERSION_DONE_EVENT, pdTRUE, pdFALSE, portMAX_DELAY);
    SetDeviceState(kDeviceStateIdle);

    if (protocol_started) {
        std::string message = std::string(Lang::Strings::VERSION) + ota_.GetCurrentVersion();
        display->ShowNotification(message.c_str());
        display->SetChatMessage("system", "");
        // Play the success sound to indicate the device is ready
        ResetDecoder();
        PlaySound(Lang::Sounds::P3_SUCCESS);
    }
    
    // Enter the main event loop
    MainEventLoop();
}

void Application::OnClockTimer() {
    clock_ticks_++;

    // Print the debug info every 10 seconds
    if (clock_ticks_ % 10 == 0) {
        // SystemInfo::PrintRealTimeStats(pdMS_TO_TICKS(1000));

        int free_sram = heap_caps_get_free_size(MALLOC_CAP_INTERNAL);
        int min_free_sram = heap_caps_get_minimum_free_size(MALLOC_CAP_INTERNAL);
        ESP_LOGI(TAG, "Free internal: %u minimal internal: %u", free_sram, min_free_sram);

        // If we have synchronized server time, set the status to clock "HH:MM" if the device is idle
        if (ota_.HasServerTime()) {
            if (device_state_ == kDeviceStateIdle) {
                Schedule([this]() {
                    // Set status to clock "HH:MM"
                    time_t now = time(NULL);
                    char time_str[64];
                    strftime(time_str, sizeof(time_str), "%H:%M  ", localtime(&now));
                    Board::GetInstance().GetDisplay()->SetStatus(time_str);
                });
            }
        }
    }
}

// Add a async task to MainLoop
void Application::Schedule(std::function<void()> callback) {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        main_tasks_.push_back(std::move(callback));
    }
    xEventGroupSetBits(event_group_, SCHEDULE_EVENT);
}

// The Main Event Loop controls the chat state and websocket connection
// If other tasks need to access the websocket or chat state,
// they should use Schedule to call this function
void Application::MainEventLoop() {
    while (true) {
        auto bits = xEventGroupWaitBits(event_group_, SCHEDULE_EVENT, pdTRUE, pdFALSE, portMAX_DELAY);

        if (bits & SCHEDULE_EVENT) {
            std::unique_lock<std::mutex> lock(mutex_);
            std::list<std::function<void()>> tasks = std::move(main_tasks_);
            lock.unlock();
            for (auto& task : tasks) {
                try {
                    task();
                } catch (const std::exception& e) {
                    ESP_LOGE(TAG, "Exception in background task: %s", e.what());
                } catch (...) {
                    ESP_LOGE(TAG, "Unknown exception in background task");
                }
            }
        }
    }
}

// The Audio Loop is used to input and output audio data
void Application::AudioLoop() {
    auto codec = Board::GetInstance().GetAudioCodec();
    while (true) {
        OnAudioInput();
        if (codec->output_enabled()) {
            OnAudioOutput();
        }
    }
}

// Playing audio Loop
void Application::PlayingLoop() {
    while (true) {
        if (current_playing_url_.empty() or playing_type_ != PlayingType::Mp3Stream) {
            //ESP_LOGI(TAG, "Waiting for audio stream...");
            vTaskDelay(pdMS_TO_TICKS(1000));
            continue;
        }
        if (playing_type_ == PlayingType::Mp3Stream) {
            //PlayMp3Stream();
            PlayOnlineList();
        }  else if (playing_type_ == PlayingType::LocalAudio) {
            PlayLocalAudio();
        }
        else {
            ESP_LOGW(TAG, "Unknown playing type: %d", (int)playing_type_);
        }
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}

void Application::OnAudioOutput() {
    if (busy_decoding_audio_) {
        return;
    }

    auto now = std::chrono::steady_clock::now();
    auto codec = Board::GetInstance().GetAudioCodec();
    const int max_silence_seconds = 10;

    std::unique_lock<std::mutex> lock(mutex_);
    if (audio_decode_queue_.empty()) {
        // Disable the output if there is no audio data for a long time
        if (device_state_ == kDeviceStateIdle && playing_type_ == PlayingType::None) {
            auto duration = std::chrono::duration_cast<std::chrono::seconds>(now - last_output_time_).count();
            if (duration > max_silence_seconds) {
                codec->EnableOutput(false);
            }
        }
        return;
    }

    if (device_state_ == kDeviceStateListening) {
        audio_decode_queue_.clear();
        audio_decode_cv_.notify_all();
        return;
    }

    auto packet = std::move(audio_decode_queue_.front());
    audio_decode_queue_.pop_front();
    lock.unlock();
    audio_decode_cv_.notify_all();

    busy_decoding_audio_ = true;

    background_task_->Schedule([this, codec, packet = std::move(packet)]() mutable {
        busy_decoding_audio_ = false;
        if (aborted_) {
            return;
        }
        std::vector<int16_t> pcm;
        if (!opus_decoder_->Decode(std::move(packet.payload), pcm)) {
            ESP_LOGW(TAG, "Opus decode failed");
            return;
        }
        // Resample if the sample rate is different
        if (opus_decoder_->sample_rate() != codec->output_sample_rate()) {
            int target_size = output_resampler_.GetOutputSamples(pcm.size());
            std::vector<int16_t> resampled(target_size);
            output_resampler_.Process(pcm.data(), pcm.size(), resampled.data());
            pcm = std::move(resampled);
        }
        codec->OutputData(pcm);
        last_output_timestamp_ = packet.timestamp;
        last_output_time_ = std::chrono::steady_clock::now();
    });
}

void Application::OnAudioInput() {
#if CONFIG_USE_WAKE_WORD_DETECT
    if (wake_word_detect_.IsDetectionRunning()) {
        std::vector<int16_t> data;
        int samples = wake_word_detect_.GetFeedSize();
        if (samples > 0) {
            ReadAudio(data, 16000, samples);
            wake_word_detect_.Feed(data);
            return;
        }
    }
#endif
    if (audio_processor_->IsRunning()) {
        std::vector<int16_t> data;
        int samples = audio_processor_->GetFeedSize();
        if (samples > 0) {
            ReadAudio(data, 16000, samples);
            audio_processor_->Feed(data);
            return;
        }
    }

    vTaskDelay(pdMS_TO_TICKS(30));
}

void Application::ReadAudio(std::vector<int16_t>& data, int sample_rate, int samples) {
    auto codec = Board::GetInstance().GetAudioCodec();
    if (codec->input_sample_rate() != sample_rate) {
        data.resize(samples * codec->input_sample_rate() / sample_rate);
        if (!codec->InputData(data)) {
            return;
        }
        if (codec->input_channels() == 2) {
            auto mic_channel = std::vector<int16_t>(data.size() / 2);
            auto reference_channel = std::vector<int16_t>(data.size() / 2);
            for (size_t i = 0, j = 0; i < mic_channel.size(); ++i, j += 2) {
                mic_channel[i] = data[j];
                reference_channel[i] = data[j + 1];
            }
            auto resampled_mic = std::vector<int16_t>(input_resampler_.GetOutputSamples(mic_channel.size()));
            auto resampled_reference = std::vector<int16_t>(reference_resampler_.GetOutputSamples(reference_channel.size()));
            input_resampler_.Process(mic_channel.data(), mic_channel.size(), resampled_mic.data());
            reference_resampler_.Process(reference_channel.data(), reference_channel.size(), resampled_reference.data());
            data.resize(resampled_mic.size() + resampled_reference.size());
            for (size_t i = 0, j = 0; i < resampled_mic.size(); ++i, j += 2) {
                data[j] = resampled_mic[i];
                data[j + 1] = resampled_reference[i];
            }
        } else {
            auto resampled = std::vector<int16_t>(input_resampler_.GetOutputSamples(data.size()));
            input_resampler_.Process(data.data(), data.size(), resampled.data());
            data = std::move(resampled);
        }
    } else {
        data.resize(samples);
        if (!codec->InputData(data)) {
            return;
        }
    }
}

void Application::AbortSpeaking(AbortReason reason) {
    ESP_LOGI(TAG, "Abort speaking");
    aborted_ = true;
    protocol_->SendAbortSpeaking(reason);
}

void Application::SetListeningMode(ListeningMode mode) {
    listening_mode_ = mode;
    SetDeviceState(kDeviceStateListening);
}

void Application::SetDeviceState(DeviceState state) {
    ESP_LOGI(TAG, "STATE: %s (called from %s:%d)", STATE_STRINGS[state], __FILE__, __LINE__);
    if (device_state_ == state) {
        return;
    }
    
    clock_ticks_ = 0;
    auto previous_state = device_state_;
    device_state_ = state;
    ESP_LOGI(TAG, "STATE: %s", STATE_STRINGS[device_state_]);
    // The state is changed, wait for all background tasks to finish
    background_task_->WaitForCompletion();

    auto& board = Board::GetInstance();
    auto display = board.GetDisplay();
    auto led = board.GetLed();
    led->OnStateChanged();
    switch (state) {
        case kDeviceStateUnknown:
        case kDeviceStateIdle:
            display->SetStatus(Lang::Strings::STANDBY);
            display->SetEmotion("neutral");
            audio_processor_->Stop();
#if CONFIG_USE_WAKE_WORD_DETECT
            wake_word_detect_.StartDetection();
#endif
            break;
        case kDeviceStateConnecting:
            display->SetStatus(Lang::Strings::CONNECTING);
            display->SetEmotion("neutral");
            display->SetChatMessage("system", "");
            break;
        case kDeviceStateListening:
            display->SetStatus(Lang::Strings::LISTENING);
            display->SetEmotion("neutral");

            // Update the IoT states before sending the start listening command
            UpdateIotStates();

            // Make sure the audio processor is running
            if (!audio_processor_->IsRunning()) {
                // Send the start listening command
                protocol_->SendStartListening(listening_mode_);
                if (listening_mode_ == kListeningModeAutoStop && previous_state == kDeviceStateSpeaking) {
                    // FIXME: Wait for the speaker to empty the buffer
                    vTaskDelay(pdMS_TO_TICKS(120));
                }
                opus_encoder_->ResetState();
#if CONFIG_USE_WAKE_WORD_DETECT
                wake_word_detect_.StopDetection();
#endif
                audio_processor_->Start();
            }
            break;
        case kDeviceStateSpeaking:
            display->SetStatus(Lang::Strings::SPEAKING);

            if (listening_mode_ != kListeningModeRealtime) {
                audio_processor_->Stop();
#if CONFIG_USE_WAKE_WORD_DETECT
                wake_word_detect_.StartDetection();
#endif
            }
            ResetDecoder();
            break;
        default:
            // Do nothing
            break;
    }
}

void Application::ResetDecoder() {
    std::lock_guard<std::mutex> lock(mutex_);
    opus_decoder_->ResetState();
    audio_decode_queue_.clear();
    audio_decode_cv_.notify_all();
    last_output_time_ = std::chrono::steady_clock::now();
    
    // Reset the playing type
    playing_type_ = PlayingType::None;

    auto codec = Board::GetInstance().GetAudioCodec();
    codec->EnableOutput(true);
}

void Application::SetDecodeSampleRate(int sample_rate, int frame_duration) {
    if (opus_decoder_->sample_rate() == sample_rate && opus_decoder_->duration_ms() == frame_duration) {
        return;
    }

    opus_decoder_.reset();
    opus_decoder_ = std::make_unique<OpusDecoderWrapper>(sample_rate, 1, frame_duration);

    auto codec = Board::GetInstance().GetAudioCodec();
    if (opus_decoder_->sample_rate() != codec->output_sample_rate()) {
        ESP_LOGI(TAG, "Resampling audio from %d to %d", opus_decoder_->sample_rate(), codec->output_sample_rate());
        output_resampler_.Configure(opus_decoder_->sample_rate(), codec->output_sample_rate());
    }
}

void Application::UpdateIotStates() {
    auto& thing_manager = iot::ThingManager::GetInstance();
    std::string states;
    if (thing_manager.GetStatesJson(states, true)) {
        protocol_->SendIotStates(states);
    }
}

void Application::Reboot() {
    ESP_LOGI(TAG, "Rebooting...");
    esp_restart();
}

void Application::WakeWordInvoke(const std::string& wake_word) {
    std::unique_lock<std::mutex> lock(mutex_);
    if (playing_type_ == PlayingType::Mp3Stream) {
        // 打断mp3播放
        aborted_ = true; // 通知后台任务退出
        background_task_->WaitForCompletion();
        ResetDecoder();  // 清空队列，重置状态
        ESP_LOGI(TAG, "Mp3 stream interrupted by wake word");
    }
    lock.unlock();

    if (device_state_ == kDeviceStateIdle) {
        ToggleChatState();
        Schedule([this, wake_word]() {
            if (protocol_) {
                protocol_->SendWakeWordDetected(wake_word); 
            }
        }); 
    } else if (device_state_ == kDeviceStateSpeaking) {
        Schedule([this]() {
            AbortSpeaking(kAbortReasonNone);
        });
    } else if (device_state_ == kDeviceStateListening) {   
        Schedule([this]() {
            if (protocol_) {
                protocol_->CloseAudioChannel();
            }
        });
    }
}

bool Application::CanEnterSleepMode() {
    if (device_state_ != kDeviceStateIdle) {
        return false;
    }

    if (protocol_ && protocol_->IsAudioChannelOpened()) {
        return false;
    }

    // Now it is safe to enter sleep mode
    return true;
}
