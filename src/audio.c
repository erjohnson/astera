#ifndef ALC_ENUMERATE_ALL_EXT
#define ALC_DEFAULT_ALL_DEVICES_SPECIFIER 0x1012
#define ALC_ALL_DEVICES_SPECIFIER 0x1013
#endif

#ifndef ALC_EXT_EFX
#define ALC_EFX_MAJOR_VERSION 0x20001
#define ALC_EFX_MINOR_VERSION 0x20002
#define ALC_MAX_AUXILIARY_SENDS 0x20003
#endif

#include "audio.h"

#include "debug.h"

#undef STB_VORBIS_HEADER_ONLY
#include <misc/stb_vorbis.c>

#include <string.h>

int a_can_play(void) { return g_a_ctx.allow; }

int a_init(const char *device, uint32_t master, uint32_t sfx, uint32_t music) {
  if (!a_ctx_create(device)) {
    _e("Unable to create audio context.\n");
    return -1;
  }

  g_a_map.layer_count = MAX_AUDIO_LAYERS;

  // Initialize cached audio subsystem
  for (int i = 0; i < MAX_SFX; ++i) {
    memset(&g_a_map.sfx[i], 0, sizeof(a_sfx));
    alGenSources(1, &g_a_map.sfx[i].id);
    //_l("Generated SFX Slot: %i\n", g_a_map.sfx[i].id);
    g_a_map.sfx[i].range = DEFAULT_SFX_RANGE;
    g_a_map.sfx[i].gain = 1.f;
    alSourcef(g_a_map.sfx[i].id, AL_GAIN, 1.f);

    // TODO implement range
    alSourcefv(g_a_map.sfx[i].id, AL_POSITION, g_a_map.sfx[i].position);
    g_a_map.sfx[i].has_req = 0;
  }

  g_a_map.sfx_count = MAX_SFX;

  unsigned int sources[MAX_SONGS];
  unsigned int buffers[MAX_SONGS * AUDIO_BUFFERS_PER_MUSIC];

  alGenSources(MAX_SONGS, &sources);
  alGenBuffers(MAX_SONGS * AUDIO_BUFFERS_PER_MUSIC, &buffers);

  for (int i = 0; i < MAX_SONGS; ++i) {
    g_a_map.songs[i].source = sources[i];

    for (int j = 0; j < AUDIO_BUFFERS_PER_MUSIC; ++j) {
      g_a_map.songs[i].buffers[j] = buffers[(i * AUDIO_BUFFERS_PER_MUSIC) + j];
    }

    g_a_map.songs[i].packets_per_buffer = AUDIO_DEFAULT_FRAMES_PER_BUFFER;
    g_a_map.songs[i].gain = 1.f;
    g_a_map.songs[i].has_req = 0;
  }

  g_a_map.song_count = MAX_SONGS;

  float master_f = master / 100.f;
  g_listener.gain = master_f;
  alListenerf(AL_GAIN, g_listener.gain);
  a_set_pos(g_listener.pos);

  for (int i = 0; i < MAX_AUDIO_LAYERS; ++i) {
    g_a_map.layers[i].id = i;
    g_a_map.layers[i].gain = 1.f;
  }

  // TODO convert to processor bounds check
#ifdef AUDIO_MUSIC_LAYER
  if (g_a_map.layer_count > AUDIO_MUSIC_LAYER) {
    g_a_map.layers[AUDIO_MUSIC_LAYER].gain = (music / 100.f);
  } else {
    _e("Not enough layers generated to set volume for music layer on: %i\n",
       AUDIO_MUSIC_LAYER);
  }
#endif
#ifdef AUDIO_SFX_LAYER
  if (g_a_map.layer_count > AUDIO_SFX_LAYER) {
    g_a_map.layers[AUDIO_SFX_LAYER].gain = (sfx / 100.f);
  } else {
    _e("Not enough layers generated to set volume for sfx layer on: %i\n",
       AUDIO_SFX_LAYER);
  }
#endif
#ifdef AUDIO_MISC_LAYER
  if (g_a_map.layer_count > AUDIO_MISC_LAYER) {
    g_a_map.layers[AUDIO_MISC_LAYER].gain = AUDIO_MISC_GAIN;
  } else {
    _e("Not enough layers generated to set volume for misc layer on: %i\n",
       AUDIO_MISC_LAYER);
  }
#endif
#ifdef AUDIO_UI_LAYER
  if (g_a_map.layer_count > AUDIO_UI_LAYER) {
    g_a_map.layers[AUDIO_UI_LAYER].gain = AUDIO_UI_GAIN;
  } else {
    _e("Not enough layers generated to set volume for UI layer on: %i\n",
       AUDIO_UI_LAYER);
  }
#endif
  g_a_ctx.allow = 1;
  return 1;
}

