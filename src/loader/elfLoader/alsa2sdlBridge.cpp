#if defined(_WIN32) || defined(__MINGW32__)

#include "alsa2sdlBridge.hpp"
#include "symbolResolver.hpp"
#include "../audio/lindbergh/alsa2sdl/alsa2sdl.h"

#define MAP(name, func) SymbolResolver::GetInstance().RegisterVTable(name, reinterpret_cast<void *>(func))

namespace Alsa2SdlBridge
{
    void initBridges()
    {
        MAP("snd_pcm_open", shared_snd_pcm_open);
        MAP("snd_pcm_close", shared_snd_pcm_close);

        MAP("snd_pcm_hw_params_malloc", shared_snd_pcm_hw_params_malloc);
        MAP("snd_pcm_hw_params_free", shared_snd_pcm_hw_params_free);
        MAP("snd_pcm_hw_params_any", shared_snd_pcm_hw_params_any);
        MAP("snd_pcm_hw_params_set_access", shared_snd_pcm_hw_params_set_access);
        MAP("snd_pcm_hw_params_set_format", shared_snd_pcm_hw_params_set_format);
        MAP("snd_pcm_hw_params_set_channels", shared_snd_pcm_hw_params_set_channels);
        MAP("snd_pcm_hw_params_set_rate_near", shared_snd_pcm_hw_params_set_rate_near);
        MAP("snd_pcm_hw_params_set_periods", shared_snd_pcm_hw_params_set_periods);
        MAP("snd_pcm_hw_params_set_buffer_size", shared_snd_pcm_hw_params_set_buffer_size);
        MAP("snd_pcm_hw_params_set_buffer_size_near", shared_snd_pcm_hw_params_set_buffer_size_near);
        MAP("snd_pcm_hw_params_set_period_size_near", shared_snd_pcm_hw_params_set_period_size_near);
        MAP("snd_pcm_hw_params", shared_snd_pcm_hw_params);

        MAP("snd_pcm_sw_params_malloc", shared_snd_pcm_sw_params_malloc);
        MAP("snd_pcm_sw_params_free", shared_snd_pcm_sw_params_free);
        MAP("snd_pcm_sw_params_current", shared_snd_pcm_sw_params_current);
        MAP("snd_pcm_sw_params_set_avail_min", shared_snd_pcm_sw_params_set_avail_min);
        MAP("snd_pcm_sw_params_set_start_threshold", shared_snd_pcm_sw_params_set_start_threshold);
        MAP("snd_pcm_sw_params_set_xfer_align", shared_snd_pcm_sw_params_set_xfer_align);
        MAP("snd_pcm_sw_params", shared_snd_pcm_sw_params);

        MAP("snd_pcm_prepare", shared_snd_pcm_prepare);
        MAP("snd_pcm_start", shared_snd_pcm_start);
        MAP("snd_pcm_drop", shared_snd_pcm_drop);
        MAP("snd_pcm_resume", shared_snd_pcm_resume);
        MAP("snd_pcm_drain", shared_snd_pcm_drain);
        MAP("snd_pcm_delay", shared_snd_pcm_delay);
        MAP("snd_pcm_writei", shared_snd_pcm_writei);
        MAP("snd_pcm_avail_update", shared_snd_pcm_avail_update);
        MAP("snd_pcm_state", shared_snd_pcm_state);
        MAP("snd_pcm_format_width", shared_snd_pcm_format_width);
        MAP("snd_strerror", shared_snd_strerror);

        // The rest of the ALSA API the g6 games import.
        MAP("snd_pcm_hw_params_copy", shared_snd_pcm_hw_params_copy);
        MAP("snd_pcm_hw_params_get_buffer_size", shared_snd_pcm_hw_params_get_buffer_size);
        MAP("snd_pcm_hw_params_get_channels", shared_snd_pcm_hw_params_get_channels);
        MAP("snd_pcm_hw_params_get_periods", shared_snd_pcm_hw_params_get_periods);
        MAP("snd_pcm_hw_params_get_period_size", shared_snd_pcm_hw_params_get_period_size);
        MAP("snd_pcm_hw_params_set_periods_near", shared_snd_pcm_hw_params_set_periods_near);
        MAP("snd_pcm_hw_params_sizeof", shared_snd_pcm_hw_params_sizeof);
        MAP("snd_pcm_sw_params_sizeof", shared_snd_pcm_sw_params_sizeof);
        MAP("snd_pcm_nonblock", shared_snd_pcm_nonblock);
        MAP("snd_pcm_recover", shared_snd_pcm_recover);

        // PulseAudio, routed to the same SDL audio stream.
        MAP("pa_channel_map_init_auto", shared_pa_channel_map_init_auto);
        MAP("pa_mainloop_new", shared_pa_mainloop_new);
        MAP("pa_mainloop_get_api", shared_pa_mainloop_get_api);
        MAP("pa_mainloop_free", shared_pa_mainloop_free);
        MAP("pa_mainloop_iterate", shared_pa_mainloop_iterate);
        MAP("pa_context_new", shared_pa_context_new);
        MAP("pa_context_set_name", shared_pa_context_set_name);
        MAP("pa_context_connect", shared_pa_context_connect);
        MAP("pa_context_get_state", shared_pa_context_get_state);
        MAP("pa_context_disconnect", shared_pa_context_disconnect);
        MAP("pa_context_unref", shared_pa_context_unref);
        MAP("pa_stream_new", shared_pa_stream_new);
        MAP("pa_stream_connect_playback", shared_pa_stream_connect_playback);
        MAP("pa_stream_write", shared_pa_stream_write);
        MAP("pa_stream_drain", shared_pa_stream_drain);
        MAP("pa_stream_get_state", shared_pa_stream_get_state);
        MAP("pa_stream_disconnect", shared_pa_stream_disconnect);
        MAP("pa_stream_writable_size", shared_pa_stream_writable_size);
        MAP("pa_stream_unref", shared_pa_stream_unref);
        MAP("pa_simple_new", shared_pa_simple_new);
        MAP("pa_simple_free", shared_pa_simple_free);
        MAP("pa_operation_cancel", shared_pa_operation_cancel);
        MAP("pa_operation_get_state", shared_pa_operation_get_state);
        MAP("pa_operation_unref", shared_pa_operation_unref);
    }
} // namespace Alsa2SdlBridge

#endif