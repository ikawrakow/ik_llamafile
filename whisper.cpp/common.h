// -*- mode:c++;indent-tabs-mode:nil;c-basic-offset:4;tab-width:8;coding:utf-8 -*-
// vi: set et ft=cpp ts=4 sts=4 sw=4 fenc=utf-8 :vi

// Various helper functions and utilities

#pragma once

#include <string>
#include <vector>
#include <thread>
#include <fstream>

#define COMMON_SAMPLE_RATE 16000

//
// Audio utils
//

// Check if a buffer is a WAV audio file
bool is_wav_buffer(const std::string buf);

// Read WAV audio file and store the PCM data into pcmf32
// fname can be a buffer of WAV data instead of a filename
// The sample rate of the audio must be equal to COMMON_SAMPLE_RATE
// If stereo flag is set and the audio has 2 channels, the pcmf32s will contain 2 channel PCM
bool read_wav(
        const std::string & fname,
        std::vector<float> & pcmf32,
        std::vector<std::vector<float>> & pcmf32s,
        bool stereo);

// Write PCM data into WAV audio file
class wav_writer {
private:
    std::ofstream file;
    uint32_t dataSize = 0;
    std::string wav_filename;

    bool write_header(const uint32_t sample_rate,
                      const uint16_t bits_per_sample,
                      const uint16_t channels) {

        file.write("RIFF", 4);
        file.write("\0\0\0\0", 4);    // Placeholder for file size
        file.write("WAVE", 4);
        file.write("fmt ", 4);

        const uint32_t sub_chunk_size = 16;
        const uint16_t audio_format = 1;      // PCM format
        const uint32_t byte_rate = sample_rate * channels * bits_per_sample / 8;
        const uint16_t block_align = channels * bits_per_sample / 8;

        file.write(reinterpret_cast<const char *>(&sub_chunk_size), 4);
        file.write(reinterpret_cast<const char *>(&audio_format), 2);
        file.write(reinterpret_cast<const char *>(&channels), 2);
        file.write(reinterpret_cast<const char *>(&sample_rate), 4);
        file.write(reinterpret_cast<const char *>(&byte_rate), 4);
        file.write(reinterpret_cast<const char *>(&block_align), 2);
        file.write(reinterpret_cast<const char *>(&bits_per_sample), 2);
        file.write("data", 4);
        file.write("\0\0\0\0", 4);    // Placeholder for data size

        return true;
    }

    // It is assumed that PCM data is normalized to a range from -1 to 1
    bool write_audio(const float * data, size_t length) {
        for (size_t i = 0; i < length; ++i) {
            const int16_t intSample = data[i] * 32767;
            file.write(reinterpret_cast<const char *>(&intSample), sizeof(int16_t));
            dataSize += sizeof(int16_t);
        }
        if (file.is_open()) {
            file.seekp(4, std::ios::beg);
            uint32_t fileSize = 36 + dataSize;
            file.write(reinterpret_cast<char *>(&fileSize), 4);
            file.seekp(40, std::ios::beg);
            file.write(reinterpret_cast<char *>(&dataSize), 4);
            file.seekp(0, std::ios::end);
        }
        return true;
    }

    bool open_wav(const std::string & filename) {
        if (filename != wav_filename) {
            if (file.is_open()) {
                file.close();
            }
        }
        if (!file.is_open()) {
            file.open(filename, std::ios::binary);
            wav_filename = filename;
            dataSize = 0;
        }
        return file.is_open();
    }

public:
    bool open(const std::string & filename,
              const    uint32_t   sample_rate,
              const    uint16_t   bits_per_sample,
              const    uint16_t   channels) {

        if (open_wav(filename)) {
            write_header(sample_rate, bits_per_sample, channels);
        } else {
            return false;
        }

        return true;
    }

    bool close() {
        file.close();
        return true;
    }

    bool write(const float * data, size_t length) {
        return write_audio(data, length);
    }

    ~wav_writer() {
        if (file.is_open()) {
            file.close();
        }
    }
};


// Apply a high-pass frequency filter to PCM audio
// Suppresses frequencies below cutoff Hz
void high_pass_filter(
        std::vector<float> & data,
        float cutoff,
        float sample_rate);

// Basic voice activity detection (VAD) using audio energy adaptive threshold
bool vad_simple(
        std::vector<float> & pcmf32,
        int   sample_rate,
        int   last_ms,
        float vad_thold,
        float freq_thold,
        bool  verbose);

// compute similarity between two strings using Levenshtein distance
float similarity(const std::string & s0, const std::string & s1);

//
// SAM argument parsing
//

struct sam_params {
    int32_t seed      = -1; // RNG seed
    int32_t n_threads = std::min(4, (int32_t) std::thread::hardware_concurrency());

    std::string model     = "models/sam-vit-b/ggml-model-f16.bin"; // model path
    std::string fname_inp = "img.jpg";
    std::string fname_out = "img.out";
};

bool sam_params_parse(int argc, char ** argv, sam_params & params);

void sam_print_usage(int argc, char ** argv, const sam_params & params);

//
// Terminal utils
//

#define SQR(X)    ((X) * (X))
#define UNCUBE(x) x < 48 ? 0 : x < 115 ? 1 : (x - 35) / 40

/**
 * Quantizes 24-bit RGB to xterm256 code range [16,256).
 */
static int rgb2xterm256(int r, int g, int b) {
    unsigned char cube[] = {0, 0137, 0207, 0257, 0327, 0377};
    int av, ir, ig, ib, il, qr, qg, qb, ql;
    av = r * .299 + g * .587 + b * .114 + .5;
    ql = (il = av > 238 ? 23 : (av - 3) / 10) * 10 + 8;
    qr = cube[(ir = UNCUBE(r))];
    qg = cube[(ig = UNCUBE(g))];
    qb = cube[(ib = UNCUBE(b))];
    if (SQR(qr - r) + SQR(qg - g) + SQR(qb - b) <=
        SQR(ql - r) + SQR(ql - g) + SQR(ql - b))
        return ir * 36 + ig * 6 + ib + 020;
    return il + 0350;
}

static std::string set_xterm256_foreground(int r, int g, int b) {
    int x = rgb2xterm256(r, g, b);
    std::ostringstream oss;
    oss << "\033[38;5;" << x << "m";
    return oss.str();
}

// Lowest is red, middle is yellow, highest is green. Color scheme from
// Paul Tol; it is colorblind friendly https://personal.sron.nl/~pault/
const std::vector<std::string> k_colors = {
    set_xterm256_foreground(220,   5,  12),
    set_xterm256_foreground(232,  96,  28),
    set_xterm256_foreground(241, 147,  45),
    set_xterm256_foreground(246, 193,  65),
    set_xterm256_foreground(247, 240,  86),
    set_xterm256_foreground(144, 201, 135),
    set_xterm256_foreground( 78, 178, 101),
};

//
// Other utils
//

// convert timestamp to string, 6000 -> 01:00.000
std::string to_timestamp(int64_t t, bool comma = false);

// given a timestamp get the sample
int timestamp_to_sample(int64_t t, int n_samples, int whisper_sample_rate);

// check if file exists using ifstream
bool is_file_exist(const char *fileName);

// write text to file, and call system("command voice_id file")
bool speak_with_file(const std::string & command, const std::string & text, const std::string & path, int voice_id);