void a_set_pos(vec3 p) { alListener3f(AL_POSITION, p[0], p[1], p[2]); }

void a_set_vol(uint32_t master, uint32_t sfx, uint32_t music) {
  a_set_vol_master(master);
  a_set_vol_sfx(sfx);
  a_set_vol_music(music);
}

void a_set_vol_master(uint32_t master) {
  g_listener.gain = master / 100.f;
  alListenerf(AL_GAIN, g_listener.gain);
}

void a_set_vol_sfx(uint32_t sfx) {
#ifdef AUDIO_SFX_LAYER
  if (g_a_map.layer_count > AUDIO_SFX_LAYER) {
    float value = sfx / 100.f;
    if (g_a_map.layers[AUDIO_SFX_LAYER].gain != value) {
      g_a_map.layers[AUDIO_SFX_LAYER].gain = value;
      g_a_map.layers[AUDIO_SFX_LAYER].gain_change = 1;
    }
  } else {
    _e("Unable to set SFX vol on layer out of bounds: %i\n", AUDIO_SFX_LAYER);
  }
#endif
}

void a_set_vol_music(uint32_t music) {
#ifdef AUDIO_MUSIC_LAYER
  if (g_a_map.layer_count > AUDIO_MUSIC_LAYER) {
    float value = (music / 100.f);
    if (value != g_a_map.layers[AUDIO_MUSIC_LAYER].gain) {
      g_a_map.layers[AUDIO_MUSIC_LAYER].gain = value;
      g_a_map.layers[AUDIO_MUSIC_LAYER].gain_change = 1;
    }
  } else {
    _e("Unable to set music vol on layer out of bounds: %i\n",
       AUDIO_MUSIC_LAYER);
  }
#endif
}

float a_get_vol_master(void) { return g_listener.gain; }

float a_get_vol_sfx(void) {
#ifdef AUDIO_SFX_LAYER
  if (AUDIO_SFX_LAYER < g_a_map.layer_count) {
    return g_a_map.layers[AUDIO_SFX_LAYER].gain;
  } else {
    _e("Unable to get volume for SFX layer, [%i] out of bounds.\n",
       AUDIO_SFX_LAYER);
    return 0.f;
  }
#else
  return 0.f;
#endif
}

float a_get_vol_music(void) {
#ifdef AUDIO_MUSIC_LAYER
  if (AUDIO_MUSIC_LAYER < g_a_map.layer_count) {
    return g_a_map.layers[AUDIO_MUSIC_LAYER].gain;
  } else {
    _e("Unable to get volume for music layer, [%i] out of bounds.\n",
       AUDIO_MUSIC_LAYER);
    return 0.f;
  }
#else
  return 0.f;
#endif
}

void a_exit(void) {
  if (g_a_ctx.context == NULL) {
    return;
  }

  for (int i = 0; i < g_a_map.song_count; ++i) {
    alDeleteSources(1, &g_a_map.songs[i].source);
    alDeleteBuffers(AUDIO_BUFFERS_PER_MUSIC, &g_a_map.songs[i].buffers);
  }

  for (int i = 0; i < g_a_map.sfx_count; ++i) {
    alDeleteSources(1, &g_a_map.sfx[i].id);
  }

  alcMakeContextCurrent(NULL);
  alcDestroyContext(g_a_ctx.context);
  alcCloseDevice(g_a_ctx.device);
}

