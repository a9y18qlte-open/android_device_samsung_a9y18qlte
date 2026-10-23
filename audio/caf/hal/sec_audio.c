/*
 * Copyright (C) 2026 The LineageOS Project
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#define LOG_TAG "audio_hw_sec"
/*#define LOG_NDEBUG 0*/

#include <errno.h>
#include <stdlib.h>
#include <string.h>

#include <cutils/properties.h>
#include <cutils/str_parms.h>
#include <log/log.h>
#include <tinyalsa/asoundlib.h>

#include "audio_hw.h"
#include "audio_extn.h"
#include "platform.h"
#include "platform_api.h"
#include "sec_audio.h"
#include "voice.h"

#define OUT_HEADSETS (AUDIO_DEVICE_OUT_WIRED_HEADSET | AUDIO_DEVICE_OUT_WIRED_HEADPHONE)
#define OUT_USB (AUDIO_DEVICE_OUT_USB_DEVICE | AUDIO_DEVICE_OUT_USB_HEADSET)
#define OUT_SCO (AUDIO_DEVICE_OUT_BLUETOOTH_SCO | AUDIO_DEVICE_OUT_BLUETOOTH_SCO_HEADSET | \
                 AUDIO_DEVICE_OUT_BLUETOOTH_SCO_CARKIT)
#define OUT_A2DP (AUDIO_DEVICE_OUT_BLUETOOTH_A2DP | AUDIO_DEVICE_OUT_BLUETOOTH_A2DP_HEADPHONES | \
                  AUDIO_DEVICE_OUT_BLUETOOTH_A2DP_SPEAKER)
#define OUT_DOCKS (AUDIO_DEVICE_OUT_ANLG_DOCK_HEADSET | AUDIO_DEVICE_OUT_DGTL_DOCK_HEADSET)
#define OUT_SPEAKERS (AUDIO_DEVICE_OUT_SPEAKER | AUDIO_DEVICE_OUT_SPEAKER_SAFE)
/* Wired outputs a TTY device can be on */
#define OUT_TTY (OUT_HEADSETS | OUT_USB)

#define IN_USB (AUDIO_DEVICE_IN_USB_ACCESSORY | AUDIO_DEVICE_IN_USB_DEVICE | \
                AUDIO_DEVICE_IN_USB_HEADSET)

/* Samsung tunes EVS calls separately; every call device has an _EVS twin */
#define EVS(adev, dev) ((adev)->sec.call_band == SEC_CALL_BAND_SWB ? dev##_EVS : dev)

static bool is_in_call(struct audio_device *adev)
{
    return ((adev->mode == AUDIO_MODE_IN_CALL || adev->sec.real_call) &&
            voice_is_in_call(adev)) || adev->sec.voip_type == SEC_VOIP_CSVT;
}

static bool usb_capture(void)
{
    return audio_extn_usb_is_capture_supported();
}

static snd_device_t get_output_for_call(struct audio_device *adev, audio_devices_t devices)
{
    bool nrec = adev->bluetooth_nrec;
    bool wb = adev->bt_wb_speech_enabled;
    int tty_mode = adev->voice.tty_mode;

    if (tty_mode != TTY_MODE_OFF && (devices & (OUT_TTY | AUDIO_DEVICE_OUT_SPEAKER))) {
        switch (tty_mode) {
        case TTY_MODE_HCO:
            if (devices & OUT_TTY)
                return adev->sec.extra_volume ? SND_DEVICE_OUT_CALL_TTY_HANDSET_EX :
                                                SND_DEVICE_OUT_CALL_TTY_HANDSET;
            break;
        case TTY_MODE_VCO:
            if (devices & OUT_USB)
                return SND_DEVICE_OUT_CALL_TTY_USB_HEADSET;
            if (devices & (OUT_HEADSETS | AUDIO_DEVICE_OUT_SPEAKER))
                return SND_DEVICE_OUT_CALL_TTY_HEADSET;
            break;
        case TTY_MODE_FULL:
            if (devices & OUT_USB)
                return SND_DEVICE_OUT_CALL_TTY_USB_HEADSET;
            if (devices & OUT_HEADSETS)
                return SND_DEVICE_OUT_CALL_TTY_FULL;
            break;
        }
    }

    if (devices & OUT_HEADSETS)
        return EVS(adev, SND_DEVICE_OUT_CALL_HEADSET);
    if (devices & OUT_USB)
        return usb_capture() ? EVS(adev, SND_DEVICE_OUT_CALL_USB_HEADSET) :
                               EVS(adev, SND_DEVICE_OUT_CALL_USB_HEADPHONE);
    if (devices & OUT_SCO) {
        if (wb)
            return nrec ? EVS(adev, SND_DEVICE_OUT_CALL_BT_HEADSET_16K) :
                          EVS(adev, SND_DEVICE_OUT_CALL_BT_HEADSET_NREC_16K);
        return nrec ? EVS(adev, SND_DEVICE_OUT_CALL_BT_HEADSET) :
                      EVS(adev, SND_DEVICE_OUT_CALL_BT_HEADSET_NREC);
    }
    if (devices & AUDIO_DEVICE_OUT_SPEAKER)
        return adev->sec.extra_volume ? EVS(adev, SND_DEVICE_OUT_CALL_SPEAKER_EX) :
                                        EVS(adev, SND_DEVICE_OUT_CALL_SPEAKER);
    if (devices & AUDIO_DEVICE_OUT_EARPIECE) {
        if (adev->voice.hac)
            return EVS(adev, SND_DEVICE_OUT_CALL_HANDSET_HAC);
        return adev->sec.extra_volume ? EVS(adev, SND_DEVICE_OUT_CALL_HANDSET_EX) :
                                        EVS(adev, SND_DEVICE_OUT_CALL_HANDSET);
    }
    if (devices & AUDIO_DEVICE_OUT_HEARING_AID)
        return SND_DEVICE_OUT_VOICE_HEARING_AID;
    return SND_DEVICE_NONE;
}

