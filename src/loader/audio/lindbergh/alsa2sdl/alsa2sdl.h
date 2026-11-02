#ifndef ALSA2SDL_H
#define ALSA2SDL_H

#ifdef __cplusplus
extern "C"
{
#endif

    typedef struct _snd_pcm snd_pcm_t;
    typedef struct _snd_pcm_hw_params snd_pcm_hw_params_t;
    typedef struct _snd_pcm_sw_params snd_pcm_sw_params_t;
    typedef long snd_pcm_sframes_t;
    typedef unsigned long snd_pcm_uframes_t;

    int shared_snd_pcm_open(snd_pcm_t **pcm, const char *name, int stream, int mode);
    int shared_snd_pcm_close(snd_pcm_t *pcm);

    int shared_snd_pcm_hw_params_malloc(snd_pcm_hw_params_t **ptr);
    void shared_snd_pcm_hw_params_free(snd_pcm_hw_params_t *obj);
    int shared_snd_pcm_hw_params_any(snd_pcm_t *pcm, snd_pcm_hw_params_t *params);
    int shared_snd_pcm_hw_params_set_access(snd_pcm_t *pcm, snd_pcm_hw_params_t *params, int access);
    int shared_snd_pcm_hw_params_set_format(snd_pcm_t *pcm, snd_pcm_hw_params_t *params, int val);
    int shared_snd_pcm_hw_params_set_channels(snd_pcm_t *pcm, snd_pcm_hw_params_t *params, int val);
    int shared_snd_pcm_hw_params_set_rate_near(snd_pcm_t *pcm, snd_pcm_hw_params_t *params, unsigned int *val, int *dir);
    int shared_snd_pcm_hw_params_set_periods(snd_pcm_t *pcm, snd_pcm_hw_params_t *params, unsigned int val, int dir);
    int shared_snd_pcm_hw_params_set_buffer_size(snd_pcm_t *pcm, snd_pcm_hw_params_t *params, snd_pcm_uframes_t val);
    int shared_snd_pcm_hw_params_set_buffer_size_near(snd_pcm_t *pcm, snd_pcm_hw_params_t *params, unsigned long *val);
    int shared_snd_pcm_hw_params_set_period_size_near(snd_pcm_t *pcm, snd_pcm_hw_params_t *params, unsigned long *val, int *dir);
    int shared_snd_pcm_hw_params(snd_pcm_t *pcm, snd_pcm_hw_params_t *params);

    int shared_snd_pcm_sw_params_malloc(snd_pcm_sw_params_t **ptr);
    void shared_snd_pcm_sw_params_free(snd_pcm_sw_params_t *obj);
    int shared_snd_pcm_sw_params_current(snd_pcm_t *pcm, snd_pcm_sw_params_t *params);
    int shared_snd_pcm_sw_params_set_avail_min(snd_pcm_t *pcm, snd_pcm_sw_params_t *params, snd_pcm_uframes_t val);
    int shared_snd_pcm_sw_params_set_start_threshold(snd_pcm_t *pcm, snd_pcm_sw_params_t *params, snd_pcm_uframes_t val);
    int shared_snd_pcm_sw_params_set_xfer_align(snd_pcm_t *pcm, snd_pcm_sw_params_t *params, snd_pcm_uframes_t val);
    int shared_snd_pcm_sw_params(snd_pcm_t *pcm, snd_pcm_sw_params_t *params);

    int shared_snd_pcm_prepare(snd_pcm_t *pcm);
    int shared_snd_pcm_start(snd_pcm_t *pcm);
    int shared_snd_pcm_drop(snd_pcm_t *pcm);
    int shared_snd_pcm_resume(snd_pcm_t *pcm);
    int shared_snd_pcm_drain(snd_pcm_t *pcm);
    int shared_snd_pcm_delay(snd_pcm_t *pcm, snd_pcm_sframes_t *delayp);
    snd_pcm_sframes_t shared_snd_pcm_writei(snd_pcm_t *pcm, const void *buffer, unsigned long size);
    int shared_snd_pcm_avail_update(snd_pcm_t *pcm);
    int shared_snd_pcm_state(snd_pcm_t *pcm);
    int shared_snd_pcm_format_width(int format);
    const char *shared_snd_strerror(int errnum);

    // The rest of the ALSA API the g6 games (Angry Birds, The Walking Dead) import.
    int shared_snd_pcm_hw_params_copy(snd_pcm_hw_params_t *dst, const snd_pcm_hw_params_t *src);
    int shared_snd_pcm_hw_params_get_buffer_size(snd_pcm_hw_params_t *p, snd_pcm_uframes_t *value);
    int shared_snd_pcm_hw_params_get_channels(snd_pcm_hw_params_t *p, unsigned int *value);
    int shared_snd_pcm_hw_params_get_periods(snd_pcm_hw_params_t *p, unsigned int *value, int *value_max);
    int shared_snd_pcm_hw_params_get_period_size(snd_pcm_hw_params_t *p, snd_pcm_uframes_t *value, snd_pcm_uframes_t *value_max);
    int shared_snd_pcm_hw_params_set_periods_near(snd_pcm_t *pcm, snd_pcm_hw_params_t *p, unsigned int *value, int *dir);
    unsigned long shared_snd_pcm_hw_params_sizeof(void);
    unsigned long shared_snd_pcm_sw_params_sizeof(void);
    int shared_snd_pcm_nonblock(snd_pcm_t *pcm, int nonblock);
    int shared_snd_pcm_recover(snd_pcm_t *pcm, int err, int silent);

    // PulseAudio: the g6 games link a Pulse client alongside ALSA (on the
    // cabinet Pulse wins); on Windows there is no Pulse, so the calls are
    // routed to the same SDL audio stream.
    int shared_pa_channel_map_init_auto(void *map, unsigned int channels, const char *map_name);
    void *shared_pa_mainloop_new(void);
    void *shared_pa_mainloop_get_api(void *mainloop);
    void shared_pa_mainloop_free(void *mainloop);
    int shared_pa_mainloop_iterate(void *mainloop, int block, int *time);
    void *shared_pa_context_new(void *spawncb, const char *name, void *map);
    int shared_pa_context_set_name(void *context, const char *name);
    int shared_pa_context_connect(void *context, const char *server, int flags, void *auth);
    int shared_pa_context_get_state(void *context);
    int shared_pa_context_disconnect(void *context);
    int shared_pa_context_unref(void *context);
    void *shared_pa_stream_new(void *context, const char *name, void *ss, void *map);
    int shared_pa_stream_connect_playback(void *stream, const char *device, const void *buffer_attr, long long initial_bytes, int format, int *error_code);
    int shared_pa_stream_write(void *stream, const void *data, unsigned long size, long long offset, int eol, void *writer);
    int shared_pa_stream_drain(void *stream, long long usec, void *userdata);
    int shared_pa_stream_get_state(void *stream);
    int shared_pa_stream_disconnect(void *stream);
    unsigned long shared_pa_stream_writable_size(void *stream);
    int shared_pa_stream_unref(void *stream);
    void *shared_pa_simple_new(void *context, const char *name, int direction, void *device, const char *stream_name, void *ss, void *map, void *attr, long long *usec);
    int shared_pa_simple_free(void *simple);
    int shared_pa_operation_cancel(void *op);
    int shared_pa_operation_get_state(void *op);
    int shared_pa_operation_unref(void *op);

#ifdef __cplusplus
}
#endif

#endif // ALSA2SDL_H
