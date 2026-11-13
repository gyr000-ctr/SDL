/**
 * WAVE container loading tests (RIFF, RF64, BW64, Sony Wave64).
 *
 * All test files are assembled in memory so the tests do not depend on
 * external resource files.
 */

/* quiet windows compiler warnings */
#if defined(_MSC_VER) && !defined(_CRT_SECURE_NO_WARNINGS)
#define _CRT_SECURE_NO_WARNINGS
#endif

#include <stdio.h>

#include <SDL3/SDL.h>
#include <SDL3/SDL_test.h>
#include "testautomation_suites.h"

/* ================= In-memory WAVE builders ================== */

typedef struct WaveBuilder
{
    Uint8 *data;
    size_t size;
    size_t capacity;
    bool ok;
} WaveBuilder;

static void WaveBuilderInit(WaveBuilder *b)
{
    b->capacity = 256;
    b->size = 0;
    b->ok = true;
    b->data = (Uint8 *)SDL_malloc(b->capacity);
    if (!b->data) {
        b->ok = false;
        b->capacity = 0;
    }
}

static void WaveBuilderFree(WaveBuilder *b)
{
    SDL_free(b->data);
    b->data = NULL;
    b->size = b->capacity = 0;
}

static bool WaveBuilderReserve(WaveBuilder *b, size_t extra)
{
    if (b->size + extra > b->capacity) {
        size_t newcap = b->capacity ? b->capacity : 256;
        while (newcap < b->size + extra) {
            newcap *= 2;
        }
        Uint8 *newdata = (Uint8 *)SDL_realloc(b->data, newcap);
        if (!newdata) {
            b->ok = false;
            return false;
        }
        b->data = newdata;
        b->capacity = newcap;
    }
    return true;
}

static void WaveBuilderPut(WaveBuilder *b, const void *src, size_t len)
{
    if (!b->ok || !WaveBuilderReserve(b, len)) {
        b->ok = false;
        return;
    }
    SDL_memcpy(b->data + b->size, src, len);
    b->size += len;
}

static void WaveBuilderU8(WaveBuilder *b, Uint8 v)
{
    WaveBuilderPut(b, &v, sizeof(v));
}

static void WaveBuilderU16(WaveBuilder *b, Uint16 v)
{
    Uint8 raw[2] = { (Uint8)(v & 0xff), (Uint8)(v >> 8) };
    WaveBuilderPut(b, raw, sizeof(raw));
}

static void WaveBuilderU32(WaveBuilder *b, Uint32 v)
{
    Uint8 raw[4] = {
        (Uint8)(v & 0xff), (Uint8)((v >> 8) & 0xff),
        (Uint8)((v >> 16) & 0xff), (Uint8)((v >> 24) & 0xff)
    };
    WaveBuilderPut(b, raw, sizeof(raw));
}

static void WaveBuilderU64(WaveBuilder *b, Uint64 v)
{
    Uint8 raw[8];
    int i;
    for (i = 0; i < 8; i++) {
        raw[i] = (Uint8)((v >> (i * 8)) & 0xff);
    }
    WaveBuilderPut(b, raw, sizeof(raw));
}

static void WaveBuilderFourCC(WaveBuilder *b, Uint32 fourcc)
{
    WaveBuilderU32(b, fourcc);
}

static void WaveBuilderPad(WaveBuilder *b, size_t alignment)
{
    while (b->ok && (b->size % alignment) != 0) {
        WaveBuilderU8(b, 0);
    }
}

// Chunk GUIDs used by Sony Wave64.
static const Uint8 W64GUID_RIFF[16] = {
    0x66, 0x66, 0x6D, 0x72, 0x35, 0x62, 0xD2, 0x11,
    0xAF, 0x8F, 0x00, 0xC0, 0x4F, 0x8E, 0xEB, 0x97
};
static const Uint8 W64GUID_WAVE[16] = {
    0x65, 0x66, 0x6D, 0x72, 0x35, 0x62, 0xD2, 0x11,
    0xAF, 0x8F, 0x00, 0xC0, 0x4F, 0x8E, 0xEB, 0x97
};
static const Uint8 W64GUID_FMT[16] = {
    0x61, 0x66, 0x6D, 0x72, 0x35, 0x62, 0xD2, 0x11,
    0xAF, 0x8F, 0x00, 0xC0, 0x4F, 0x8E, 0xEB, 0x97
};
static const Uint8 W64GUID_DATA[16] = {
    0x63, 0x66, 0x6D, 0x72, 0x35, 0x62, 0xD2, 0x11,
    0xAF, 0x8F, 0x00, 0xC0, 0x4F, 0x8E, 0xEB, 0x97
};
static const Uint8 W64GUID_FACT[16] = {
    0x67, 0x66, 0x6D, 0x72, 0x35, 0x62, 0xD2, 0x11,
    0xAF, 0x8F, 0x00, 0xC0, 0x4F, 0x8E, 0xEB, 0x97
};
static const Uint8 W64GUID_JUNK[16] = {
    0x69, 0x66, 0x6D, 0x72, 0x35, 0x62, 0xD2, 0x11,
    0xAF, 0x8F, 0x00, 0xC0, 0x4F, 0x8E, 0xEB, 0x97
};

#define FOURCC_RIFF 0x46464952u
#define FOURCC_RF64 0x34364652u
#define FOURCC_BW64 0x34365742u
#define FOURCC_WAVE 0x45564157u
#define FOURCC_FMT  0x20746D66u
#define FOURCC_DATA 0x61746164u
#define FOURCC_FACT 0x74636166u
#define FOURCC_DS64 0x34367364u

#define WAVE_FORMAT_PCM        0x0001
#define WAVE_FORMAT_MS_ADPCM   0x0002
#define WAVE_FORMAT_IEEE_FLOAT 0x0003
#define WAVE_FORMAT_ALAW       0x0006
#define WAVE_FORMAT_MULAW      0x0007
#define WAVE_FORMAT_IMA_ADPCM  0x0011
#define WAVE_FORMAT_EXTENSIBLE 0xFFFE