static snd_device_t get_output_for_wifi_call(struct audio_device *adev, audio_devices_t devices)
{
    bool nrec = adev->bluetooth_nrec;
    bool wb = adev->bt_wb_speech_enabled;
    int tty_mode = adev->voice.tty_mode;

    if (tty_mode != TTY_MODE_OFF && (devices & (OUT_TTY | AUDIO_DEVICE_OUT_SPEAKER))) {
        switch (tty_mode) {
        case TTY_MODE_HCO:
            if (devices & OUT_TTY)
                return adev->sec.extra_volume ? SND_DEVICE_OUT_WIFI_CALL_TTY_HANDSET_EX :
                                                SND_DEVICE_OUT_WIFI_CALL_TTY_HANDSET;
            break;
        case TTY_MODE_VCO:
            if (devices & OUT_USB)
                return SND_DEVICE_OUT_WIFI_CALL_TTY_USB_HEADSET;
            if (devices & (OUT_HEADSETS | AUDIO_DEVICE_OUT_SPEAKER))
                return SND_DEVICE_OUT_WIFI_CALL_TTY_HEADSET;
            break;
        case TTY_MODE_FULL:
            if (devices & OUT_USB)
                return SND_DEVICE_OUT_WIFI_CALL_TTY_USB_HEADSET;
            if (devices & OUT_HEADSETS)
                return SND_DEVICE_OUT_WIFI_CALL_TTY_FULL;
            break;
        }
    }

    if (devices & OUT_HEADSETS)
        return EVS(adev, SND_DEVICE_OUT_WIFI_CALL_HEADSET);
    if (devices & OUT_USB)
        return usb_capture() ? EVS(adev, SND_DEVICE_OUT_WIFI_CALL_USB_HEADSET) :
                               EVS(adev, SND_DEVICE_OUT_WIFI_CALL_USB_HEADPHONE);
    if (devices & OUT_SCO) {
        if (wb)
            return nrec ? EVS(adev, SND_DEVICE_OUT_WIFI_CALL_BT_HEADSET_16K) :
                          EVS(adev, SND_DEVICE_OUT_WIFI_CALL_BT_HEADSET_NREC_16K);
        return nrec ? EVS(adev, SND_DEVICE_OUT_WIFI_CALL_BT_HEADSET) :
                      EVS(adev, SND_DEVICE_OUT_WIFI_CALL_BT_HEADSET_NREC);
    }
    if (devices & AUDIO_DEVICE_OUT_SPEAKER)
        return adev->sec.extra_volume ? EVS(adev, SND_DEVICE_OUT_WIFI_CALL_SPEAKER_EX) :
                                        EVS(adev, SND_DEVICE_OUT_WIFI_CALL_SPEAKER);
    if (devices & AUDIO_DEVICE_OUT_EARPIECE) {
        if (adev->voice.hac)
            return EVS(adev, SND_DEVICE_OUT_WIFI_CALL_HANDSET_HAC);
        return adev->sec.extra_volume ? EVS(adev, SND_DEVICE_OUT_WIFI_CALL_HANDSET_EX) :
                                        EVS(adev, SND_DEVICE_OUT_WIFI_CALL_HANDSET);
    }
    return SND_DEVICE_NONE;
}

static snd_device_t get_output_for_voip(struct audio_device *adev, audio_devices_t devices)
{
    bool nrec = adev->bluetooth_nrec;
    bool wb = adev->bt_wb_speech_enabled;

    switch (adev->sec.voip_type) {
    case SEC_VOIP_WIFI_CALL:
        return get_output_for_wifi_call(adev, devices);
    case SEC_VOIP_COMM:
        if (devices & OUT_HEADSETS)
            return SND_DEVICE_OUT_VOIP_COMM_HEADSET;
        if (devices & OUT_USB)
            return usb_capture() ? SND_DEVICE_OUT_VOIP_COMM_USB_HEADSET :
                                   SND_DEVICE_OUT_VOIP_COMM_USB_HEADPHONE;
        if (devices & OUT_SCO) {
            if (wb)
                return nrec ? SND_DEVICE_OUT_VOIP_COMM_BT_HEADSET_16K :
                              SND_DEVICE_OUT_VOIP_COMM_BT_HEADSET_NREC_16K;
            return nrec ? SND_DEVICE_OUT_VOIP_COMM_BT_HEADSET :
                          SND_DEVICE_OUT_VOIP_COMM_BT_HEADSET_NREC;
        }
        if (devices & AUDIO_DEVICE_OUT_SPEAKER)
            return SND_DEVICE_OUT_VOIP_COMM_SPEAKER;
        if (devices & AUDIO_DEVICE_OUT_EARPIECE)
            return adev->voice.hac ? SND_DEVICE_OUT_VOIP_COMM_HANDSET_HAC :
                                     SND_DEVICE_OUT_VOIP_COMM_HANDSET;
        break;
    case SEC_VOIP_FMC:
        if (devices & OUT_HEADSETS)
            return SND_DEVICE_OUT_VOIP_FMC_HEADSET;
        if (devices & OUT_USB)
            return usb_capture() ? SND_DEVICE_OUT_VOIP_FMC_USB_HEADSET :
                                   SND_DEVICE_OUT_VOIP_FMC_USB_HEADPHONE;
        if (devices & OUT_SCO) {
            if (wb)
                return nrec ? SND_DEVICE_OUT_VOIP_FMC_BT_HEADSET_16K :
                              SND_DEVICE_OUT_VOIP_FMC_BT_HEADSET_NREC_16K;
            return nrec ? SND_DEVICE_OUT_VOIP_FMC_BT_HEADSET :
                          SND_DEVICE_OUT_VOIP_FMC_BT_HEADSET_NREC;
        }
        if (devices & AUDIO_DEVICE_OUT_SPEAKER)
            return SND_DEVICE_OUT_VOIP_FMC_SPEAKER;
        if (devices & AUDIO_DEVICE_OUT_EARPIECE)
            return adev->voice.hac ? SND_DEVICE_OUT_VOIP_FMC_HANDSET_HAC :
                                     SND_DEVICE_OUT_VOIP_FMC_HANDSET;
        break;
    default:
        break;
    }
    return SND_DEVICE_NONE;
}

