#ifndef BATCHEXPORTRUNNER_H
#define BATCHEXPORTRUNNER_H

#include <QString>

class BatchExportRunner {
public:
    bool runFromConfig(const QString& configPath, QString* errorMessage = nullptr) const;
};

#endif // BATCHEXPORTRUNNER_H
