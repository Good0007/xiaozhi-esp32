#include "iot/thing.h"
#include "board.h"
#include "ota_utils.h"
#include <esp_log.h>

#define TAG "Switcher"

namespace iot {

// 这里仅定义 Speaker 的属性和方法，不包含具体的实现
class Switcher : public Thing {
public:
    Switcher() : Thing("Switcher", "系统切换") {
        // 定义设备可以被远程执行的指令
        methods_.AddMethod("SwitchToRadio", "切换系统（必须当用户明确说切换到：收音机/电台模式）", ParameterList({
            Parameter("state", "1", kValueTypeNumber, true)
        }), [this](const ParameterList& parameters) {
            auto display_ = Board::GetInstance().GetDisplay();
            OtaUtils::SwitchToOtherApp(display_);
        });
    }
};

} // namespace iot

DECLARE_THING(Switcher);
