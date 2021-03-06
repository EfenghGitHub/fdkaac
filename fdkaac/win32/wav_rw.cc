#include "wav_rw.h"
#include <assert.h>
#include <algorithm>
#include <limits>
#include <string.h>

typedef std::numeric_limits<int16_t> limits_int16;

struct ChunkHeader {
	uint32_t ID;
	uint32_t Size;
};

struct FmtSubchunk {
	ChunkHeader header;
	uint16_t AudioFormat;
	uint16_t NumChannels;
	uint32_t SampleRate;
	uint32_t ByteRate;
	uint16_t BlockAlign;
	uint16_t BitsPerSample;
};

struct WavHeader {
	struct {
		ChunkHeader header;
		uint32_t Format;
	} riff;
	FmtSubchunk fmt;
	struct {
		ChunkHeader header;
	} data;
};

class ReadableWav {
public:
	// Returns the number of bytes read.
	size_t virtual Read(void* buf, size_t num_bytes) = 0;
	virtual ~ReadableWav() {}
};

enum WavFormat {
	kWavFormatPcm = 1,  // PCM, each sample of size bytes_per_sample
	kWavFormatALaw = 6,  // 8-bit ITU-T G.711 A-law
	kWavFormatMuLaw = 7,  // 8-bit ITU-T G.711 mu-law
};

class ReadableWavFile : public ReadableWav {
public:
	explicit ReadableWavFile(FILE* file) : file_(file) {}
	virtual size_t Read(void* buf, size_t num_bytes) {
		return fread(buf, 1, num_bytes, file_);
	}

private:
	FILE* file_;
};

static const size_t kWavHeaderSize = 44;
// We write 16-bit PCM WAV files.
static const WavFormat kWavFormat = kWavFormatPcm;
static const int kBytesPerSample = 2;

static inline void WriteLE16(uint16_t* f, uint16_t x) { *f = x; }
static inline void WriteLE32(uint32_t* f, uint32_t x) { *f = x; }
static inline void WriteFourCC(uint32_t* f, char a, char b, char c, char d) {
	*f = static_cast<uint32_t>(a)
		| static_cast<uint32_t>(b) << 8
		| static_cast<uint32_t>(c) << 16
		| static_cast<uint32_t>(d) << 24;
}

static inline uint16_t ReadLE16(uint16_t x) { return x; }
static inline uint32_t ReadLE32(uint32_t x) { return x; }
static inline std::string ReadFourCC(uint32_t x) {
	return std::string(reinterpret_cast<char*>(&x), 4);
}

static inline uint32_t RiffChunkSize(uint32_t bytes_in_payload) {
	return bytes_in_payload + kWavHeaderSize - sizeof(ChunkHeader);
}

static inline uint32_t ByteRate(int num_channels, int sample_rate,
	int bytes_per_sample) {
	return static_cast<uint32_t>(num_channels)* sample_rate * bytes_per_sample;
}

static inline uint16_t BlockAlign(int num_channels, int bytes_per_sample) {
	return num_channels * bytes_per_sample;
}

const uint32_t kFmtSubchunkSize = sizeof(FmtSubchunk)-sizeof(ChunkHeader);

static bool CheckWavParameters(int num_channels,
	int sample_rate,
	WavFormat format,
	int bytes_per_sample,
	uint32_t num_samples) {
	// num_channels, sample_rate, and bytes_per_sample must be positive, must fit
	// in their respective fields, and their product must fit in the 32-bit
	// ByteRate field.
	if (num_channels <= 0 || sample_rate <= 0 || bytes_per_sample <= 0)
		return false;
	if (static_cast<uint64_t>(sample_rate) > std::numeric_limits<uint32_t>::max())
		return false;
	if (static_cast<uint64_t>(num_channels) >
		std::numeric_limits<uint16_t>::max())
		return false;
	if (static_cast<uint64_t>(bytes_per_sample)* 8 >
		std::numeric_limits<uint16_t>::max())
		return false;
	if (static_cast<uint64_t>(sample_rate)* num_channels * bytes_per_sample >
		std::numeric_limits<uint32_t>::max())
		return false;

	// format and bytes_per_sample must agree.
	switch (format) {
	case kWavFormatPcm:
		// Other values may be OK, but for now we're conservative:
		if (bytes_per_sample != 1 && bytes_per_sample != 2)
			return false;
		break;
	case kWavFormatALaw:
	case kWavFormatMuLaw:
		if (bytes_per_sample != 1)
			return false;
		break;
	default:
		return false;
	}

	// The number of bytes in the file, not counting the first ChunkHeader, must
	// be less than 2^32; otherwise, the ChunkSize field overflows.
	const uint32_t max_samples =
		(std::numeric_limits<uint32_t>::max()
		- (kWavHeaderSize - sizeof(ChunkHeader))) /
		bytes_per_sample;
	if (num_samples > max_samples)
		return false;

	// Each channel must have the same number of samples.
	if (num_samples % num_channels != 0)
		return false;

	return true;
}

