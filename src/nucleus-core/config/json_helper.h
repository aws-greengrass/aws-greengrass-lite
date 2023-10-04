#pragma once

#include "config_manager.h"
#include "data/struct_model.h"
#include <rapidjson/writer.h>
#include <rapidjson/stringbuffer.h>

namespace config {
    //
    // Translation of a log line to/from JSON
    //
    struct TlogLine {
        static constexpr const char *TS = {"TS"};
        Timestamp timestamp;
        static constexpr const char *TP = {"TP"};
        std::vector<std::string> topicPath;
        static constexpr const char *W = {"W"};
        WhatHappened action;
        static constexpr const char *V = {"V"};
        data::StructElement value;

        void serialize(data::Environment &environment, rapidjson::Writer<rapidjson::StringBuffer> &writer);

        void deSerialize();
    };

    struct JsonHelper {
        static void serialize(data::Environment &environment, rapidjson::Writer<rapidjson::StringBuffer> &writer,
                              const data::StructElement &value);
    };
}