int sec_audio_get_output_snd_device(struct audio_device *adev, struct listnode *out_devices)
{
    audio_devices_t devices = get_device_types(out_devices);
    snd_device_t snd_device = SND_DEVICE_NONE;
    int count = list_length(out_devices);

    if (devices == AUDIO_DEVICE_NONE)
        return SND_DEVICE_NONE;

    if (is_in_call(adev)) {
        snd_device = get_output_for_call(adev, devices);
        if (snd_device != SND_DEVICE_NONE)
            goto exit;
        ALOGW("%s: no call device for %#x", __func__, devices);
    }

    if (adev->sec.voip_type != SEC_VOIP_NONE && adev->sec.voip_type != SEC_VOIP_CSVT) {
        snd_device = get_output_for_voip(adev, devices);
        if (snd_device != SND_DEVICE_NONE)
            goto exit;
    }

    if (count == 2) {
        if ((devices & AUDIO_DEVICE_OUT_SPEAKER) && (devices & OUT_HEADSETS))
            snd_device = SND_DEVICE_OUT_SPEAKER_AND_HEADPHONES;
        else if ((devices & AUDIO_DEVICE_OUT_SPEAKER) && (devices & OUT_DOCKS))
            snd_device = SND_DEVICE_OUT_SPEAKER_AND_DOCK;
        else if ((devices & AUDIO_DEVICE_OUT_SPEAKER) && (devices & OUT_USB))
            snd_device = SND_DEVICE_OUT_SPEAKER_AND_USB_HEADSET;
        else if ((devices & AUDIO_DEVICE_OUT_SPEAKER) && (devices & OUT_A2DP))
            snd_device = SND_DEVICE_OUT_SPEAKER_AND_BT_A2DP;
        else if ((devices & OUT_SPEAKERS) && (devices & OUT_SCO))
            snd_device = adev->bt_wb_speech_enabled ? SND_DEVICE_OUT_SPEAKER_AND_BT_SCO_WB :
                                                      SND_DEVICE_OUT_SPEAKER_AND_BT_SCO;
        /* HDMI and display port combos are CAF's */
        goto exit;
    }

    if (count != 1)
        goto exit;

    if (devices & (OUT_HEADSETS | AUDIO_DEVICE_OUT_LINE))
        snd_device = SND_DEVICE_OUT_HEADPHONES;
    else if (devices & OUT_SPEAKERS)
        snd_device = SND_DEVICE_OUT_SPEAKER;
    else if (devices & OUT_SCO) {
        if (adev->bt_wb_speech_enabled)
            snd_device = adev->bluetooth_nrec ? SND_DEVICE_OUT_BT_HEADSET_16K :
                                                SND_DEVICE_OUT_BT_HEADSET_NREC_16K;
        else
            snd_device = adev->bluetooth_nrec ? SND_DEVICE_OUT_BT_HEADSET :
                                                SND_DEVICE_OUT_BT_HEADSET_NREC;
    } else if (devices & OUT_A2DP)
        snd_device = SND_DEVICE_OUT_BT_A2DP;
    else if (devices & OUT_DOCKS)
        snd_device = SND_DEVICE_OUT_DOCK;
    else if (devices & OUT_USB)
        snd_device = SND_DEVICE_OUT_USB_HEADSET;
    else if (devices & AUDIO_DEVICE_OUT_EARPIECE)
        snd_device = SND_DEVICE_OUT_HANDSET;
    /* HDMI, display port, proxy and telephony TX are CAF's */

exit:
    ALOGV("%s: devices %#x -> %s", __func__, devices,
          snd_device != SND_DEVICE_NONE ? platform_get_snd_device_name(snd_device) : "CAF");
    return snd_device;
}