// Standard WAVEFORMATEXTENSIBLE sub-format GUID for PCM.
static const Uint8 SUBTYPE_PCM_GUID[16] = {
    0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x10, 0x00,
    0x80, 0x00, 0x00, 0xAA, 0x00, 0x38, 0x9B, 0x71
};

typedef enum WaveContainerKind
{
    CONTAINER_KIND_RIFF,
    CONTAINER_KIND_RF64,
    CONTAINER_KIND_BW64,
    CONTAINER_KIND_WAVE64
} WaveContainerKind;

typedef struct WaveFmtSpec
{
    Uint16 formattag;
    Uint16 channels;
    Uint32 frequency;
    Uint16 blockalign;
    Uint16 bitspersample;
    bool extensible;
    Uint32 channelmask;
} WaveFmtSpec;

// Appends a RIFF-family chunk (FOURCC + 32-bit size + data + pad byte).
static void BuildRiffChunk(WaveBuilder *b, Uint32 fourcc, const Uint8 *data, Uint32 len)
{
    WaveBuilderFourCC(b, fourcc);
    WaveBuilderU32(b, len);
    if (len > 0) {
        WaveBuilderPut(b, data, len);
    }
    if (len & 1) {
        WaveBuilderU8(b, 0);
    }
}

// Appends a Sony Wave64 chunk (GUID + 64-bit size + data, 8-byte aligned).
static void BuildWave64Chunk(WaveBuilder *b, const Uint8 guid[16], const Uint8 *data, Uint64 datalen)
{
    WaveBuilderPut(b, guid, 16);
    // The size field includes the 24-byte chunk header.
    WaveBuilderU64(b, datalen + 24);
    if (datalen > 0) {
        WaveBuilderPut(b, data, (size_t)datalen);
    }
    WaveBuilderPad(b, 8);
}

static void BuildFmtBody(WaveBuilder *b, const WaveFmtSpec *spec)
{
    Uint16 formattag = spec->extensible ? WAVE_FORMAT_EXTENSIBLE : spec->formattag;
    Uint32 byterate = spec->frequency * spec->blockalign;

    WaveBuilderU16(b, formattag);
    WaveBuilderU16(b, spec->channels);
    WaveBuilderU32(b, spec->frequency);
    WaveBuilderU32(b, byterate);
    WaveBuilderU16(b, spec->blockalign);
    WaveBuilderU16(b, spec->bitspersample);

    if (spec->extensible) {
        // cbSize + valid bits + channel mask + sub-format GUID.
        WaveBuilderU16(b, 22);
        WaveBuilderU16(b, spec->bitspersample);
        WaveBuilderU32(b, spec->channelmask);
        WaveBuilderPut(b, SUBTYPE_PCM_GUID, 16);
    }
}

// On-disk size of a RIFF-family chunk, including its header and pad byte.
static size_t RiffChunkWireSize(size_t datalen)
{
    return 8 + datalen + (datalen & 1);
}

// Builds a complete WAVE file in the requested container.
static bool BuildWaveFile(WaveBuilder *b, WaveContainerKind kind, const WaveFmtSpec *fmt,
                          const Uint8 *data, Uint32 datalen, Uint32 sampleframes, bool force_placeholder)
{
    WaveBuilder fmtbody;
    WaveBuilder ds64body;
    Uint64 rifflen64;

    WaveBuilderInit(&fmtbody);
    BuildFmtBody(&fmtbody, fmt);
    if (!fmtbody.ok) {
        WaveBuilderFree(&fmtbody);
        return false;
    }

    if (kind == CONTAINER_KIND_WAVE64) {
        WaveBuilder chunks;
        WaveBuilderInit(&chunks);
        BuildWave64Chunk(&chunks, W64GUID_FMT, fmtbody.data, fmtbody.size);
        if (sampleframes > 0) {
            WaveBuilder factbody;
            WaveBuilderInit(&factbody);
            WaveBuilderU32(&factbody, sampleframes);
            BuildWave64Chunk(&chunks, W64GUID_FACT, factbody.data, factbody.size);
            WaveBuilderFree(&factbody);
        }
        BuildWave64Chunk(&chunks, W64GUID_DATA, data, datalen);
        if (!chunks.ok) {
            WaveBuilderFree(&chunks);
            WaveBuilderFree(&fmtbody);
            return false;
        }

        // RIFF GUID + size (includes the 24-byte RIFF header) + WAVE GUID + chunks.
        WaveBuilderPut(b, W64GUID_RIFF, 16);
        WaveBuilderU64(b, (Uint64)chunks.size + 40);
        WaveBuilderPut(b, W64GUID_WAVE, 16);
        WaveBuilderPut(b, chunks.data, chunks.size);
        WaveBuilderFree(&chunks);
    } else {
        WaveBuilder chunks;
        bool rf64 = (kind == CONTAINER_KIND_RF64 || kind == CONTAINER_KIND_BW64);
        bool needfact = (sampleframes > 0 && (rf64 || fmt->formattag != WAVE_FORMAT_PCM));
        size_t fmtwire = RiffChunkWireSize(fmtbody.size);
        size_t factwire = needfact ? RiffChunkWireSize(4) : 0;
        size_t datawire = RiffChunkWireSize(datalen);
        WaveBuilderInit(&chunks);

        if (rf64) {
            // ds64 body is 28 bytes (three 64-bit sizes + table length).
            size_t ds64wire = RiffChunkWireSize(28);
            // The RIFF size counts the WAVE form type plus every chunk.
            rifflen64 = 4 + ds64wire + fmtwire + factwire + datawire;
            WaveBuilderInit(&ds64body);
            WaveBuilderU64(&ds64body, rifflen64);
            WaveBuilderU64(&ds64body, datalen);
            WaveBuilderU64(&ds64body, sampleframes);
            WaveBuilderU32(&ds64body, 0);
            BuildRiffChunk(&chunks, FOURCC_DS64, ds64body.data, (Uint32)ds64body.size);
            WaveBuilderFree(&ds64body);
        }

        BuildRiffChunk(&chunks, FOURCC_FMT, fmtbody.data, (Uint32)fmtbody.size);
        if (needfact) {
            Uint8 factraw[4];
            factraw[0] = (Uint8)(sampleframes & 0xff);
            factraw[1] = (Uint8)((sampleframes >> 8) & 0xff);
            factraw[2] = (Uint8)((sampleframes >> 16) & 0xff);
            factraw[3] = (Uint8)((sampleframes >> 24) & 0xff);
            BuildRiffChunk(&chunks, FOURCC_FACT, factraw, 4);
        }
        BuildRiffChunk(&chunks, FOURCC_DATA, data, datalen);
        if (!chunks.ok) {
            WaveBuilderFree(&chunks);
            WaveBuilderFree(&fmtbody);
            return false;
        }

        WaveBuilderFourCC(b, rf64 ? (kind == CONTAINER_KIND_BW64 ? FOURCC_BW64 : FOURCC_RF64) : FOURCC_RIFF);
        if (rf64 && force_placeholder) {
            WaveBuilderU32(b, SDL_MAX_UINT32);
        } else {
            // The size counts the WAVE form type plus all chunks.
            WaveBuilderU32(b, (Uint32)(chunks.size + 4));
        }
        WaveBuilderFourCC(b, FOURCC_WAVE);
        WaveBuilderPut(b, chunks.data, chunks.size);
        WaveBuilderFree(&chunks);
    }

    WaveBuilderFree(&fmtbody);
    return b->ok;
}

