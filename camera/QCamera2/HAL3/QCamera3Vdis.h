/*
 * Copyright (C) 2026 The LineageOS Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#ifndef __QCAMERA3VDIS_H__
#define __QCAMERA3VDIS_H__

#include <stdint.h>

#include <utils/Errors.h>
#include <utils/Timers.h>

namespace qcamera {

/*
 * Samsung software video stabilization (VDIS 3.0) for the main rear and the
 * front camera.
 *
 * The stock camera.sdm660 runs it through the "VDISplugin" uniplugin
 * (libuniplugin -> libvdis_interface -> libvdis_core): it reads the gyro
 * itself, estimates motion on the GPU and warps every input frame, which is
 * 6/5 of the output size, into an output frame. Outputs come out in the order
 * the output buffers were handed over, VDIS_DELAY frames after their input.
 *
 * The plugin's header was never released; the structs follow the stock HAL
 * and vdisPlugin::{set,get,process}_plugin, the call sequence Samsung's
 * SecCameraSWVdis_3_0.cpp.
 */
class QCamera3Vdis {
public:
    struct Buffer {
        void *y;
        uint32_t uvOffset;
        int fd;
    };

    /* frames an input stays in the plugin before its output is ready */
    static constexpr uint32_t VDIS_DELAY = 8;

    static bool isAvailable(uint32_t cameraId);
    /* input size for an output size, or false if the size is not supported */
    static bool getInputSize(int32_t outW, int32_t outH, int32_t &inW, int32_t &inH);

    QCamera3Vdis();
    ~QCamera3Vdis();

    android::status_t init(uint32_t cameraId, int32_t inW, int32_t inH, int32_t inStride,
            int32_t outW, int32_t outH, int32_t outStride, int32_t fps);
    void deinit();
    bool isActive() const { return mHandle != nullptr; }

    /* hands over the output buffer for the next input; slot < 256 */
    void setOutput(uint32_t slot, const Buffer &out);
    /* feeds input slot (< 256); timestamp in CLOCK_BOOTTIME like the gyro */
    void process(uint32_t slot, const Buffer &in, nsecs_t timestamp);
    /* the output finished by the last process(), if any */
    bool getOutput(uint32_t &outSlot, uint32_t &inSlot, bool &valid);

private:
    void *mHandle;
    int32_t mCamType;
    int32_t mInW, mInH, mInStride, mOutW, mOutH, mOutStride;
};

}; // namespace qcamera

#endif /* __QCAMERA3VDIS_H__ */