void a_update(time_s delta) {
  for (int i = 0; i < g_a_map.layer_count; ++i) {
    a_layer *layer = &g_a_map.layers[i];

    for (int j = 0; j < layer->sfx_count; ++j) {
      a_sfx *sfx = &layer->sources[j];
      if (sfx->has_req) {
        a_req *req = sfx->req;
        if (sfx->gain != req->gain || layer->gain_change) {
          float n_gain = layer->gain * req->gain;
          sfx->gain = req->gain;
          alSourcef(sfx->id, AL_GAIN, n_gain);
        }

        if (!vec3_cmp(sfx->position, req->pos)) {
          vec3_dup(sfx->position, req->pos);
          alSource3f(sfx->id, AL_POSITION, req->pos[0], req->pos[1],
                     req->pos[2]);
        }

        ALenum state;
        alGetSourcei(sfx->id, AL_SOURCE_STATE, &state);
        if (state == AL_STOPPED) {
          alSourcei(sfx->id, AL_BUFFER, 0);
          sfx->req = NULL;
          sfx->has_req = 0;
        } else if (req) {
          if (req->stop) {
            alSourcei(g_a_map.sfx[i].id, AL_BUFFER, 0);
            sfx->req = NULL;
            sfx->has_req = 0;
          }
        }
      }
    }

    for (int j = 0; j < MAX_LAYER_SONGS; ++j) {
      a_music *mus = layer->musics[j];
      if (mus) {
        if (mus->req) {
          a_req *req = mus->req;

          float n_gain = layer->gain * req->gain;
          // NOTE: n_gain is a calculated product, not what we want to store
          alSourcef(mus->source, AL_GAIN, n_gain);

          alSource3f(mus->source, AL_POSITION, req->pos[0], req->pos[1],
                     req->pos[2]);

          ALenum state;
          ALint proc;
          alGetSourcei(mus->source, AL_SOURCE_STATE, &state);
          alGetSourcei(mus->source, AL_BUFFERS_PROCESSED, &proc);

          if (req->stop) {
            alSourcei(mus->source, AL_BUFFER, 0);
            alSourceStop(mus->source);
            mus->req = NULL;
            mus->has_req = 0;
          } else if (proc > 0) {
            if (mus->data_length == mus->data_offset) {
              if (mus->loop) {
                mus->data_offset = mus->header_end;
              } else {
                mus->req->stop = 1;
                break;
              }
            }
            int max_samples;
            for (int k = 0; k < proc; ++k) {
              if (mus->samples_left > AUDIO_FRAME_SIZE) {
                max_samples = AUDIO_FRAME_SIZE;
              } else {
                max_samples = mus->samples_left;
              }

              uint32_t buffer;
              int32_t al_err;

              // This is just a comment
              alSourceUnqueueBuffers(mus->source, 1, &buffer);

              if (state != AL_PLAYING && !req->stop && k == 0) {
                alSourcePlay(mus->source);
              }

              memset(mus->pcm, 0, mus->pcm_length * sizeof(short));
              int32_t pcm_total_length = 0;
              int32_t pcm_index = 0, frame_size = 0;
              int32_t bytes_used = 0, num_samples = 0, num_channels = 0;
              float **out;

              int fail_counter = 0;
              for (int p = 0; p < mus->packets_per_buffer; ++p) {
                frame_size = mus->data_length - mus->data_offset;
                if (frame_size > AUDIO_FRAME_SIZE)
                  frame_size = AUDIO_FRAME_SIZE;

#if defined(AUDIO_MUSIC_MAX_FAILS)
                if (fail_counter >= AUDIO_MUSIC_MAX_FAILS)
                  break;
#endif

                bytes_used = stb_vorbis_decode_frame_pushdata(
                    mus->vorbis, mus->data + mus->data_offset, frame_size,
                    &num_channels, &out, &num_samples);
                if (!bytes_used) {
                  _l("Unable to load samples from [%i] bytes.\n", frame_size);
                  ++fail_counter;
                  continue;
                }

                mus->data_offset += bytes_used;

                if (num_samples > 0) {
                  int short_count = num_samples * num_channels;
                  int pcm_length = sizeof(short) * short_count;
                  pcm_total_length += pcm_length;
                  if (pcm_length + pcm_index > mus->pcm_length) {
                    _e("Uhhh.\n");
                    break;
                  }

                  for (int s = 0; s < num_samples; ++s) {
                    for (int c = 0; c < num_channels; ++c) {
                      mus->pcm[pcm_index] = out[c][s] * 32767;
                      ++pcm_index;
                    }
                  }
                }
              }

              alBufferData(buffer, mus->format, mus->pcm, pcm_total_length,
                           mus->vorbis->sample_rate);

              if ((al_err = alGetError()) != AL_INVALID_VALUE) {
                alSourceQueueBuffers(mus->source, 1, &buffer);
              } else {
                _e("Unable to unqueue audio buffer for music err [%i]: %i\n",
                   al_err, buffer);
              }
            }

            if (proc == AUDIO_BUFFERS_PER_MUSIC) {
              alSourcePlay(mus->source);
            }
          }
        }
      }
    }
  }
}

