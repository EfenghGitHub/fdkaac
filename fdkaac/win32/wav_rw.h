#ifndef _WAV_RW_H_
#define _WAV_RW_H_

#include <stdint.h>
#include <string>

// Simple C++ class for writing 16-bit PCM WAV files. All error handling is
// by calls to CHECK(), making it unsuitable for anything but debug code.
class WavWriter {
public:
	// Open a new WAV file for writing.
	WavWriter(const std::string& filename, int sample_rate, int num_channels);

	// Close the WAV file, after writing its header.
	~WavWriter();

	// Write additional samples to the file. Each sample is in the range
	// [-32768,32767], and there must be the previously specified number of
	// interleaved channels.
	void WriteSamples(const float* samples, size_t num_samples);
	void WriteSamples(const int16_t* samples, size_t num_samples);

	int sample_rate() const { return sample_rate_; }
	int num_channels() const { return num_channels_; }
	uint32_t num_samples() const { return num_samples_; }

private:
	void Close();
	const int sample_rate_;
	const int num_channels_;
	uint32_t num_samples_;  // Total number of samples written to file.
	FILE* file_handle_;  // Output file, owned by this class
};

// Follows the conventions of WavWriter.
class WavReader {
public:
	// Opens an existing WAV file for reading.
	explicit WavReader(const std::string& filename);

	// Close the WAV file.
	~WavReader();

	// Returns the number of samples read. If this is less than requested,
	// verifies that the end of the file was reached.
	size_t ReadSamples(size_t num_samples, float* samples);   // [-32768.0, 32767.0]
	size_t ReadSamples(size_t num_samples, int16_t* samples); // [-32768, 32767]

	int sample_rate() const { return sample_rate_; }
	int num_channels() const { return num_channels_; }
	uint32_t num_samples() const { return num_samples_; }

private:
	void Close();
	int sample_rate_;
	int num_channels_;
	uint32_t num_samples_;  // Total number of samples in the file.
	uint32_t num_samples_remaining_;
	FILE* file_handle_;  // Input file, owned by this class.
};

#endif 