// A small mono 16-bit PCM payload, 4 frames.
static const Uint16 TEST_PCM_SAMPLES[4] = { 0x0102, 0x0304, 0x8000, 0xFFFE };

static bool LoadFromBuilder(const WaveBuilder *b, bool use64, SDL_AudioSpec *spec,
                            Uint8 **out_buf, Uint64 *out_len)
{
    SDL_IOStream *stream = SDL_IOFromConstMem(b->data, b->size);
    if (!stream) {
        return false;
    }
    if (use64) {
        return SDL_LoadWAV_IO64(stream, true, spec, out_buf, out_len);
    } else {
        Uint32 len32 = 0;
        bool ret = SDL_LoadWAV_IO(stream, true, spec, out_buf, &len32);
        *out_len = len32;
        return ret;
    }
}

/* A memory-backed stream that can report a size larger than its buffer. */
typedef struct FakeLargeStream
{
    const Uint8 *data;
    size_t realsize;
    Sint64 reportedsize;
    Sint64 pos;
} FakeLargeStream;

static Sint64 SDLCALL FakeLargeSize(void *userdata)
{
    return ((FakeLargeStream *)userdata)->reportedsize;
}

static Sint64 SDLCALL FakeLargeSeek(void *userdata, Sint64 offset, SDL_IOWhence whence)
{
    FakeLargeStream *s = (FakeLargeStream *)userdata;
    Sint64 newpos = s->pos;

    switch (whence) {
    case SDL_IO_SEEK_SET:
        newpos = offset;
        break;
    case SDL_IO_SEEK_CUR:
        newpos += offset;
        break;
    case SDL_IO_SEEK_END:
        newpos = s->reportedsize + offset;
        break;
    default:
        return -1;
    }
    if (newpos < 0) {
        return -1;
    }
    s->pos = newpos;
    return newpos;
}

static size_t SDLCALL FakeLargeRead(void *userdata, void *ptr, size_t size, SDL_IOStatus *status)
{
    FakeLargeStream *s = (FakeLargeStream *)userdata;
    size_t available = (s->pos < (Sint64)s->realsize) ? (size_t)((Sint64)s->realsize - s->pos) : 0;
    size_t n = size < available ? size : available;

    if (n > 0) {
        SDL_memcpy(ptr, s->data + s->pos, n);
        s->pos += (Sint64)n;
    }
    if (n < size && status) {
        *status = SDL_IO_STATUS_ERROR;
    }
    return n;
}

static bool SDLCALL FakeLargeClose(void *userdata)
{
    (void)userdata;
    return true;
}

static const WaveFmtSpec FMT_PCM16_MONO = {
    WAVE_FORMAT_PCM, 1, 48000, 2, 16, false, 0
};
static const WaveFmtSpec FMT_PCM16_MONO_EXT = {
    WAVE_FORMAT_PCM, 1, 48000, 2, 16, true, 0x4
};
static const WaveFmtSpec FMT_FLOAT32_MONO = {
    WAVE_FORMAT_IEEE_FLOAT, 1, 48000, 4, 32, false, 0
};
static const WaveFmtSpec FMT_ALAW_MONO = {
    WAVE_FORMAT_ALAW, 1, 8000, 1, 8, false, 0
};
static const WaveFmtSpec FMT_MULAW_MONO = {
    WAVE_FORMAT_MULAW, 1, 8000, 1, 8, false, 0
};
static const WaveFmtSpec FMT_IMA_ADPCM_MONO = {
    WAVE_FORMAT_IMA_ADPCM, 1, 8000, 8, 4, false, 0
};

/* ================= Test cases ================== */