static snd_device_t get_input_for_call(struct audio_device *adev, audio_devices_t out)
{
    bool nrec = adev->bluetooth_nrec;
    bool wb = adev->bt_wb_speech_enabled;
    int tty_mode = adev->voice.tty_mode;

    /* A call never captures from the speaker mic while a headset is in */
    if ((out & AUDIO_DEVICE_OUT_SPEAKER) && (out & OUT_HEADSETS))
        out &= ~AUDIO_DEVICE_OUT_SPEAKER;

    if (tty_mode != TTY_MODE_OFF && (out & (OUT_TTY | AUDIO_DEVICE_OUT_SPEAKER))) {
        switch (tty_mode) {
        case TTY_MODE_HCO:
            if (out & OUT_TTY)
                return (out & OUT_USB) ? SND_DEVICE_IN_CALL_TTY_USB_HEADSET_MIC :
                                         SND_DEVICE_IN_CALL_TTY_HEADSET_MIC;
            break;
        case TTY_MODE_VCO:
            if (out & OUT_USB)
                return SND_DEVICE_IN_CALL_TTY_USB_VCO_MIC;
            if (out & (OUT_HEADSETS | AUDIO_DEVICE_OUT_SPEAKER))
                return SND_DEVICE_IN_CALL_TTY_VCO_MIC;
            break;
        case TTY_MODE_FULL:
            if (out & OUT_TTY)
                return (out & OUT_USB) ? SND_DEVICE_IN_CALL_TTY_USB_FULL_MIC :
                                         SND_DEVICE_IN_CALL_TTY_FULL_MIC;
            break;
        }
    }

    if (out & AUDIO_DEVICE_OUT_EARPIECE)
        return EVS(adev, SND_DEVICE_IN_CALL_HANDSET_MIC);
    if (out & AUDIO_DEVICE_OUT_WIRED_HEADPHONE)
        return EVS(adev, SND_DEVICE_IN_CALL_HEADPHONE_MIC);
    if (out & AUDIO_DEVICE_OUT_WIRED_HEADSET)
        return EVS(adev, SND_DEVICE_IN_CALL_HEADSET_MIC);
    if (out & OUT_USB)
        return usb_capture() ? EVS(adev, SND_DEVICE_IN_CALL_USB_HEADSET_MIC) :
                               EVS(adev, SND_DEVICE_IN_CALL_USB_HEADPHONE_MIC);
    if (out & OUT_SCO) {
        if (wb)
            return nrec ? EVS(adev, SND_DEVICE_IN_CALL_BT_MIC_16K) :
                          EVS(adev, SND_DEVICE_IN_CALL_BT_MIC_NREC_16K);
        return nrec ? EVS(adev, SND_DEVICE_IN_CALL_BT_MIC) :
                      EVS(adev, SND_DEVICE_IN_CALL_BT_MIC_NREC);
    }
    if (out & AUDIO_DEVICE_OUT_SPEAKER)
        return EVS(adev, SND_DEVICE_IN_CALL_SPEAKER_MIC);
    if (out & AUDIO_DEVICE_OUT_HEARING_AID)
        return SND_DEVICE_IN_VOICE_HEARING_AID;
    return SND_DEVICE_NONE;
}

static snd_device_t get_input_for_wifi_call(struct audio_device *adev, audio_devices_t out)
{
    bool nrec = adev->bluetooth_nrec;
    bool wb = adev->bt_wb_speech_enabled;
    int tty_mode = adev->voice.tty_mode;

    if (tty_mode != TTY_MODE_OFF && (out & (OUT_TTY | AUDIO_DEVICE_OUT_SPEAKER))) {
        switch (tty_mode) {
        case TTY_MODE_HCO:
            if (out & OUT_TTY)
                return (out & OUT_USB) ? SND_DEVICE_IN_WIFI_CALL_TTY_USB_HEADSET_MIC :
                                         SND_DEVICE_IN_WIFI_CALL_TTY_HEADSET_MIC;
            break;
        case TTY_MODE_VCO:
            if (out & OUT_USB)
                return SND_DEVICE_IN_WIFI_CALL_TTY_USB_VCO_MIC;
            if (out & (OUT_HEADSETS | AUDIO_DEVICE_OUT_SPEAKER))
                return SND_DEVICE_IN_WIFI_CALL_TTY_VCO_MIC;
            break;
        case TTY_MODE_FULL:
            if (out & OUT_TTY)
                return (out & OUT_USB) ? SND_DEVICE_IN_WIFI_CALL_TTY_USB_FULL_MIC :
                                         SND_DEVICE_IN_WIFI_CALL_TTY_FULL_MIC;
            break;
        }
    }

    if (out & AUDIO_DEVICE_OUT_EARPIECE)
        return EVS(adev, SND_DEVICE_IN_WIFI_CALL_HANDSET_MIC);
    if (out & AUDIO_DEVICE_OUT_WIRED_HEADSET)
        return EVS(adev, SND_DEVICE_IN_WIFI_CALL_HEADSET_MIC);
    if (out & AUDIO_DEVICE_OUT_WIRED_HEADPHONE)
        return EVS(adev, SND_DEVICE_IN_WIFI_CALL_HEADPHONE_MIC);
    if (out & OUT_USB)
        return usb_capture() ? EVS(adev, SND_DEVICE_IN_WIFI_CALL_USB_HEADSET_MIC) :
                               EVS(adev, SND_DEVICE_IN_WIFI_CALL_USB_HEADPHONE_MIC);
    if (out & OUT_SCO) {
        if (wb)
            return nrec ? EVS(adev, SND_DEVICE_IN_WIFI_CALL_BT_MIC_16K) :
                          EVS(adev, SND_DEVICE_IN_WIFI_CALL_BT_MIC_NREC_16K);
        return nrec ? EVS(adev, SND_DEVICE_IN_WIFI_CALL_BT_MIC) :
                      EVS(adev, SND_DEVICE_IN_WIFI_CALL_BT_MIC_NREC);
    }
    if (out & AUDIO_DEVICE_OUT_SPEAKER)
        return EVS(adev, SND_DEVICE_IN_WIFI_CALL_SPEAKER_MIC);
    return SND_DEVICE_NONE;
}