static void WriteWavHeader(uint8_t* buf,
	int num_channels,
	int sample_rate,
	WavFormat format,
	int bytes_per_sample,
	uint32_t num_samples) {
	assert(CheckWavParameters(num_channels, sample_rate, format,
		bytes_per_sample, num_samples));

	WavHeader header;
	const uint32_t bytes_in_payload = bytes_per_sample * num_samples;

	WriteFourCC(&header.riff.header.ID, 'R', 'I', 'F', 'F');
	WriteLE32(&header.riff.header.Size, RiffChunkSize(bytes_in_payload));
	WriteFourCC(&header.riff.Format, 'W', 'A', 'V', 'E');

	WriteFourCC(&header.fmt.header.ID, 'f', 'm', 't', ' ');
	WriteLE32(&header.fmt.header.Size, kFmtSubchunkSize);
	WriteLE16(&header.fmt.AudioFormat, format);
	WriteLE16(&header.fmt.NumChannels, num_channels);
	WriteLE32(&header.fmt.SampleRate, sample_rate);
	WriteLE32(&header.fmt.ByteRate, ByteRate(num_channels, sample_rate,
		bytes_per_sample));
	WriteLE16(&header.fmt.BlockAlign, BlockAlign(num_channels, bytes_per_sample));
	WriteLE16(&header.fmt.BitsPerSample, 8 * bytes_per_sample);

	WriteFourCC(&header.data.header.ID, 'd', 'a', 't', 'a');
	WriteLE32(&header.data.header.Size, bytes_in_payload);

	// Do an extra copy rather than writing everything to buf directly, since buf
	// might not be correctly aligned.
	memcpy(buf, &header, kWavHeaderSize);
}

static inline int16_t FloatS16ToS16(float v) {
	static const float kMaxRound = limits_int16::max() - 0.5f;
	static const float kMinRound = limits_int16::min() + 0.5f;
	if (v > 0)
		return v >= kMaxRound ? limits_int16::max() :
		static_cast<int16_t>(v + 0.5f);
	return v <= kMinRound ? limits_int16::min() :
		static_cast<int16_t>(v - 0.5f);
}


static void FloatS16ToS16(const float* src, size_t size, int16_t* dest) {
	for (size_t i = 0; i < size; ++i)
		dest[i] = FloatS16ToS16(src[i]);
}

static bool ReadWavHeader(ReadableWav* readable,
	int* num_channels,
	int* sample_rate,
	WavFormat* format,
	int* bytes_per_sample,
	uint32_t* num_samples) {
	WavHeader header;
	if (readable->Read(&header, kWavHeaderSize - sizeof(header.data)) !=
		kWavHeaderSize - sizeof(header.data))
		return false;

	const uint32_t fmt_size = ReadLE32(header.fmt.header.Size);
	if (fmt_size != kFmtSubchunkSize) {
		// There is an optional two-byte extension field permitted to be present
		// with PCM, but which must be zero.
		int16_t ext_size;
		if (kFmtSubchunkSize + sizeof(ext_size) != fmt_size)
			return false;
		if (readable->Read(&ext_size, sizeof(ext_size)) != sizeof(ext_size))
			return false;
		if (ext_size != 0)
			return false;
	}
	if (readable->Read(&header.data, sizeof(header.data)) != sizeof(header.data))
		return false;

	// Parse needed fields.
	*format = static_cast<WavFormat>(ReadLE16(header.fmt.AudioFormat));
	*num_channels = ReadLE16(header.fmt.NumChannels);
	*sample_rate = ReadLE32(header.fmt.SampleRate);
	*bytes_per_sample = ReadLE16(header.fmt.BitsPerSample) / 8;
	const uint32_t bytes_in_payload = ReadLE32(header.data.header.Size);
	if (*bytes_per_sample <= 0)
		return false;
	*num_samples = bytes_in_payload / *bytes_per_sample;

	// Sanity check remaining fields.
	if (ReadFourCC(header.riff.header.ID) != "RIFF")
		return false;
	if (ReadFourCC(header.riff.Format) != "WAVE")
		return false;
	if (ReadFourCC(header.fmt.header.ID) != "fmt ")
		return false;
	if (ReadFourCC(header.data.header.ID) != "data")
		return false;

	if (ReadLE32(header.riff.header.Size) < RiffChunkSize(bytes_in_payload))
		return false;
	if (ReadLE32(header.fmt.ByteRate) !=
		ByteRate(*num_channels, *sample_rate, *bytes_per_sample))
		return false;
	if (ReadLE16(header.fmt.BlockAlign) !=
		BlockAlign(*num_channels, *bytes_per_sample))
		return false;

	return CheckWavParameters(*num_channels, *sample_rate, *format,
		*bytes_per_sample, *num_samples);
}

