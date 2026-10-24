/*
 * Copyright (C) 2026 The LineageOS Project
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/*
 * Samsung's audio HAL extensions for the Galaxy A9 (2018), rebuilt on top of
 * the CAF HAL from what the stock audio.primary.sdm660.so does: sound device
 * selection for Samsung's sound devices, the call state Samsung's telephony
 * and IMS stack report through set_parameters, the speaker amplifier's
 * VI feedback, and the recording preprocessing of librecordalive.
 */

#ifndef SEC_AUDIO_H
#define SEC_AUDIO_H

#include <stdbool.h>
#include <stddef.h>

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

    enum sec_call_band call_band; /* g_call_band, g_call_ims_codec_type */
    bool extra_volume;            /* g_call_extra_volume_enable */
    bool nb_quality;              /* l_call_nb_quality_enable */

    /* g_call_sim_slot: the modem (0 for SIM 1, 1 for SIM 2) calls run on */
    int modem;
    /* VoiceMMode usecase of the ongoing call */
    int voice_usecase;

    /* VI feedback of the speaker amplifier */
    struct pcm *vi_feedback_pcm;

    /*
     * librecordalive runs Samsung's recording solutions (gain control, wind
     * noise suppression) on the captured data. It keeps one set of state, so
     * it serves the input that started last.
     */
    void *recordalive;
    void (*preprocess_init)(unsigned int solutions);
    void (*preprocess_set_params)(struct str_parms *parms);
    void (*preprocess_clear)(void);
    void (*preprocess)(unsigned int solutions, void *in, void *out, size_t bytes,
                       size_t frames);
    struct stream_in *preprocess_in;
    unsigned int preprocess_solutions;
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

/* Set up the recording preprocessing for an input that starts; adev->lock must be held */
void sec_audio_preprocess_start(struct audio_device *adev, struct stream_in *in);
/* Hand the preprocessing back when the input stops; adev->lock must be held */
void sec_audio_preprocess_stop(struct audio_device *adev, struct stream_in *in);
/* Tell the preprocessing about the input's new devices; adev->lock must be held */
void sec_audio_preprocess_route(struct audio_device *adev, struct stream_in *in);
/* Run the preprocessing in place on the data an input read; in->lock must be held */
void sec_audio_preprocess(struct audio_device *adev, struct stream_in *in, void *buffer,
                          size_t bytes);

#endif /* SEC_AUDIO_H */
