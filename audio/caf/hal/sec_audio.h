/*
 * Copyright (C) 2026 The LineageOS Project
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/*
 * Samsung's audio HAL extensions for the Galaxy A9 (2018), rebuilt on top of
 * the CAF HAL from what the stock audio.primary.sdm660.so does: sound device
 * selection for Samsung's sound devices, the call state Samsung's telephony
 * and IMS stack report through set_parameters, and the speaker amplifier's
 * VI feedback.
 */

#ifndef SEC_AUDIO_H
#define SEC_AUDIO_H

#include <stdbool.h>

#include <cutils/list.h>

struct audio_device;
struct pcm;
struct stream_in;
struct str_parms;

/* What kind of VoIP call the active voice communication input belongs to */
enum sec_voip_type {
    SEC_VOIP_NONE,
    SEC_VOIP_COMM,      /* VoIP app in communication mode */
    SEC_VOIP_WIFI_CALL, /* IMS call over Wi-Fi */
    SEC_VOIP_CSVT,      /* circuit-switched video call */
    SEC_VOIP_FMC,       /* fixed-mobile convergence call */
};

enum sec_call_band {
    SEC_CALL_BAND_NB,
    SEC_CALL_BAND_WB,
    SEC_CALL_BAND_SWB, /* EVS */
};

struct sec_audio {
    /* g_call_state */
    bool real_call;
    bool wifi_call;
    bool csvt;
    bool fmc;

    enum sec_voip_type voip_type;
    enum sec_call_band call_band; /* g_call_band, g_call_ims_codec_type */
    bool extra_volume;            /* g_call_extra_volume_enable */
    bool nb_quality;              /* l_call_nb_quality_enable */

    /* g_call_sim_slot: the modem (0 for SIM 1, 1 for SIM 2) calls run on */
    int modem;
    /* VoiceMMode usecase of the ongoing call */
    int voice_usecase;
    /* Last "Voice Device Info" sent to the DSP, -1 for none */
    int voice_device_type;

    /* VI feedback of the speaker amplifier */
    struct pcm *vi_feedback_pcm;
};

void sec_audio_init(struct audio_device *adev);

/* Samsung's call parameters; adev->lock must be held */
void sec_audio_set_parameters(struct audio_device *adev, struct str_parms *parms);

/* The VoiceMMode usecase a new call runs on: VoiceMMode1 for SIM 1, 2 for SIM 2 */
int sec_audio_get_voice_usecase(struct audio_device *adev);
/* Tell the DSP about the call's devices and band once a call (re)routes */
void sec_audio_on_voice_routed(struct audio_device *adev, struct listnode *out_devices);

/* Samsung's sound device for out_devices, or SND_DEVICE_NONE to leave it to CAF */
int sec_audio_get_output_snd_device(struct audio_device *adev,
                                    struct listnode *out_devices);
/* Samsung's sound device for an input, or SND_DEVICE_NONE to leave it to CAF */
int sec_audio_get_input_snd_device(struct audio_device *adev, struct stream_in *in,
                                   struct listnode *out_devices);

/* Start the VI feedback when a playback usecase runs on the speaker */
void sec_audio_vi_feedback_on_enable(struct audio_device *adev, int snd_device);
/* Stop the VI feedback when a speaker sound device goes away */
void sec_audio_vi_feedback_on_disable(struct audio_device *adev, int snd_device);

#endif /* SEC_AUDIO_H */