// Baseline: ordinary RIFF WAVE still loads through both entry points.
static int audio_loadWAV_RIFF(void *arg)
{
    WaveBuilder b;
    SDL_AudioSpec spec;
    Uint8 *buf = NULL;
    Uint64 len = 0;
    const Uint8 *raw = (const Uint8 *)TEST_PCM_SAMPLES;
    Uint32 rawlen = sizeof(TEST_PCM_SAMPLES);

    WaveBuilderInit(&b);
    SDLTest_AssertCheck(BuildWaveFile(&b, CONTAINER_KIND_RIFF, &FMT_PCM16_MONO, raw, rawlen, 0, false),
                        "Build RIFF PCM file in memory");

    SDLTest_AssertCheck(LoadFromBuilder(&b, true, &spec, &buf, &len), "Load RIFF via SDL_LoadWAV_IO64");
    if (buf) {
        SDLTest_AssertCheck(len == rawlen, "Verify 64-bit length %" SDL_PRIu64 " == %u", len, rawlen);
        SDLTest_AssertCheck(SDL_memcmp(buf, raw, rawlen) == 0, "Verify PCM payload is returned unchanged");
        SDLTest_AssertCheck(spec.freq == 48000 && spec.channels == 1 && spec.format == SDL_AUDIO_S16LE,
                            "Verify audio spec (freq/channels/format)");
        SDL_free(buf);
    }

    buf = NULL;
    len = 0;
    SDLTest_AssertCheck(LoadFromBuilder(&b, false, &spec, &buf, &len), "Load RIFF via SDL_LoadWAV_IO");
    if (buf) {
        SDLTest_AssertCheck(len == rawlen, "Verify 32-bit length %" SDL_PRIu64 " == %u", len, rawlen);
        SDL_free(buf);
    }
    WaveBuilderFree(&b);

    // A stream that starts directly with "WAVE" (no RIFF header) must still load.
    {
        WaveBuilder h;
        WaveBuilderInit(&h);
        {
            WaveBuilder fmtbody;
            WaveBuilderInit(&fmtbody);
            BuildFmtBody(&fmtbody, &FMT_PCM16_MONO);
            BuildRiffChunk(&h, FOURCC_FMT, fmtbody.data, (Uint32)fmtbody.size);
            BuildRiffChunk(&h, FOURCC_DATA, raw, rawlen);
            WaveBuilderFree(&fmtbody);
        }
        // Prepend the bare WAVE form type in place of a RIFF header.
        {
            size_t total = h.size + 4;
            Uint8 *moved = (Uint8 *)SDL_malloc(total);
            SDLTest_AssertCheck(moved != NULL, "Allocate headerless WAVE buffer");
            if (moved) {
                Uint32 wave = FOURCC_WAVE;
                Uint8 wavebytes[4] = {
                    (Uint8)(wave & 0xff), (Uint8)((wave >> 8) & 0xff),
                    (Uint8)((wave >> 16) & 0xff), (Uint8)((wave >> 24) & 0xff)
                };
                SDL_memcpy(moved, wavebytes, 4);
                SDL_memcpy(moved + 4, h.data, h.size);
                SDL_free(h.data);
                h.data = moved;
                h.size = total;
                h.capacity = total;
            }
        }
        buf = NULL;
        len = 0;
        SDLTest_AssertCheck(LoadFromBuilder(&h, true, &spec, &buf, &len),
                            "Load a WAVE stream without a RIFF header");
        if (buf) {
            SDLTest_AssertCheck(len == rawlen, "Verify headerless WAVE length");
            SDL_free(buf);
        }
        WaveBuilderFree(&h);
    }
    return TEST_COMPLETED;
}

// RF64 with 0xFFFFFFFF placeholders must use the ds64 sizes.
static int audio_loadWAV_RF64(void *arg)
{
    WaveBuilder b;
    SDL_AudioSpec spec;
    Uint8 *buf = NULL;
    Uint64 len = 0;
    const Uint8 *raw = (const Uint8 *)TEST_PCM_SAMPLES;
    Uint32 rawlen = sizeof(TEST_PCM_SAMPLES);

    WaveBuilderInit(&b);
    SDLTest_AssertCheck(BuildWaveFile(&b, CONTAINER_KIND_RF64, &FMT_PCM16_MONO, raw, rawlen, 4, true),
                        "Build RF64 PCM file with placeholder sizes");

    SDLTest_AssertCheck(LoadFromBuilder(&b, true, &spec, &buf, &len), "Load RF64 via SDL_LoadWAV_IO64");
    if (buf) {
        SDLTest_AssertCheck(len == rawlen, "Verify RF64 data length from ds64: %" SDL_PRIu64 " == %u", len, rawlen);
        SDLTest_AssertCheck(SDL_memcmp(buf, raw, rawlen) == 0, "Verify RF64 payload");
        SDL_free(buf);
    }
    WaveBuilderFree(&b);
    return TEST_COMPLETED;
}

// BW64 uses the same ds64 mechanism as RF64.
static int audio_loadWAV_BW64(void *arg)
{
    WaveBuilder b;
    SDL_AudioSpec spec;
    Uint8 *buf = NULL;
    Uint64 len = 0;
    const Uint8 *raw = (const Uint8 *)TEST_PCM_SAMPLES;
    Uint32 rawlen = sizeof(TEST_PCM_SAMPLES);

    WaveBuilderInit(&b);
    SDLTest_AssertCheck(BuildWaveFile(&b, CONTAINER_KIND_BW64, &FMT_PCM16_MONO, raw, rawlen, 4, true),
                        "Build BW64 PCM file with placeholder sizes");

    SDLTest_AssertCheck(LoadFromBuilder(&b, true, &spec, &buf, &len), "Load BW64 via SDL_LoadWAV_IO64");
    if (buf) {
        SDLTest_AssertCheck(len == rawlen, "Verify BW64 data length from ds64: %" SDL_PRIu64 " == %u", len, rawlen);
        SDLTest_AssertCheck(SDL_memcmp(buf, raw, rawlen) == 0, "Verify BW64 payload");
        SDL_free(buf);
    }
    WaveBuilderFree(&b);
    return TEST_COMPLETED;
}

// RF64 without placeholders: real 32-bit sizes must agree with ds64.
static int audio_loadWAV_RF64NoPlaceholder(void *arg)
{
    WaveBuilder b;
    SDL_AudioSpec spec;
    Uint8 *buf = NULL;
    Uint64 len = 0;
    const Uint8 *raw = (const Uint8 *)TEST_PCM_SAMPLES;
    Uint32 rawlen = sizeof(TEST_PCM_SAMPLES);

    WaveBuilderInit(&b);
    SDLTest_AssertCheck(BuildWaveFile(&b, CONTAINER_KIND_RF64, &FMT_PCM16_MONO, raw, rawlen, 4, false),
                        "Build RF64 file with real 32-bit sizes plus ds64");

    SDLTest_AssertCheck(LoadFromBuilder(&b, true, &spec, &buf, &len),
                        "Load RF64 with matching real/ds64 sizes");
    if (buf) {
        SDLTest_AssertCheck(len == rawlen, "Verify RF64 fallback length: %" SDL_PRIu64 " == %u", len, rawlen);
        SDL_free(buf);
    }
    WaveBuilderFree(&b);
    return TEST_COMPLETED;
}

