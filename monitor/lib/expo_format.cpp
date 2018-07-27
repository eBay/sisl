#include "expo_format.hpp"
#include <string>

namespace monitor {

std::string GetContentTypeWithExpoFormat(ExpositionFormats format) {
    static std::string proto_content_type = "application/vnd.google.protobuf; "
                                            "proto=io.prometheus.client.MetricFamily; "
                                            "encoding=delimited";

    static std::string json_content_type = "application/json";
    static std::string text_content_type = "text/plain";

    std::string result = "";
    switch (format) {
    case ExpositionFormats::kProtoBufferFormat:
        result = proto_content_type;
        break;
    case ExpositionFormats::kJsonFormat:
        result = json_content_type;
        break;
    case ExpositionFormats::kTextFormat:
        result = text_content_type;
        break;
    default:
        result = text_content_type;
    }

    return result;
}
} // namespace monitor
