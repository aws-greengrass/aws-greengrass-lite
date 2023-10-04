#include "json_helper.h"
#include "data/environment.h"

namespace config {

    void TlogLine::serialize(
        data::Environment &environment, rapidjson::Writer<rapidjson::StringBuffer> &writer
    ) {
        writer.StartObject();

        writer.Key(TS);
        writer.Int64(timestamp.asMilliseconds());

        writer.Key(TP);
        writer.StartArray();
        for(const auto &i : topicPath) {
            writer.String(i.c_str());
        }
        writer.EndArray();

        const char *actionText = "";
        if((action & WhatHappened::interiorAdded) != WhatHappened::never) {
            actionText = "interiorAdded";
        } else if((action & WhatHappened::childChanged) != WhatHappened::never) {
            actionText = "childChanged";
        } else if((action & WhatHappened::childRemoved) != WhatHappened::never) {
            actionText = "childRemoved";
        } else if((action & WhatHappened::changed) != WhatHappened::never) {
            actionText = "changed";
        } else if((action & WhatHappened::removed) != WhatHappened::never) {
            actionText = "removed";
        } else if((action & WhatHappened::timestampUpdated) != WhatHappened::never) {
            actionText = "timestampUpdated";
        } else if((action & WhatHappened::initialized) != WhatHappened::never) {
            actionText = "initialized";
        }
        writer.Key("W");
        writer.String(actionText);

        writer.Key("V");
        JsonHelper::serialize(environment, writer, value);

        writer.EndObject();
    }

    // NOLINTNEXTLINE(*-no-recursion)
    void JsonHelper::serialize(
        data::Environment &environment,
        rapidjson::Writer<rapidjson::StringBuffer> &writer,
        const data::StructElement &value
    ) {
        switch(value.getType()) {
        case data::ValueTypes::NONE:
            writer.Null();
            break;
        case data::ValueTypes::BOOL:
            writer.Bool(value.getBool());
            break;
        case data::ValueTypes::INT:
            writer.Int64(static_cast<int64_t>(value.getInt()));
            break;
        case data::ValueTypes::DOUBLE:
            writer.Double(value.getDouble());
            break;
        case data::ValueTypes::CONTAINER:
            if(value.isType<data::ListModelBase>()) {
                std::shared_ptr<data::ListModelBase> list =
                    value.castContainer<data::ListModelBase>()->copy();
                auto size = static_cast<int32_t>(list->size());
                writer.StartArray();
                for(int32_t idx = 0; idx < size; idx++) {
                    serialize(environment, writer, list->get(idx));
                }
                writer.EndArray();
            } else {
                std::shared_ptr<data::StructModelBase> s =
                    value.castContainer<data::StructModelBase>()->copy();
                std::vector<data::StringOrd> keys = s->getKeys();
                writer.StartObject();
                for(const auto &i : keys) {
                    std::string k = environment.stringTable.getString(i);
                    writer.Key(k.c_str());
                    serialize(environment, writer, s->get(i));
                }
                writer.EndObject();
            }
            break;
        default:
            writer.String(value.getString().c_str());
            break;
        }
    }

} // namespace config