uint32_t a_get_device_name(char *dst, int capacity) {
  ALchar *name;
  if (g_a_ctx.device) {
    name = alcGetString(g_a_ctx.device, ALC_DEVICE_SPECIFIER);
  } else {
    name = alcGetString(NULL, ALC_DEFAULT_DEVICE_SPECIFIER);
  }

  int length = strlen(name);
  int limit = (length > capacity) ? capacity - 1 : length;
  memcpy(dst, name, sizeof(char) * limit);
  dst[limit] = '\0';

  return limit;
}

int8_t a_layer_add_music(uint32_t id, a_music *music) {
  a_layer *layer = 0;
  for (int i = 0; i < g_a_map.layer_count; ++i) {
    if (g_a_map.layers[i].id == id) {
      layer = &g_a_map.layers[i];
      break;
    }
  }

  if (!layer) {
    _l("Unable to find layer: %i\n", id);
    return -1;
  }

  if (layer->music_count == MAX_LAYER_SONGS) {
    _l("No space left in layer: %i\n", id);
    return -1;
  }

  for (int i = 0; i < MAX_LAYER_SONGS; ++i) {
    if (layer->musics[i] == NULL) {
      layer->musics[i] = music;
      ++layer->music_count;
      return 1;
    }
  }

  return 0;
}

int8_t a_layer_add_sfx(uint32_t id, a_sfx *sfx) {
  a_layer *layer = 0;
  for (int i = 0; i < g_a_map.layer_count; ++i) {
    if (g_a_map.layers[i].id == id) {
      layer = &g_a_map.layers[i];
      break;
    }
  }

  if (!layer) {
    return -1;
  }

  if (layer->sfx_count == MAX_LAYER_SFX) {
    return -1;
  }

  for (int i = 0; i < MAX_LAYER_SFX; ++i) {
    if (layer->sources[i] == NULL) {
      layer->sources[i] = sfx;
      ++layer->sfx_count;
      return 1;
    }
  }

  return 0;
}

int a_ctx_create(const char *device_name) {
  ALCdevice *device = NULL;

  if (device_name != NULL) {
    device = alcOpenDevice(device_name);
  } else {
    device = alcOpenDevice(NULL);
  }

  ALCcontext *context = alcCreateContext(device, NULL);

  if (!alcMakeContextCurrent(context)) {
    _e("Error creating OpenAL Context\n");
    return 0;
  }
  g_a_ctx = (a_ctx){context, device};

#if defined(INIT_DEBUG)
  _l("Loaded audio device.\n");
#endif

  return 1;
}

static int16_t a_load_int16(asset_t *asset, int offset) {
  return *((int16_t *)&asset->data[offset]);
}

static int32_t a_load_int32(asset_t *asset, int offset) {
  return *((int32_t *)&asset->data[offset]);
}