// Sony Wave64 with GUID chunks and 8-byte alignment, including an odd data size.
static int audio_loadWAV_Wave64(void *arg)
{
    static const Uint8 odddata[7] = { 1, 2, 3, 4, 5, 6, 7 };
    WaveBuilder b;
    SDL_AudioSpec spec;
    Uint8 *buf = NULL;
    Uint64 len = 0;

    WaveBuilderInit(&b);
    // 8-bit PCM, block align 1: an odd payload forces alignment padding.
    WaveFmtSpec pcm8 = { WAVE_FORMAT_PCM, 1, 22050, 1, 8, false, 0 };
    SDLTest_AssertCheck(BuildWaveFile(&b, CONTAINER_KIND_WAVE64, &pcm8, odddata, sizeof(odddata),
                                      sizeof(odddata), false),
                        "Build Wave64 PCM8 file with odd data size");

    SDLTest_AssertCheck(LoadFromBuilder(&b, true, &spec, &buf, &len), "Load Wave64 via SDL_LoadWAV_IO64");
    if (buf) {
        SDLTest_AssertCheck(len == sizeof(odddata), "Verify Wave64 data length: %" SDL_PRIu64 " == %u",
                            len, (unsigned)sizeof(odddata));
        SDLTest_AssertCheck(SDL_memcmp(buf, odddata, sizeof(odddata)) == 0, "Verify Wave64 payload");
        SDLTest_AssertCheck(spec.format == SDL_AUDIO_U8, "Verify Wave64 U8 format");
        SDL_free(buf);
    }

    // Wave64 with an unknown GUID chunk (JUNK) interleaved before the data.
    {
        WaveBuilder b2;
        static const Uint8 junk[20] = { 0xAB };
        WaveBuilderInit(&b2);
        SDLTest_AssertCheck(BuildWaveFile(&b2, CONTAINER_KIND_WAVE64, &FMT_PCM16_MONO,
                                          (const Uint8 *)TEST_PCM_SAMPLES, sizeof(TEST_PCM_SAMPLES), 4, false),
                            "Build second Wave64 file");
        // Insert a JUNK GUID chunk right after the WAVE GUID (offset 40).
        {
            WaveBuilder junkchunk;
            WaveBuilderInit(&junkchunk);
            BuildWave64Chunk(&junkchunk, W64GUID_JUNK, junk, sizeof(junk));
            if (junkchunk.ok) {
                size_t head = 40;
                size_t total = b2.size + junkchunk.size;
                Uint8 *moved = (Uint8 *)SDL_malloc(total);
                SDLTest_AssertCheck(moved != NULL, "Allocate buffer for JUNK insertion");
                if (moved) {
                    Uint64 newriffsize;
                    int i;

                    SDL_memcpy(moved, b2.data, head);
                    SDL_memcpy(moved + head, junkchunk.data, junkchunk.size);
                    SDL_memcpy(moved + head + junkchunk.size, b2.data + head, b2.size - head);
                    SDL_free(b2.data);
                    b2.data = moved;
                    b2.size = total;
                    b2.capacity = total;

                    // The Wave64 RIFF size field is the whole file size.
                    newriffsize = (Uint64)total;
                    for (i = 0; i < 8; i++) {
                        b2.data[16 + i] = (Uint8)((newriffsize >> (i * 8)) & 0xff);
                    }
                }
            }
            WaveBuilderFree(&junkchunk);
        }
        buf = NULL;
        len = 0;
        SDLTest_AssertCheck(LoadFromBuilder(&b2, true, &spec, &buf, &len),
                            "Load Wave64 containing an unknown (JUNK) GUID chunk");
        if (buf) {
            SDLTest_AssertCheck(len == sizeof(TEST_PCM_SAMPLES), "Verify Wave64 length after JUNK chunk");
            SDL_free(buf);
        }
        WaveBuilderFree(&b2);
    }

    WaveBuilderFree(&b);
    return TEST_COMPLETED;
}

// WAVEFORMATEXTENSIBLE sub-format GUID decoding in RIFF and Wave64.
static int audio_loadWAV_Extensible(void *arg)
{
    static const int kinds[2] = { CONTAINER_KIND_RIFF, CONTAINER_KIND_WAVE64 };
    const Uint8 *raw = (const Uint8 *)TEST_PCM_SAMPLES;
    Uint32 rawlen = sizeof(TEST_PCM_SAMPLES);
    int k;

    for (k = 0; k < 2; k++) {
        WaveBuilder b;
        SDL_AudioSpec spec;
        Uint8 *buf = NULL;
        Uint64 len = 0;

        WaveBuilderInit(&b);
        SDLTest_AssertCheck(BuildWaveFile(&b, (WaveContainerKind)kinds[k], &FMT_PCM16_MONO_EXT,
                                          raw, rawlen, 4, false),
                            "Build extensible PCM file (container %d)", kinds[k]);

        SDLTest_AssertCheck(LoadFromBuilder(&b, true, &spec, &buf, &len),
                            "Load extensible PCM (container %d)", kinds[k]);
        if (buf) {
            SDLTest_AssertCheck(len == rawlen && SDL_memcmp(buf, raw, rawlen) == 0,
                                "Verify extensible PCM payload (container %d)", kinds[k]);
            SDLTest_AssertCheck(spec.format == SDL_AUDIO_S16LE, "Verify extensible S16LE format");
            SDL_free(buf);
        }
        WaveBuilderFree(&b);
    }
    return TEST_COMPLETED;
}