static snd_device_t get_input_for_voip(struct audio_device *adev, audio_devices_t out)
{
    bool nrec = adev->bluetooth_nrec;
    bool wb = adev->bt_wb_speech_enabled;

    switch (adev->sec.voip_type) {
    case SEC_VOIP_WIFI_CALL:
        return get_input_for_wifi_call(adev, out);
    case SEC_VOIP_COMM:
        if (out & AUDIO_DEVICE_OUT_EARPIECE)
            return SND_DEVICE_IN_VOIP_COMM_HANDSET_MIC;
        if (out & AUDIO_DEVICE_OUT_WIRED_HEADSET)
            return SND_DEVICE_IN_VOIP_COMM_HEADSET_MIC;
        if (out & AUDIO_DEVICE_OUT_WIRED_HEADPHONE)
            return SND_DEVICE_IN_VOIP_COMM_HEADPHONE_MIC;
        if (out & OUT_USB)
            return usb_capture() ? SND_DEVICE_IN_VOIP_COMM_USB_HEADSET_MIC :
                                   SND_DEVICE_IN_VOIP_COMM_USB_HEADPHONE_MIC;
        if (out & OUT_SCO) {
            if (wb)
                return nrec ? SND_DEVICE_IN_VOIP_COMM_BT_MIC_16K :
                              SND_DEVICE_IN_VOIP_COMM_BT_MIC_NREC_16K;
            return nrec ? SND_DEVICE_IN_VOIP_COMM_BT_MIC : SND_DEVICE_IN_VOIP_COMM_BT_MIC_NREC;
        }
        if (out & AUDIO_DEVICE_OUT_SPEAKER)
            return SND_DEVICE_IN_VOIP_COMM_SPEAKER_MIC;
        break;
    case SEC_VOIP_FMC:
        if (out & AUDIO_DEVICE_OUT_EARPIECE)
            return SND_DEVICE_IN_VOIP_FMC_HANDSET_MIC;
        if (out & AUDIO_DEVICE_OUT_WIRED_HEADSET)
            return SND_DEVICE_IN_VOIP_FMC_HEADSET_MIC;
        if (out & AUDIO_DEVICE_OUT_WIRED_HEADPHONE)
            return SND_DEVICE_IN_VOIP_FMC_HEADPHONE_MIC;
        if (out & OUT_USB)
            return usb_capture() ? SND_DEVICE_IN_VOIP_FMC_USB_HEADSET_MIC :
                                   SND_DEVICE_IN_VOIP_FMC_USB_HEADPHONE_MIC;
        if (out & OUT_SCO) {
            if (wb)
                return nrec ? SND_DEVICE_IN_VOIP_FMC_BT_MIC_16K :
                              SND_DEVICE_IN_VOIP_FMC_BT_MIC_NREC_16K;
            return nrec ? SND_DEVICE_IN_VOIP_FMC_BT_MIC : SND_DEVICE_IN_VOIP_FMC_BT_MIC_NREC;
        }
        if (out & AUDIO_DEVICE_OUT_SPEAKER)
            return SND_DEVICE_IN_VOIP_FMC_SPEAKER_MIC;
        break;
    default:
        break;
    }
    return SND_DEVICE_NONE;
}

static bool is_single_usb_input(audio_devices_t in)
{
    return (in & IN_USB) && __builtin_popcount(in & ~AUDIO_DEVICE_BIT_IN) == 1;
}

static snd_device_t get_input_by_source(struct audio_device *adev, int source,
                                        audio_devices_t in)
{
    bool nrec = adev->bluetooth_nrec;
    bool wb = adev->bt_wb_speech_enabled;

    switch (source) {
    case AUDIO_SOURCE_CAMCORDER:
        if (in & (AUDIO_DEVICE_IN_BUILTIN_MIC | AUDIO_DEVICE_IN_BACK_MIC) & ~AUDIO_DEVICE_BIT_IN)
            return SND_DEVICE_IN_CAMCORDER_STEREO_MIC;
        break;
    case AUDIO_SOURCE_VOICE_RECOGNITION:
        if ((in & AUDIO_DEVICE_IN_BUILTIN_MIC) == AUDIO_DEVICE_IN_BUILTIN_MIC)
            return SND_DEVICE_IN_VR_MAIN_MIC;
        if ((in & AUDIO_DEVICE_IN_BACK_MIC) == AUDIO_DEVICE_IN_BACK_MIC)
            return SND_DEVICE_IN_VR_SUB_MIC;
        if ((in & AUDIO_DEVICE_IN_WIRED_HEADSET) == AUDIO_DEVICE_IN_WIRED_HEADSET)
            return SND_DEVICE_IN_VR_HEADSET_MIC;
        if (is_single_usb_input(in))
            return SND_DEVICE_IN_VR_USB_HEADSET_MIC;
        if ((in & AUDIO_DEVICE_IN_BLUETOOTH_SCO_HEADSET) == AUDIO_DEVICE_IN_BLUETOOTH_SCO_HEADSET) {
            if (wb)
                return nrec ? SND_DEVICE_IN_VR_BT_MIC_16K : SND_DEVICE_IN_VR_BT_MIC_NREC_16K;
            return nrec ? SND_DEVICE_IN_VR_BT_MIC : SND_DEVICE_IN_VR_BT_MIC_NREC;
        }
        break;
    default:
        break;
    }
    return SND_DEVICE_NONE;
}

