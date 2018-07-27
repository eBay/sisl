#ifndef MONITOR_EXPOSITION_FORMAT_H_
#define MONITOR_EXPOSITION_FORMAT_H_

#include <string>

namespace monitor {

enum ExpositionFormats { kProtoBufferFormat = 0, kJsonFormat, kTextFormat, kUnknownFormat };

// const static ExpositionFormats kExpositionFormat = ExpositionFormats::kProtoBufferFormat;
// to support Prometheus 2.0 that requires text format
const static ExpositionFormats kExpositionFormat = ExpositionFormats::kTextFormat;

std::string GetContentTypeWithExpoFormat(ExpositionFormats format);

} // namespace monitor

#endif // MONITOR_EXPOSITION_FORMAT_H_