// IMA ADPCM: one 8-byte block decodes to 9 mono frames (16-bit output).
static int audio_loadWAV_IMAADPCM(void *arg)
{
    static const Uint8 adpcm_block[8] = {
        0x00, 0x00, // first sample = 0
        0x00, 0x00, // step index 0, reserved 0
        0x12, 0x34, 0x56, 0x78
    };
    static const int kinds[4] = {
        CONTAINER_KIND_RIFF, CONTAINER_KIND_RF64, CONTAINER_KIND_BW64, CONTAINER_KIND_WAVE64
    };
    int k;

    for (k = 0; k < 4; k++) {
        WaveBuilder b;
        SDL_AudioSpec spec;
        Uint8 *buf = NULL;
        Uint64 len = 0;

        WaveBuilderInit(&b);
        SDLTest_AssertCheck(BuildWaveFile(&b, (WaveContainerKind)kinds[k], &FMT_IMA_ADPCM_MONO,
                                          adpcm_block, sizeof(adpcm_block), 9,
                                          kinds[k] == CONTAINER_KIND_RF64 ||
                                          kinds[k] == CONTAINER_KIND_BW64),
                            "Build IMA ADPCM file (container %d)", kinds[k]);

        SDLTest_AssertCheck(LoadFromBuilder(&b, true, &spec, &buf, &len),
                            "Load IMA ADPCM (container %d)", kinds[k]);
        if (buf) {
            SDLTest_AssertCheck(spec.format == SDL_AUDIO_S16, "Verify IMA ADPCM decodes to S16");
            SDLTest_AssertCheck(len == 9 * sizeof(Sint16),
                                "Verify IMA ADPCM decoded length %" SDL_PRIu64 " == %d", len,
                                (int)(9 * sizeof(Sint16)));
            SDL_free(buf);
        }
        WaveBuilderFree(&b);
    }
    return TEST_COMPLETED;
}

// A-law and mu-law expand to 16-bit.
static int audio_loadWAV_Law(void *arg)
{
    static const Uint8 law_payload[4] = { 0x00, 0x7F, 0x80, 0xD5 };
    const WaveFmtSpec *specs[2] = { &FMT_ALAW_MONO, &FMT_MULAW_MONO };
    int s;

    for (s = 0; s < 2; s++) {
        WaveBuilder b;
        SDL_AudioSpec spec;
        Uint8 *buf = NULL;
        Uint64 len = 0;

        WaveBuilderInit(&b);
        SDLTest_AssertCheck(BuildWaveFile(&b, CONTAINER_KIND_RIFF, specs[s], law_payload,
                                          sizeof(law_payload), sizeof(law_payload), false),
                            "Build companded WAVE (kind %d)", s);

        SDLTest_AssertCheck(LoadFromBuilder(&b, true, &spec, &buf, &len),
                            "Load companded WAVE (kind %d)", s);
        if (buf) {
            SDLTest_AssertCheck(spec.format == SDL_AUDIO_S16, "Verify law output is S16");
            SDLTest_AssertCheck(len == sizeof(law_payload) * sizeof(Sint16),
                                "Verify expanded law length %" SDL_PRIu64, len);
            SDL_free(buf);
        }
        WaveBuilderFree(&b);
    }
    return TEST_COMPLETED;
}

// 32-bit float payload round-trips unchanged.
static int audio_loadWAV_Float(void *arg)
{
    static const float floats[3] = { 0.0f, 0.5f, -0.25f };
    WaveBuilder b;
    SDL_AudioSpec spec;
    Uint8 *buf = NULL;
    Uint64 len = 0;

    WaveBuilderInit(&b);
    SDLTest_AssertCheck(BuildWaveFile(&b, CONTAINER_KIND_WAVE64, &FMT_FLOAT32_MONO,
                                      (const Uint8 *)floats, sizeof(floats), 3, false),
                        "Build Wave64 float file");

    SDLTest_AssertCheck(LoadFromBuilder(&b, true, &spec, &buf, &len), "Load Wave64 float");
    if (buf) {
        SDLTest_AssertCheck(spec.format == SDL_AUDIO_F32LE, "Verify F32LE format");
        SDLTest_AssertCheck(len == sizeof(floats), "Verify float length %" SDL_PRIu64, len);
        SDLTest_AssertCheck(SDL_memcmp(buf, floats, sizeof(floats)) == 0, "Verify float payload");
        SDL_free(buf);
    }
    WaveBuilderFree(&b);
    return TEST_COMPLETED;
}