WavReader::WavReader(const std::string& filename)
: file_handle_(fopen(filename.c_str(), "rb")) {
	//CHECK(file_handle_ && "Could not open wav file for reading.");
	if (!file_handle_)
	{
		assert(0);
	}

	ReadableWavFile readable(file_handle_);
	WavFormat format;
	int bytes_per_sample;
	assert(ReadWavHeader(&readable, &num_channels_, &sample_rate_, &format,
		&bytes_per_sample, &num_samples_));
	num_samples_remaining_ = num_samples_;
	assert(kWavFormat == format);
	assert(kBytesPerSample == bytes_per_sample);
}

WavReader::~WavReader() {
	Close();
}

size_t WavReader::ReadSamples(size_t num_samples, int16_t* samples) {

	// There could be metadata after the audio; ensure we don't read it.
	num_samples = std::min(uint32_t(num_samples), num_samples_remaining_);
	const size_t read = fread(samples, sizeof(*samples), num_samples, file_handle_);
	// If we didn't read what was requested, ensure we've reached the EOF.
	assert(read == num_samples || feof(file_handle_));
	assert(read <= num_samples_remaining_);
	num_samples_remaining_ -= uint32_t(read);
	return read;
}

size_t WavReader::ReadSamples(size_t num_samples, float* samples) {
	static const size_t kChunksize = 4096 / sizeof(uint16_t);
	size_t read = 0;
	for (size_t i = 0; i < num_samples; i += kChunksize) {
		int16_t isamples[kChunksize];
		size_t chunk = std::min(kChunksize, num_samples - i);
		chunk = ReadSamples(chunk, isamples);
		for (size_t j = 0; j < chunk; ++j)
			samples[i + j] = isamples[j];
		read += chunk;
	}
	return read;
}

void WavReader::Close() {
	assert(0 == fclose(file_handle_));
	file_handle_ = NULL;
}

WavWriter::WavWriter(const std::string& filename, int sample_rate,
	int num_channels)
	: sample_rate_(sample_rate),
	num_channels_(num_channels),
	num_samples_(0),
	file_handle_(fopen(filename.c_str(), "wb")) {
	//CHECK(file_handle_ && "Could not open wav file for writing.");
	assert(file_handle_ != NULL);
	assert(CheckWavParameters(num_channels_,
		sample_rate_,
		kWavFormat,
		kBytesPerSample,
		num_samples_));

	// Write a blank placeholder header, since we need to know the total number
	// of samples before we can fill in the real data.
	static const uint8_t blank_header[kWavHeaderSize] = { 0 };
	assert(1 == fwrite(blank_header, kWavHeaderSize, 1, file_handle_));
}

WavWriter::~WavWriter() {
	Close();
}

void WavWriter::WriteSamples(const int16_t* samples, size_t num_samples) {

	const size_t written =
		fwrite(samples, sizeof(*samples), num_samples, file_handle_);
	assert(num_samples == written);
	num_samples_ += static_cast<uint32_t>(written);
	assert(written <= std::numeric_limits<uint32_t>::max() ||
		num_samples_ >= written);  // detect uint32_t overflow
	assert(CheckWavParameters(num_channels_,
		sample_rate_,
		kWavFormat,
		kBytesPerSample,
		num_samples_));
}

void WavWriter::WriteSamples(const float* samples, size_t num_samples) {
	static const size_t kChunksize = 4096 / sizeof(uint16_t);
	for (size_t i = 0; i < num_samples; i += kChunksize) {
		int16_t isamples[kChunksize];
		const size_t chunk = std::min(kChunksize, num_samples - i);
		FloatS16ToS16(samples + i, chunk, isamples);
		WriteSamples(isamples, chunk);
	}
}

void WavWriter::Close() {
	assert(0 == fseek(file_handle_, 0, SEEK_SET));
	uint8_t header[kWavHeaderSize];
	WriteWavHeader(header, num_channels_, sample_rate_, kWavFormat,
		kBytesPerSample, num_samples_);
	assert(1u == fwrite(header, kWavHeaderSize, 1, file_handle_));
	assert(0 == fclose(file_handle_));
	file_handle_ = NULL;
}
