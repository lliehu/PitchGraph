#ifndef OFFLINEWAVREADER_H
#define OFFLINEWAVREADER_H

#include <QString>
#include <vector>

struct DecodedWavInfo {
    unsigned int sampleRateHz = 0;
    unsigned int channels = 0;
    unsigned int bitsPerSample = 0;
    unsigned int audioFormat = 0;
};

class OfflineWavReader {
public:
    static bool readMonoFloat48k(
        const QString& filePath,
        std::vector<float>* monoSamples48k,
        DecodedWavInfo* sourceInfo,
        QString* errorMessage
    );
};

#endif // OFFLINEWAVREADER_H