a_buf a_buf_create(asset_t *asset) {
  if (!asset || !asset->data) {
    _e("No asset passed to load into audio buffer.\n");
    return (a_buf){0};
  }

  int16_t format = a_load_int16(asset, 20);
  int16_t channels = a_load_int16(asset, 22);
  int32_t sample_rate = a_load_int32(asset, 24);
  int32_t byte_rate = a_load_int32(asset, 28);
  int16_t bits_per_sample = a_load_int16(asset, 34);

  // NOTE: Just tests if it's valid file
  assert(strncmp(&asset->data[36], "data", 4) == 0);

  int32_t byte_length = a_load_int32(asset, 40);

  int al_format = -1;
  if (channels == 2) {
    if (bits_per_sample == 16) {
      al_format = AL_FORMAT_STEREO16;
    } else if (bits_per_sample == 8) {
      al_format = AL_FORMAT_STEREO8;
    }
  } else if (channels == 1) {
    if (bits_per_sample == 16) {
      al_format = AL_FORMAT_MONO16;
    } else if (bits_per_sample == 8) {
      al_format = AL_FORMAT_MONO8;
    }
  }

  if (al_format == -1) {
    _e("Unsupported wave file format.\n");
    return (a_buf){0};
  }

  a_buf buffer;
  alGenBuffers(1, &buffer.id);

  buffer.channels = channels;
  buffer.sample_rate = sample_rate;
  buffer.length = (float)(byte_length / byte_rate);

  alBufferData(buffer.id, al_format, &asset->data[44], byte_length,
               sample_rate);

  return buffer;
}

void a_buf_destroy(a_buf buffer) {
  int rm_index = -1;
  for (int i = 0; i < g_a_map.buf_count; ++i) {
    if (g_a_map.bufs[i].id == buffer.id) {
      rm_index = i;
      break;
    }
  }

  if (rm_index != -1) {
    int end = (g_a_map.buf_count == g_a_map.buf_capacity)
                  ? g_a_map.buf_capacity - 1
                  : g_a_map.buf_count;
    for (int i = rm_index; i < end; ++i) {
      g_a_map.bufs[i] = g_a_map.bufs[i + 1];
    }

    for (int i = rm_index; i < end; ++i) {
      g_a_map.buf_names[i] = g_a_map.buf_names[i + 1];
    }

    if (end == g_a_map.buf_capacity - 1) {
      g_a_map.bufs[end + 1].id = 0;
      g_a_map.buf_names[end + 1] = NULL;
    }
  }

  alDeleteBuffers(1, &buffer.id);
}

a_buf *a_buf_get(const char *name) {
  if (!name) {
    _e("Unable to get null name buffer.\n");
    return NULL;
  }

  for (int i = 0; i < g_a_map.buf_count; ++i) {
    if (strcmp(g_a_map.buf_names[i], name) == 0) {
      return &g_a_map.bufs[i];
    }
  }

  _e("No buffer found named: %s\n", name);
  return NULL;
}

a_sfx *a_play_sfxn(const char *name, a_req *req) {
  if (!name) {
    _e("No buffer name passed to play.\n");
    return NULL;
  }

  a_buf *buff = a_buf_get(name);

  int index = -1;
  for (int i = 0; i < MAX_SFX; ++i) {
    if (!g_a_map.sfx[i].has_req) {
      index = i;
      break;
    }
  }

  if (index == -1) {
    return NULL;
  }

  a_sfx *src = &g_a_map.sfx[index];

  alSourcei(src->id, AL_BUFFER, buff->id);

  if (req) {
    alSourcefv(src->id, AL_POSITION, req->pos);
    alSourcefv(src->id, AL_VELOCITY, req->vel);

    // Adjust to the layer's gain
    float layer_gain = g_a_map.layers[req->layer].gain;
    alSourcef(src->id, AL_GAIN, req->gain * layer_gain);
    // TODO implement range
    alSourcei(src->id, AL_LOOPING, (req->loop) ? AL_TRUE : AL_FALSE);
  } else {
    alSourcefv(src->id, AL_POSITION, (vec3){0.f, 0.f, 0.f});
    alSourcefv(src->id, AL_VELOCITY, (vec3){0.f, 0.f, 0.f});

    // Adjust to the layer's gain
    alSourcef(src->id, AL_GAIN, 1.f);
    // TODO implement range
    alSourcei(src->id, AL_LOOPING, AL_FALSE);
  }

  alSourcePlay(src->id);
  return src;
}