/* Recording with nothing but the output device to go by */
static snd_device_t get_input_by_output(struct audio_device *adev, audio_devices_t out)
{
    if (out & AUDIO_DEVICE_OUT_EARPIECE)
        return SND_DEVICE_IN_REC_MAIN_MIC;
    if (out & AUDIO_DEVICE_OUT_WIRED_HEADSET)
        return SND_DEVICE_IN_REC_HEADSET_MIC;
    if (out & AUDIO_DEVICE_OUT_BLUETOOTH_SCO_HEADSET)
        return adev->bluetooth_nrec ? SND_DEVICE_IN_REC_BT_MIC : SND_DEVICE_IN_REC_BT_MIC_NREC;
    return SND_DEVICE_IN_REC_MAIN_MIC;
}

int sec_audio_get_input_snd_device(struct audio_device *adev, struct stream_in *in,
                                   struct listnode *out_devices)
{
    audio_devices_t out = get_device_types(out_devices);
    audio_devices_t in_devices = AUDIO_DEVICE_NONE;
    int source = AUDIO_SOURCE_DEFAULT;
    snd_device_t snd_device = SND_DEVICE_NONE;

    if (in == NULL)
        in = adev_get_active_input(adev);
    if (in != NULL) {
        source = in->source;
        in_devices = get_device_types(&in->device_list);
    }

    if (is_in_call(adev)) {
        if (out == AUDIO_DEVICE_NONE) {
            ALOGE("%s: no output device set for the call", __func__);
            return SND_DEVICE_NONE;
        }
        snd_device = get_input_for_call(adev, out);
        if (snd_device != SND_DEVICE_NONE)
            goto exit;
    }

    if (adev->sec.voip_type != SEC_VOIP_NONE && adev->sec.voip_type != SEC_VOIP_CSVT) {
        snd_device = get_input_for_voip(adev, out);
        if (snd_device != SND_DEVICE_NONE)
            goto exit;
    }

    snd_device = get_input_by_source(adev, source, in_devices);
    if (snd_device != SND_DEVICE_NONE)
        goto exit;

    in_devices &= ~AUDIO_DEVICE_BIT_IN;
    if (in_devices == AUDIO_DEVICE_NONE ||
        (in_devices & ((AUDIO_DEVICE_IN_COMMUNICATION | AUDIO_DEVICE_IN_VOICE_CALL) &
                       ~AUDIO_DEVICE_BIT_IN))) {
        snd_device = get_input_by_output(adev, out);
    } else if ((in_devices & (AUDIO_DEVICE_IN_BUILTIN_MIC | AUDIO_DEVICE_IN_BACK_MIC) &
                ~AUDIO_DEVICE_BIT_IN) ==
               ((AUDIO_DEVICE_IN_BUILTIN_MIC | AUDIO_DEVICE_IN_BACK_MIC) & ~AUDIO_DEVICE_BIT_IN)) {
        snd_device = SND_DEVICE_IN_REC_STEREO_MIC;
    } else if (in_devices & AUDIO_DEVICE_IN_BUILTIN_MIC & ~AUDIO_DEVICE_BIT_IN) {
        snd_device = SND_DEVICE_IN_REC_MAIN_MIC;
    } else if (in_devices & AUDIO_DEVICE_IN_BACK_MIC & ~AUDIO_DEVICE_BIT_IN) {
        snd_device = SND_DEVICE_IN_REC_SUB_MIC;
    } else if (in_devices & AUDIO_DEVICE_IN_WIRED_HEADSET & ~AUDIO_DEVICE_BIT_IN) {
        snd_device = SND_DEVICE_IN_REC_HEADSET_MIC;
    } else if (in_devices & AUDIO_DEVICE_IN_BLUETOOTH_SCO_HEADSET & ~AUDIO_DEVICE_BIT_IN) {
        if (adev->bt_wb_speech_enabled)
            snd_device = adev->bluetooth_nrec ? SND_DEVICE_IN_REC_BT_MIC_NREC_16K :
                                                SND_DEVICE_IN_REC_BT_MIC_16K;
        else
            snd_device = adev->bluetooth_nrec ? SND_DEVICE_IN_REC_BT_MIC_NREC :
                                                SND_DEVICE_IN_REC_BT_MIC;
    } else if (audio_extn_usb_connected(NULL) && is_single_usb_input(in_devices | AUDIO_DEVICE_BIT_IN)) {
        snd_device = SND_DEVICE_IN_REC_USB_HEADSET_MIC;
    } else {
        ALOGW("%s: unknown input device(s) %#x, using the main mic", __func__, in_devices);
        snd_device = SND_DEVICE_IN_REC_MAIN_MIC;
    }

exit:
    ALOGV("%s: source %d in %#x out %#x -> %s", __func__, source, in_devices, out,
          platform_get_snd_device_name(snd_device));
    return snd_device;
}

/* VI (voltage/current) feedback of the speaker amplifier */

