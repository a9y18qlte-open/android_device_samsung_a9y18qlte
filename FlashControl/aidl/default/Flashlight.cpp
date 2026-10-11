/*
 * Copyright (C) 2023 The LineageOS Project
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "Flashlight.h"

#include <SafeStoi.h>

#include <android-base/file.h>
#include <android-base/properties.h>

namespace aidl {
namespace vendor {
namespace samsung_ext {
namespace hardware {
namespace camera {
namespace flashlight {

using ::android::base::GetIntProperty;
using ::android::base::ReadFileToString;
using ::android::base::SetProperty;
using ::android::base::WriteStringToFile;

static constexpr const char* FLASH_NODE = "/sys/class/camera/flash/rear_flash";
static constexpr const char *FLASH_BRIGHTNESS_PROP = "persist.ext.flashlight.last_brightness";

ndk::ScopedAStatus Flashlight::getCurrentBrightness(int32_t* _aidl_return) {
    std::string value;
    int intvalue;

    ReadFileToString(FLASH_NODE, &value);
    intvalue = stoi_safe(value);
    switch (intvalue) {
	    case 0:
		    *_aidl_return = 0;
		    break;
	    case 1:
		    *_aidl_return = GetIntProperty(FLASH_BRIGHTNESS_PROP, level_saved, 0, 5);
		    break;
	    case 1001:
		    *_aidl_return = 1;
		    break;
	    case 1002:
		    *_aidl_return = 2;
		    break;
	    case 1003:
	    case 1004:
		    *_aidl_return = 3;
		    break;
	    case 1005:
	    case 1006:
		    *_aidl_return = 4;
		    break;
	    case 1007 ... 1009:
		    *_aidl_return = 5;
		    break;
	    default:
		    // Some kernels (e.g. sm5705) read back the maximum torch current
		    // offset instead of the last written value; report the saved level.
		    *_aidl_return = GetIntProperty(FLASH_BRIGHTNESS_PROP, level_saved, 0, 5);
		    break;
    }
    return ndk::ScopedAStatus::ok();
}

ndk::ScopedAStatus Flashlight::setBrightness(int32_t level) {
    if (level > 5 || level < 1)
       return ndk::ScopedAStatus::fromExceptionCode(EX_UNSUPPORTED_OPERATION);
    int writeval = 0;
    switch (level) {
	case 1:
		writeval = 1001;
		break;
	case 2:
		writeval = 1002;
		break;
	case 3:
		writeval = 1004;
		break;
	case 4:
		writeval = 1006;
		break;
	case 5:
		writeval = 1009;
		break;
	default:
		break;
    }
    WriteStringToFile(std::to_string(writeval), FLASH_NODE);
    SetProperty(FLASH_BRIGHTNESS_PROP, std::to_string(level));
    level_saved = level;
    return ndk::ScopedAStatus::ok();
}

ndk::ScopedAStatus Flashlight::enableFlash(bool enable) {
    int32_t rc = 0;
    std::string value;
    ReadFileToString(FLASH_NODE, &value);
    int nodevalue = stoi_safe(value);
    bool reportsState = nodevalue == 0 || nodevalue == 1 || (nodevalue > 1000 && nodevalue < 1010);
    auto ret = getCurrentBrightness(&rc);
    if (reportsState && ret.isOk()) {
	if (!!rc == enable)
	    return ndk::ScopedAStatus::fromExceptionCode(EX_ILLEGAL_STATE);
    }
    WriteStringToFile(std::to_string(static_cast<int>(enable)), FLASH_NODE);
    return ndk::ScopedAStatus::ok();
}

} // namespace flashlight
} // namespace camera
} // namespace hardware
} // namespace samsung_ext
} // namespace vendor
} // namespace aidl