a_sfx *a_play_sfx(a_buf *buff, a_req *req) {
  if (!buff) {
    _e("No buffer passed to play.\n");
    return NULL;
  }

  int index = -1;
  for (int i = 0; i < MAX_SFX; ++i) {
    if (!g_a_map.sfx[i].has_req) {
      index = i;
      break;
    }
  }

  if (index == -1) {
    return NULL;
  }

  a_sfx *src = &g_a_map.sfx[index];

  alSourcei(src->id, AL_BUFFER, buff->id);

  if (req) {
    alSourcefv(src->id, AL_POSITION, req->pos);
    alSourcefv(src->id, AL_VELOCITY, req->vel);

    // Adjust to the layer's gain
    float layer_gain = g_a_map.layers[req->layer].gain;
    alSourcef(src->id, AL_GAIN, req->gain * layer_gain);
    // TODO implement range
    alSourcei(src->id, AL_LOOPING, (req->loop) ? AL_TRUE : AL_FALSE);
  } else {
    alSourcefv(src->id, AL_POSITION, (vec3){0.f, 0.f, 0.f});
    alSourcefv(src->id, AL_VELOCITY, (vec3){0.f, 0.f, 0.f});

    // Adjust to the layer's gain
    alSourcef(src->id, AL_GAIN, 1.f);
    // TODO implement range
    alSourcei(src->id, AL_LOOPING, AL_FALSE);
  }

  alSourcePlay(src->id);
  return src;
}

a_music *a_music_create(asset_t *asset, a_meta *meta, a_req *req) {
  if (!asset) {
    _e("No data passed to create music.\n");
    return NULL;
  }

  // Get the first free open song slot
  int index = -1;
  for (int i = 0; i < MAX_SONGS; ++i) {
    if (!g_a_map.songs[i].req && g_a_map.songs[i].source != 0) {
      index = i;
      break;
    }
  }

  if (index == -1) {
    _e("No available music slots.\n");
    return NULL;
  }

  a_music *music = &g_a_map.songs[index];

  int error, used;

  music->data_length = asset->data_length;
  music->data = asset->data;
  music->vorbis = stb_vorbis_open_pushdata(asset->data, asset->data_length,
                                           &used, &error, NULL);

  if (!music->vorbis) {
    music->data_length = 0;
    music->data = 0;
    music->req = 0;
    _e("Unable to load vorbis, that sucks.\n");
    return NULL;
  }

  music->header_end = used;
  music->data_offset += used;

  stb_vorbis_info info = stb_vorbis_get_info(music->vorbis);
  music->sample_rate = info.sample_rate;

  if (info.channels > 2) {
    _e("Invalid channel size for audio system: %i.\n", info.channels);
    music->data_length = 0;
    music->data = 0;
    music->req = 0;
    return 0;
  }

  music->format = (info.channels == 1) ? AL_FORMAT_MONO16 : AL_FORMAT_STEREO16;
  music->pcm_length =
      AUDIO_FRAME_SIZE * music->packets_per_buffer * info.channels;

  music->pcm = malloc(sizeof(short) * music->pcm_length);

  if (!music->pcm) {
    music->data_length = 0;
    music->data = 0;
    music->req = 0;
    _e("Unable to allocate %i shorts.\n", music->pcm_length);
    return 0;
  }

  alSourcef(music->source, AL_PITCH, 1.f);
  if (meta) {
    music->meta = meta;
    music->total_samples = meta->total_samples;
    music->samples_left = meta->total_samples;
  }

  for (int i = 0; i < AUDIO_BUFFERS_PER_MUSIC; ++i) {
    unsigned int buffer = music->buffers[i];

    int pcm_index, bytes_used, num_channels, num_samples, frame_size;
    pcm_index = bytes_used = num_channels = num_samples = frame_size = 0;

    int pcm_total_length = 0;

    float **out;

    memset(music->pcm, 0, sizeof(short) * music->pcm_length);

    for (int j = 0; j < music->packets_per_buffer; ++j) {
      frame_size = music->data_length - music->data_offset;
      if (frame_size > AUDIO_FRAME_SIZE)
        frame_size = AUDIO_FRAME_SIZE;

      bytes_used = stb_vorbis_decode_frame_pushdata(
          music->vorbis, music->data + music->data_offset, frame_size,
          &num_channels, &out, &num_samples);

      if (bytes_used == 0) {
        _e("Unable to process samples from %i bytes.\n", frame_size);
        break;
      }

      music->data_offset += bytes_used;

      if (num_samples > 0) {
        int sample_count = num_channels * num_samples;
        int pcm_size = sample_count * sizeof(short);
        pcm_total_length += pcm_size;
        for (int s = 0; s < num_samples - 1; ++s) {
          for (int c = 0; c < num_channels; ++c) {
            if (pcm_index >= music->pcm_length) {
              break;
            }

            music->pcm[pcm_index] = out[c][s] * 32676;
            ++pcm_index;
          }
        }
      }
    }

    alBufferData(buffer, music->format, music->pcm, pcm_total_length,
                 music->vorbis->sample_rate);
    alSourceQueueBuffers(music->source, 1, &buffer);
  }

  if (req) {
    music->req = req;
    alSourcef(music->source, AL_GAIN, req->gain);
    music->loop = req->loop;
    music->has_req = 1;
  }

  return music;
}