static struct pcm_config vi_feedback_config = {
    .channels = 2,
    .rate = 48000,
    .period_size = 256,
    .period_count = 4,
    .format = PCM_FORMAT_S16_LE,
};

static bool is_speaker_snd_device(snd_device_t snd_device)
{
    const char *name = platform_get_snd_device_name(snd_device);

    return strstr(name, "speaker") != NULL || strstr(name, "vi-feedback") != NULL;
}

static bool playback_on_speaker(struct audio_device *adev)
{
    struct listnode *node;

    list_for_each(node, &adev->usecase_list) {
        struct audio_usecase *usecase = node_to_item(node, struct audio_usecase, list);

        if (usecase->type != PCM_PLAYBACK && usecase->type != VOICE_CALL &&
            usecase->type != VOIP_CALL)
            continue;
        if (usecase->stream.out != NULL &&
            compare_device_type(&usecase->stream.out->device_list, AUDIO_DEVICE_OUT_SPEAKER))
            return true;
    }
    return false;
}

static void stop_vi_feedback(struct audio_device *adev)
{
    struct audio_usecase *usecase;

    if (adev->sec.vi_feedback_pcm != NULL) {
        pcm_close(adev->sec.vi_feedback_pcm);
        adev->sec.vi_feedback_pcm = NULL;
    }

    usecase = get_usecase_from_list(adev, USECASE_AUDIO_DSM_FEEDBACK);
    if (usecase == NULL)
        return;
    disable_audio_route(adev, usecase);
    disable_snd_device(adev, SND_DEVICE_IN_CAPTURE_VI_FEEDBACK);
    list_remove(&usecase->list);
    free(usecase);
    ALOGD("%s: VI feedback stopped", __func__);
}

static void start_vi_feedback(struct audio_device *adev)
{
    struct audio_usecase *usecase;
    int pcm_id;

    if (get_usecase_from_list(adev, USECASE_AUDIO_DSM_FEEDBACK) != NULL)
        return;

    usecase = calloc(1, sizeof(*usecase));
    if (usecase == NULL)
        return;
    usecase->id = USECASE_AUDIO_DSM_FEEDBACK;
    usecase->type = PCM_CAPTURE;
    usecase->in_snd_device = SND_DEVICE_IN_CAPTURE_VI_FEEDBACK;
    list_init(&usecase->device_list);
    list_add_tail(&adev->usecase_list, &usecase->list);

    enable_snd_device(adev, SND_DEVICE_IN_CAPTURE_VI_FEEDBACK);
    enable_audio_route(adev, usecase);

    pcm_id = platform_get_pcm_device_id(usecase->id, PCM_CAPTURE);
    if (pcm_id < 0) {
        ALOGE("%s: no PCM device for the VI feedback", __func__);
        goto error;
    }
    adev->sec.vi_feedback_pcm = pcm_open(adev->snd_card, pcm_id, PCM_IN, &vi_feedback_config);
    if (adev->sec.vi_feedback_pcm == NULL || !pcm_is_ready(adev->sec.vi_feedback_pcm)) {
        ALOGE("%s: cannot open PCM %d: %s", __func__, pcm_id,
              adev->sec.vi_feedback_pcm ? pcm_get_error(adev->sec.vi_feedback_pcm) : "");
        goto error;
    }
    if (pcm_start(adev->sec.vi_feedback_pcm) < 0) {
        ALOGE("%s: cannot start PCM %d: %s", __func__, pcm_id,
              pcm_get_error(adev->sec.vi_feedback_pcm));
        goto error;
    }
    ALOGD("%s: VI feedback started on PCM %d", __func__, pcm_id);
    return;

error:
    stop_vi_feedback(adev);
}

void sec_audio_vi_feedback_on_enable(struct audio_device *adev, int snd_device)
{
    if (snd_device == SND_DEVICE_IN_CAPTURE_VI_FEEDBACK)
        return;
    if (playback_on_speaker(adev))
        start_vi_feedback(adev);
}

void sec_audio_vi_feedback_on_disable(struct audio_device *adev, int snd_device)
{
    if (snd_device == SND_DEVICE_IN_CAPTURE_VI_FEEDBACK || snd_device >= SND_DEVICE_OUT_END)
        return;
    if (is_speaker_snd_device(snd_device))
        stop_vi_feedback(adev);
}

/* Call state */

void sec_audio_init(struct audio_device *adev)
{
    adev->sec.voice_usecase = USECASE_INVALID;
    adev->sec.voice_device_type = -1;
}

int sec_audio_get_voice_usecase(struct audio_device *adev)
{
    adev->sec.voice_usecase = adev->sec.modem == 1 ? USECASE_VOICEMMODE2_CALL
                                                   : USECASE_VOICEMMODE1_CALL;
    return adev->sec.voice_usecase;
}

static void set_mixer_int(struct audio_device *adev, const char *name, int value)
{
    struct mixer_ctl *ctl = mixer_get_ctl_by_name(adev->mixer, name);

    if (ctl == NULL) {
        ALOGV("%s: no mixer control %s", __func__, name);
        return;
    }
    ALOGV("%s: %s = %d", __func__, name, value);
    mixer_ctl_set_value(ctl, 0, value);
}

