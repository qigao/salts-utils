/**
 * iOS Audio Capture Implementation
 *
 * Uses AVAudioEngine for low-latency microphone capture.
 */

#import <AVFoundation/AVFoundation.h>
#import <Foundation/Foundation.h>
#import "capture_ios_guard.h"
#include "salts_capture.h"
#include <stdlib.h>

typedef struct {
    salts_capture_t base;
    AVAudioEngine *engine;
    AVAudioInputNode *input_node;
    AVAudioFormat *format;
    int sample_rate;
    int channels;
    int bits_per_sample;
    SaltsCaptureGuard *guard;
} ios_audio_capture_t;

static uint64_t now_us(void) {
    return (uint64_t)([[NSDate date] timeIntervalSince1970] * 1000000.0);
}

static void ios_audio_finalize(void *capture) {
    ios_audio_capture_t *cap = (ios_audio_capture_t *)capture;
    cap->guard = nil;
    free(cap);
}

int ios_audio_start(salts_capture_t *capture) {
    ios_audio_capture_t *cap = (ios_audio_capture_t *)capture;

    @autoreleasepool {
        NSError *error = nil;
        if (![cap->engine startAndReturnError:&error]) {
            return -1;
        }

        return 0;
    }
}

void ios_audio_stop(salts_capture_t *capture) {
    ios_audio_capture_t *cap = (ios_audio_capture_t *)capture;

    @autoreleasepool {
        [cap->engine stop];
    }
}

void ios_audio_destroy(salts_capture_t *capture) {
    ios_audio_capture_t *cap = (ios_audio_capture_t *)capture;

    @autoreleasepool {
        if (cap->input_node) {
            [cap->input_node removeTapOnBus:0];
        }
        if (cap->engine) {
            [cap->engine stop];
            cap->engine = nil;
        }

        cap->input_node = nil;
        cap->format = nil;
        [cap->guard detachOwner];
    }
}

salts_capture_t *salts_audio_capture_create(const char *device_id,
                                            const salts_audio_capture_config_t *config) {
    (void)device_id;

    @autoreleasepool {
        ios_audio_capture_t *cap = calloc(1, sizeof(ios_audio_capture_t));
        if (!cap) return NULL;

        cap->guard = [[SaltsCaptureGuard alloc] initWithCapture:cap
                                                     finalizer:ios_audio_finalize];
        if (!cap->guard) {
            free(cap);
            return NULL;
        }

        cap->base.type = SALTS_CAPTURE_TYPE_AUDIO;
        cap->base.state = SALTS_CAPTURE_STATE_STOPPED;
        cap->base.platform_ctx = cap;
        cap->sample_rate = (config && config->sample_rate > 0) ? config->sample_rate : 48000;
        cap->channels = (config && config->channels > 0) ? config->channels : 1;
        cap->bits_per_sample =
            (config && config->bits_per_sample > 0) ? config->bits_per_sample : 32;

        AVAudioSession *session = [AVAudioSession sharedInstance];
        NSError *error = nil;

        if (![session
                setCategory:AVAudioSessionCategoryPlayAndRecord
                 withOptions:AVAudioSessionCategoryOptionDefaultToSpeaker |
                             AVAudioSessionCategoryOptionAllowBluetooth
                       error:&error] ||
            ![session setPreferredSampleRate:cap->sample_rate error:&error] ||
            ![session setPreferredIOBufferDuration:0.005 error:&error] ||
            ![session setActive:YES error:&error]) {
            ios_audio_destroy((salts_capture_t *)cap);
            return NULL;
        }

        cap->engine = [[AVAudioEngine alloc] init];
        cap->input_node = cap->engine.inputNode;
        AVAudioFormat *input_format = [cap->input_node outputFormatForBus:0];

        cap->format = [[AVAudioFormat alloc]
            initWithCommonFormat:AVAudioPCMFormatFloat32
                       sampleRate:cap->sample_rate
                         channels:(AVAudioChannelCount)cap->channels
                      interleaved:YES];
        if (!cap->format) {
            ios_audio_destroy((salts_capture_t *)cap);
            return NULL;
        }

        SaltsCaptureGuard *guard = cap->guard;
        [cap->input_node installTapOnBus:0
                              bufferSize:1024
                                  format:input_format
                                   block:^(AVAudioPCMBuffer *buffer, AVAudioTime *when) {
            (void)when;
            ios_audio_capture_t *strong_cap =
                (ios_audio_capture_t *)[guard acquireCapture];
            if (!strong_cap) return;

            if (!strong_cap->base.audio_cb || !buffer.floatChannelData) {
                [guard releaseCapture];
                return;
            }

            const uint8_t *samples = (const uint8_t *)buffer.floatChannelData[0];
            size_t len = (size_t)buffer.frameLength *
                         (size_t)buffer.format.channelCount *
                         sizeof(float);

            strong_cap->base.audio_cb((salts_capture_t *)strong_cap, samples, len,
                                      now_us(), strong_cap->base.user_data);
            [guard releaseCapture];
        }];

        return (salts_capture_t *)cap;
    }
}