void a_music_reset(a_music *music) {
  // TODO finish this implementation
  // NOTE: Playing -> load back buffer first, then push to front & fill back
  // buffer again
  // Stopped -> throw out both buffers and fill normally
  /*music->data_offset = 0;
  for (int i = 0; i < AUDIO_BUFFERS_PER_MUSIC; ++i) {
    unsigned int buffer = music->buffers[i];

    int pcm_index, bytes_used, num_channels, num_samples, frame_size;
    pcm_index = bytes_used = num_channels = num_samples = frame_size = 0;

    int pcm_total_length = 0;

    float **out;

    memset(music->pcm, 0, sizeof(short) * music->pcm_length);

    for (int j = 0; j < music->packets_per_buffer; ++j) {
      frame_size = music->data_length - music->data_offset;
      if (frame_size > AUDIO_FRAME_SIZE)
        frame_size = AUDIO_FRAME_SIZE;

      bytes_used = stb_vorbis_decode_frame_pushdata(
          music->vorbis, music->data + music->data_offset, frame_size,
          &num_channels, &out, &num_samples);

      if (bytes_used == 0) {
        _e("Unable to process samples from %i bytes.\n", frame_size);
        break;
      }

      music->data_offset += bytes_used;

      if (num_samples > 0) {
        int sample_count = num_channels * num_samples;
        int pcm_size = sample_count * sizeof(short);
        pcm_total_length += pcm_size;
        for (int s = 0; s < num_samples - 1; ++s) {
          for (int c = 0; c < num_channels; ++c) {
            if (pcm_index >= music->pcm_length) {
              break;
            }

            music->pcm[pcm_index] = out[c][s] * 32676;
            ++pcm_index;
          }
        }
      }
    }

    alBufferData(buffer, music->format, music->pcm, pcm_total_length,
                 music->vorbis->sample_rate);
    alSourceQueueBuffers(music->source, 1, &buffer);
  }*/
}

void a_music_destroy(a_music *music) { stb_vorbis_close(music->vorbis); }

time_s a_music_time(a_music *music) {
  uint32_t samples = music->total_samples - music->samples_left;
  return (time_s)(samples / music->sample_rate);
}

time_s a_music_len(a_music *music) {
  return (time_s)(music->total_samples / music->sample_rate);
}

void a_music_play(a_music *music) { alSourcePlay(music->source); }

void a_music_stop(a_music *music) {
  alSourceStop(music->source);
  stb_vorbis_seek_start(music->vorbis);
  music->samples_left = music->total_samples;
}

void a_resume_music(a_music *music) {
  ALenum state;
  alSourcei(music->source, AL_SOURCE_STATE, &state);
  if (state == AL_PAUSED) {
    alSourcePlay(music->source);
  }
}

void a_music_pause(a_music *music) { alSourcePause(music->source); }

static int a_get_open_sfx(void) {
  for (int i = 0; i < MAX_SFX; ++i) {
    if (!g_a_map.sfx[i].has_req) {
      return i;
    }
  }

  return -1;
}