/* What the DSP's Samsung voice module tunes for: 0 other, 1 speaker, 2 headset, 3 BT, 4 handset */
static int voice_device_type(audio_devices_t devices)
{
    switch (devices) {
    case AUDIO_DEVICE_OUT_EARPIECE:
        return 4;
    case AUDIO_DEVICE_OUT_SPEAKER:
        return 1;
    case AUDIO_DEVICE_OUT_WIRED_HEADSET:
    case AUDIO_DEVICE_OUT_WIRED_HEADPHONE:
        return 2;
    case AUDIO_DEVICE_OUT_BLUETOOTH_SCO:
    case AUDIO_DEVICE_OUT_BLUETOOTH_SCO_HEADSET:
    case AUDIO_DEVICE_OUT_BLUETOOTH_SCO_CARKIT:
        return 3;
    default:
        return 0;
    }
}

void sec_audio_on_voice_routed(struct audio_device *adev, struct listnode *out_devices)
{
    int type = voice_device_type(get_device_types(out_devices));

    if (type != adev->sec.voice_device_type) {
        ALOGD("%s: voice device info %d", __func__, type);
        set_mixer_int(adev, "Voice Device Info", type);
        adev->sec.voice_device_type = type;
    }
    set_mixer_int(adev, "NB Mode", adev->sec.nb_quality);
}

static void reroute_call(struct audio_device *adev)
{
    if (voice_is_in_call(adev) && adev->sec.voice_usecase != USECASE_INVALID)
        select_devices(adev, adev->sec.voice_usecase);
}

static bool get_bool(struct str_parms *parms, const char *key, bool *out)
{
    char value[32];

    if (str_parms_get_str(parms, key, value, sizeof(value)) < 0)
        return false;
    *out = !strcmp(value, "true") || !strcmp(value, "on");
    str_parms_del(parms, key);
    return true;
}

static void set_sim_slot(struct audio_device *adev, int slot)
{
    char prop[PROPERTY_VALUE_MAX];
    int modem = adev->sec.modem;
    int vsid, vsid2;

    if (slot == 0x02)
        modem = 1;
    else if (slot == 0x01)
        modem = 0;

    property_get("vendor.gsm.current.vsid", prop, "0");
    vsid = atoi(prop);
    property_get("vendor.gsm.current.vsid2", prop, "0");
    vsid2 = atoi(prop);
    if (vsid != 0 && vsid2 == 0)
        modem = !modem;

    if (modem == adev->sec.modem)
        return;
    /* The voice session cannot move to the other modem in the middle of a call */
    if (voice_is_in_call(adev)) {
        ALOGI("%s: not moving the call to modem %d", __func__, modem + 1);
        return;
    }
    ALOGI("%s: calls on modem %d", __func__, modem + 1);
    adev->sec.modem = modem;
}

static void set_call_state(struct audio_device *adev, int state)
{
    struct sec_audio *sec = &adev->sec;

    ALOGI("%s: %#x", __func__, state);

    if (state & 0x2)
        sec->real_call = !sec->csvt;
    else if (state & 0x1)
        sec->real_call = false;

    if (state & 0x20) {
        sec->wifi_call = true;
        sec->csvt = false;
    } else if (state & 0x10) {
        sec->wifi_call = false;
    }

    if (state & 0xf000) {
        sec->csvt = (state & 0x2000) != 0;
        if (sec->csvt)
            sec->wifi_call = false;
    }

    if (state & 0xf0000)
        sec->fmc = (state & 0x20000) != 0;

    reroute_call(adev);
}

void sec_audio_set_parameters(struct audio_device *adev, struct str_parms *parms)
{
    char value[32];
    int val;
    bool flag;

    if (str_parms_get_int(parms, "g_call_sim_slot", &val) >= 0) {
        str_parms_del(parms, "g_call_sim_slot");
        set_sim_slot(adev, val);
    }

    if (str_parms_get_int(parms, "g_call_state", &val) >= 0) {
        str_parms_del(parms, "g_call_state");
        set_call_state(adev, val);
    }

    if (str_parms_get_str(parms, "g_call_band", value, sizeof(value)) >= 0) {
        enum sec_call_band band = adev->sec.call_band;

        str_parms_del(parms, "g_call_band");
        if (!strcmp(value, "swb"))
            band = SEC_CALL_BAND_SWB;
        else if (!strcmp(value, "wb"))
            band = SEC_CALL_BAND_WB;
        else if (!strcmp(value, "nb"))
            band = SEC_CALL_BAND_NB;
        if (str_parms_get_str(parms, "g_call_ims_codec_type", value, sizeof(value)) >= 0 &&
            !strcmp(value, "evs"))
            band = SEC_CALL_BAND_SWB;
        if (band != adev->sec.call_band) {
            ALOGI("%s: call band %d", __func__, band);
            adev->sec.call_band = band;
            reroute_call(adev);
        }
    }

    if (get_bool(parms, "g_call_extra_volume_enable", &flag) && flag != adev->sec.extra_volume) {
        adev->sec.extra_volume = flag;
        reroute_call(adev);
        if (voice_is_in_call(adev))
            voice_set_volume(adev, adev->voice.volume);
    }

    if (get_bool(parms, "l_call_nb_quality_enable", &flag))
        adev->sec.nb_quality = flag;

    if (str_parms_get_int(parms, "g_sco_samplerate", &val) >= 0) {
        bool wb = val == 16000;

        str_parms_del(parms, "g_sco_samplerate");
        if (wb != adev->bt_wb_speech_enabled) {
            ALOGI("%s: BT SCO at %d Hz", __func__, val);
            adev->bt_wb_speech_enabled = wb;
            reroute_call(adev);
        }
    }
}