// Corrupt and hostile inputs must fail safely without crashing.
static int audio_loadWAV_Corrupt(void *arg)
{
    const Uint8 *raw = (const Uint8 *)TEST_PCM_SAMPLES;
    Uint32 rawlen = sizeof(TEST_PCM_SAMPLES);

    // 1. Truncated file: cut the data section short.
    {
        WaveBuilder b;
        SDL_AudioSpec spec;
        Uint8 *buf = NULL;
        Uint64 len = 0;
        SDL_IOStream *stream;

        WaveBuilderInit(&b);
        SDLTest_AssertCheck(BuildWaveFile(&b, CONTAINER_KIND_RIFF, &FMT_PCM16_MONO, raw, rawlen, 0, false),
                            "Build RIFF file for truncation test");
        SDL_SetHint(SDL_HINT_WAVE_TRUNCATION, "strict");
        stream = SDL_IOFromConstMem(b.data, b.size - 3);
        SDLTest_AssertCheck(stream != NULL, "Create truncated memory stream");
        if (stream) {
            SDLTest_AssertCheck(!SDL_LoadWAV_IO64(stream, true, &spec, &buf, &len),
                                "Strict mode rejects truncated data chunk");
            SDL_free(buf);
        }
        SDL_ResetHint(SDL_HINT_WAVE_TRUNCATION);
        WaveBuilderFree(&b);
    }

    // 2. RF64 with placeholder but no ds64 chunk must fail.
    {
        WaveBuilder b;
        SDL_AudioSpec spec;
        Uint8 *buf = NULL;
        Uint64 len = 0;

        WaveBuilderInit(&b);
        SDLTest_AssertCheck(BuildWaveFile(&b, CONTAINER_KIND_RF64, &FMT_PCM16_MONO, raw, rawlen, 4, true),
                            "Build RF64 placeholder file");
        // Remove the ds64 chunk (header 8 + body 28 = 36 bytes at offset 12).
        {
            const size_t ds64_total = 36;
            size_t newsize = b.size - ds64_total;
            SDL_memmove(b.data + 12, b.data + 12 + ds64_total, b.size - (12 + ds64_total));
            b.size = newsize;
        }
        SDLTest_AssertCheck(!LoadFromBuilder(&b, true, &spec, &buf, &len),
                            "RF64 placeholder without ds64 is rejected");
        SDL_free(buf);
        WaveBuilderFree(&b);
    }

    // 3. ds64 data size that disagrees with the real data chunk size must fail.
    {
        WaveBuilder b;
        SDL_AudioSpec spec;
        Uint8 *buf = NULL;
        Uint64 len = 0;

        WaveBuilderInit(&b);
        SDLTest_AssertCheck(BuildWaveFile(&b, CONTAINER_KIND_RF64, &FMT_PCM16_MONO, raw, rawlen, 4, false),
                            "Build RF64 file with real sizes for mismatch test");
        // Layout: 12-byte RF64 header + 8-byte ds64 chunk header, then the
        // ds64 body: 8-byte riff size, 8-byte data size, ...
        b.data[32] = (Uint8)(b.data[32] ^ 0x01);
        SDLTest_AssertCheck(!LoadFromBuilder(&b, true, &spec, &buf, &len),
                            "Mismatching ds64 data size is rejected");
        SDL_free(buf);
        WaveBuilderFree(&b);
    }

    // 4. Forged huge data length in a tiny stream is clamped, not allocated.
    {
        WaveBuilder b;
        SDL_AudioSpec spec;
        Uint8 *buf = NULL;
        Uint64 len = 0;

        WaveBuilderInit(&b);
        SDLTest_AssertCheck(BuildWaveFile(&b, CONTAINER_KIND_RIFF, &FMT_PCM16_MONO, raw, rawlen, 0, false),
                            "Build RIFF file for oversized-length test");
        // Locate the data chunk size field and set it to a huge value.
        {
            size_t p = 12;
            bool patched = false;
            while (p + 8 <= b.size) {
                Uint32 c = (Uint32)b.data[p] | ((Uint32)b.data[p + 1] << 8) |
                           ((Uint32)b.data[p + 2] << 16) | ((Uint32)b.data[p + 3] << 24);
                Uint32 clen = (Uint32)b.data[p + 4] | ((Uint32)b.data[p + 5] << 8) |
                              ((Uint32)b.data[p + 6] << 16) | ((Uint32)b.data[p + 7] << 24);
                if (c == FOURCC_DATA) {
                    Uint64 huge = SDL_MAX_UINT64;
                    int i;
                    for (i = 0; i < 8; i++) {
                        b.data[p + 4 + i] = (Uint8)((huge >> (i * 8)) & 0xff);
                    }
                    patched = true;
                    break;
                }
                p += 8 + clen + (clen & 1);
            }
            SDLTest_AssertCheck(patched, "Find and patch data chunk length field");
        }
        SDLTest_AssertCheck(LoadFromBuilder(&b, true, &spec, &buf, &len),
                            "Huge forged data length is clamped to the stream size");
        if (buf) {
            SDLTest_AssertCheck(len <= rawlen, "Clamped length %" SDL_PRIu64 " is bounded", len);
            SDL_free(buf);
        }
        WaveBuilderFree(&b);
    }

    // 5. Invalid block alignment (zero) in an IMA ADPCM header is rejected.
    {
        WaveBuilder b;
        SDL_AudioSpec spec;
        Uint8 *buf = NULL;
        Uint64 len = 0;
        WaveFmtSpec bad = FMT_IMA_ADPCM_MONO;
        bad.blockalign = 0;

        WaveBuilderInit(&b);
        SDLTest_AssertCheck(BuildWaveFile(&b, CONTAINER_KIND_RIFF, &bad, raw, rawlen, 9, false),
                            "Build ADPCM file with zero block alignment");
        SDLTest_AssertCheck(!LoadFromBuilder(&b, true, &spec, &buf, &len),
                            "Zero ADPCM block alignment is rejected");
        SDL_free(buf);
        WaveBuilderFree(&b);
    }

    // 6. Truncated Wave64 header (too short to hold GUID + size + WAVE GUID).
    {
        static const Uint8 tiny[8] = { 0x66, 0x66, 0x6D, 0x72, 0x35, 0x62, 0xD2, 0x11 };
        SDL_IOStream *stream = SDL_IOFromConstMem(tiny, sizeof(tiny));
        SDL_AudioSpec spec;
        Uint8 *buf = NULL;
        Uint64 len = 0;
        SDLTest_AssertCheck(stream != NULL, "Create truncated Wave64-like stream");
        if (stream) {
            SDLTest_AssertCheck(!SDL_LoadWAV_IO64(stream, true, &spec, &buf, &len),
                                "Truncated Wave64 header is rejected");
            SDL_free(buf);
        }
    }

    // 6b. Wave64 prefixes between 24 and 39 bytes match the RIFF GUID but cannot
    //     contain the WAVE GUID. They must be rejected without reading past the buffer.
    {
        static const size_t trunc_lengths[3] = { 24, 32, 39 };
        size_t t;
        for (t = 0; t < SDL_arraysize(trunc_lengths); t++) {
            Uint8 prefix[40];
            SDL_IOStream *stream;
            SDL_AudioSpec spec;
            Uint8 *buf = NULL;
            Uint64 len = 0;

            SDL_memcpy(prefix, W64GUID_RIFF, 16);
            // A plausible 64-bit size in bytes 16..23.
            {
                Uint64 size = 64;
                int i;
                for (i = 0; i < 8; i++) {
                    prefix[16 + i] = (Uint8)((size >> (i * 8)) & 0xff);
                }
            }
            stream = SDL_IOFromConstMem(prefix, trunc_lengths[t]);
            SDLTest_AssertCheck(stream != NULL, "Create %u-byte truncated Wave64 stream",
                                (unsigned)trunc_lengths[t]);
            if (stream) {
                SDLTest_AssertCheck(!SDL_LoadWAV_IO64(stream, true, &spec, &buf, &len),
                                    "%u-byte truncated Wave64 stream is rejected",
                                    (unsigned)trunc_lengths[t]);
                SDL_free(buf);
            }
        }
    }

    // 7. Unknown RIFF form identifier is rejected.
    {
        WaveBuilder b;
        SDL_AudioSpec spec;
        Uint8 *buf = NULL;
        Uint64 len = 0;
        WaveBuilderInit(&b);
        WaveBuilderFourCC(&b, 0x44454144); // "DAED"
        WaveBuilderU32(&b, 16);
        WaveBuilderFourCC(&b, FOURCC_WAVE);
        SDLTest_AssertCheck(!LoadFromBuilder(&b, true, &spec, &buf, &len),
                            "Unknown container identifier is rejected");
        SDL_free(buf);
        WaveBuilderFree(&b);
    }

    return TEST_COMPLETED;
}

