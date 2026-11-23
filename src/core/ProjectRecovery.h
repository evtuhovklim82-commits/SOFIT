#pragma once

#include <QStringList>
#include <memory>

class QLockFile;

namespace opc {
class PatternDocument;

class ProjectRecovery {
public:
    ProjectRecovery();
    ~ProjectRecovery();

    static QStringList available(const QString& directory);
    bool capture(const PatternDocument& document, const QString& directory,
                 const QString& projectPath, QString* error);
    bool claim(const QString& snapshotPath, PatternDocument& document, QString* error);
    bool clear();

private:
    QString path_;
    std::unique_ptr<QLockFile> lock_;
};
}
