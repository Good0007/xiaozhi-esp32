typedef struct {
    uint32_t id;
    std::string name;
    uint8_t tag = 0;  // 0: 音乐台 1 : 新闻 3: 综合台
  } RadioItem;

  
std::vector<RadioItem> radios = {
    {4915, "清晨音乐台",0},
    {1223, "怀旧好声音",0},
    {4866, "浙江音乐调频",0},
    //{20211686, "成都年代音乐怀旧好声音",0},
    {1739, "厦门音乐广播",0},
    {1271, "深圳飞扬971",0},
    {20240, "山东经典音乐广播",0},
    {20500066, "年代音乐1022",0},
    {1296, "湖北经典音乐广播",0},
    {267, "上海经典947",0},
    {20212426, "崂山921",0},
    {20003, "天津TIKI FM100.5",0},
    {1111, "四川城市之音",0},
    {4936, "江苏音乐广播PlayFM897",0},
    {4237, "长沙FM101.7城市之声,0"},
    {1665, "山东音乐广播",0},
    {1947, "安徽音乐广播",0},
    {332, "北京音乐广播",0},
    {4932, "山西音乐广播",0},
    {20500149, "两广之声音乐台",0},
    {4804, "怀集音乐之声",0},
    {1649, "河北音乐广播",0},
    {4938, "江苏经典流行音乐",0},
    {1260, "广东音乐之声",0},
    {273, "上海流行音乐LoveRadio",0},
    {274, "上海动感101",0},
    {2803, "苏州音乐广播",0},
    //{5021381, "959年代音乐怀旧好声音",0},
    {15318569, "AsiaFM 亚洲粤语台",0},
    {5022308, "500首华语经典,0"},
    {4875, "FM950广西音乐台",0},
    {5022023, "上海KFM981",0},
    {20207761, "80后音悦台",0},
    {1654, "石家庄音乐广播",0},
    {20212227, "经典FM1008",0},
    {1671, "济南音乐广播FM88.7",0},
    {5021912, "AsiaFM 亚洲经典台",0},
    {1084, "大连1067",0},
    {1831, "吉林音乐广播",0},
    {5022405, "AsiaFM 亚洲音乐台",0},
    {4581, "亚洲音乐成都FM96.5",0},
    {20071, "AsiaFM 亚洲天空台",0},
    {20033, "1047 Nice FM",0},
    {4930, "FM102.2亲子智慧电台",0},
    {4846, "893音乐广播",0},
    {4923, "徐州音乐广播FM91.9",0},
    {4878, "海南音乐广播",0},
    {20211575, "经典983电台",0},
    {20500097, "经典音乐广播FM94.8",0},
    {1975, "MUSIC876",0},
    {5022391, "Easy Fm",0},
    {20067, "贵州FM91.6音乐广播",0},
    {20500053, "经典958",0},
    {5022520, "盛京FM105.6",0},
    {20091, "中国校园之声",0},
    {4979, "89.3芒果音乐台",0},
    {20207762, "河南经典FM",0},
    {4921, "郑州音乐广播",0},
    {4871, "唐山音乐广播",0},
    {1683, "烟台音乐广播FM105.9",0},
    {20212387, "凤凰音乐",0},
    {20500187, "云梦音乐台",0}
};

const std::string fm_url = "http://lhttp.qtfm.cn/live/%lu/64k.mp3";

class NetRadio
{
public:
    NetRadio() = default;
    ~NetRadio() = default;


    // 模糊搜索
    std::vector<RadioItem> searchByName(const std::string& keyword) {
        std::vector<RadioItem> result;
        for (const auto& item : radios) {
            if (item.name.find(keyword) != std::string::npos) {
                result.push_back(item);
            }
        }
        return result;
    }

    // 根据 id 获取 FM URL
    std::string getFmUrlById(uint32_t id) {
        char url[128];
        snprintf(url, sizeof(url), fm_url.c_str(), (unsigned long)id);
        return std::string(url);
    }

    //根据 tag 获取 FM 列表
    std::vector<RadioItem> getFmListByTag(uint8_t tag) {
        std::vector<RadioItem> result;
        for (const auto& item : radios) {
            if (item.tag == tag) {
                result.push_back(item);
            }
        }
        return result;
    }

    // 获取全部 FM 列表
    const std::vector<RadioItem>& getAll() const {
        return radios;
    }

    //获取全部 fm 列表，转换为 字符串
    std::string getAllAsString() const {
        std::string result;
        for (const auto& item : radios) {
            result += std::to_string(item.id) + ": " + item.name + "\n";
        }
        return result;
    }

    std::string getFmListByTagAsString(uint8_t tag) {
        std::vector<RadioItem>radios  = getFmListByTag(tag);
        if (radios.empty()) {
            return "没有找到符合条件的电台";
        }
        std::string result;
        for (const auto& item : radios) {
            result += std::to_string(item.id) + ": " + item.name + "\n";
        }
        return result;
    }

    std::string searchByNameAsString(const std::string& keyword) {
        std::vector<RadioItem> result = searchByName(keyword);
        if (result.empty()) {
            return "没有找到符合条件的电台";
        }
        std::string result_str;
        for (const auto& item : result) {
            result_str += std::to_string(item.id) + ": " + item.name + "\n";
        }
        return result_str;
    }
};