// Hostile size fields must not trigger enormous allocations.
static int audio_loadWAV_OverSized(void *arg)
{
    const Uint8 *raw = (const Uint8 *)TEST_PCM_SAMPLES;
    Uint32 rawlen = sizeof(TEST_PCM_SAMPLES);
    // Larger than any addressable buffer on both 32-bit and 64-bit hosts.
    const Uint64 huge = (Uint64)SIZE_MAX + 16;

    // RF64 whose ds64 declares a data size beyond the addressable range.
    {
        WaveBuilder b;
        SDL_AudioSpec spec;
        Uint8 *buf = NULL;
        Uint64 len = 0;
        FakeLargeStream fake;
        SDL_IOStreamInterface iface;
        SDL_IOStream *stream;

        WaveBuilderInit(&b);
        SDLTest_AssertCheck(BuildWaveFile(&b, CONTAINER_KIND_RF64, &FMT_PCM16_MONO, raw, rawlen, 4, true),
                            "Build RF64 placeholder file for oversize test");

        // Patch the ds64 data-size field (offset 32) to an enormous value.
        {
            int i;
            for (i = 0; i < 8; i++) {
                b.data[32 + i] = (Uint8)((huge >> (i * 8)) & 0xff);
            }
        }

        // Report a stream size large enough to satisfy the clamping logic,
        // but keep the actual buffer small so no real oversized allocation
        // is reachable before the size check rejects the file.
        fake.data = b.data;
        fake.realsize = b.size;
        fake.reportedsize = (Sint64)SIZE_MAX;
        fake.pos = 0;

        SDL_INIT_INTERFACE(&iface);
        iface.size = FakeLargeSize;
        iface.seek = FakeLargeSeek;
        iface.read = FakeLargeRead;
        iface.close = FakeLargeClose;
        stream = SDL_OpenIO(&iface, &fake);
        SDLTest_AssertCheck(stream != NULL, "Open fake-large IOStream");
        if (stream) {
            // The 64-bit entry must fail rather than attempt the allocation.
            SDLTest_AssertCheck(!SDL_LoadWAV_IO64(stream, true, &spec, &buf, &len),
                                "RF64 data beyond the addressable range is rejected");
            SDL_free(buf);
        }
        WaveBuilderFree(&b);
    }

    return TEST_COMPLETED;
}

/* ================= Test Case References ================== */

static const SDLTest_TestCaseReference wavTestRIFF = {
    audio_loadWAV_RIFF, "wav_loadRIFF", "Load a plain RIFF WAVE through both entry points", TEST_ENABLED
};
static const SDLTest_TestCaseReference wavTestRF64 = {
    audio_loadWAV_RF64, "wav_loadRF64", "Load an RF64 file using ds64 placeholder sizes", TEST_ENABLED
};
static const SDLTest_TestCaseReference wavTestBW64 = {
    audio_loadWAV_BW64, "wav_loadBW64", "Load a BW64 file using ds64 placeholder sizes", TEST_ENABLED
};
static const SDLTest_TestCaseReference wavTestRF64NoPlaceholder = {
    audio_loadWAV_RF64NoPlaceholder, "wav_loadRF64NoPlaceholder", "RF64 fallback rule with real 32-bit sizes", TEST_ENABLED
};
static const SDLTest_TestCaseReference wavTestWave64 = {
    audio_loadWAV_Wave64, "wav_loadWave64", "Load Sony Wave64 GUID chunks with 16-byte alignment", TEST_ENABLED
};
static const SDLTest_TestCaseReference wavTestExtensible = {
    audio_loadWAV_Extensible, "wav_loadExtensible", "Load WAVEFORMATEXTENSIBLE PCM in RIFF and Wave64", TEST_ENABLED
};
static const SDLTest_TestCaseReference wavTestIMAADPCM = {
    audio_loadWAV_IMAADPCM, "wav_loadIMAADPCM", "Load IMA ADPCM in all four containers", TEST_ENABLED
};
static const SDLTest_TestCaseReference wavTestLaw = {
    audio_loadWAV_Law, "wav_loadLaw", "Load A-law and mu-law audio", TEST_ENABLED
};
static const SDLTest_TestCaseReference wavTestFloat = {
    audio_loadWAV_Float, "wav_loadFloat", "Load 32-bit float audio in a Wave64 container", TEST_ENABLED
};
static const SDLTest_TestCaseReference wavTestCorrupt = {
    audio_loadWAV_Corrupt, "wav_loadCorrupt", "Reject truncated, mismatched and hostile inputs", TEST_ENABLED
};
static const SDLTest_TestCaseReference wavTestOverSized = {
    audio_loadWAV_OverSized, "wav_loadOverSized", "Reject data sizes beyond the addressable range", TEST_ENABLED
};

static const SDLTest_TestCaseReference *wavTests[] = {
    &wavTestRIFF, &wavTestRF64, &wavTestBW64, &wavTestRF64NoPlaceholder,
    &wavTestWave64, &wavTestExtensible, &wavTestIMAADPCM, &wavTestLaw,
    &wavTestFloat, &wavTestCorrupt, &wavTestOverSized, NULL
};

SDLTest_TestSuiteReference wavTestSuite = {
    "WAV",
    NULL,
    wavTests,
    NULL
};
