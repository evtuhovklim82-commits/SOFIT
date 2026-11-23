#include "ProjectRecovery.h"
#include "PatternDocument.h"

#include <QDir>
#include <QFile>
#include <QLockFile>
#include <QUuid>

namespace opc {
namespace {
std::unique_ptr<QLockFile> lockSnapshot(const QString& path) {
    auto lock = std::make_unique<QLockFile>(path + QStringLiteral(".lock"));
    // A live editor may be idle for days. Let Qt detect a dead process.
    lock->setStaleLockTime(0);
    if (!lock->tryLock())
        return {};
    return lock;
}
}

ProjectRecovery::ProjectRecovery() = default;
ProjectRecovery::~ProjectRecovery() = default;

QStringList ProjectRecovery::available(const QString& directory) {
    QStringList paths;
    const QDir folder(directory);
    for (const auto& entry : folder.entryInfoList({QStringLiteral("*.sofit")},
                                                 QDir::Files | QDir::NoSymLinks, QDir::Time)) {
        if (auto lock = lockSnapshot(entry.absoluteFilePath()))
            paths.append(entry.absoluteFilePath());
    }
    return paths;
}

bool ProjectRecovery::capture(const PatternDocument& document, const QString& directory,
                              const QString& projectPath, QString* error) {
    if (!lock_) {
        const QDir folder(directory);
        if (!QDir().mkpath(folder.absolutePath())) {
            if (error) *error = QStringLiteral("Could not create the recovery directory");
            return false;
        }
        const auto name = projectPath.isEmpty() ? QStringLiteral("Untitled")
                                              : QFileInfo(projectPath).completeBaseName().left(48);
        const auto path = folder.absoluteFilePath(QUuid::createUuid().toString(QUuid::WithoutBraces)
                                                  + QStringLiteral(" - ") + name + QStringLiteral(".sofit"));
        auto lock = lockSnapshot(path);
        if (!lock) {
            if (error) *error = QStringLiteral("Could not lock the recovery snapshot");
            return false;
        }
        path_ = path;
        lock_ = std::move(lock);
    }
    return document.save(path_, error);
}

bool ProjectRecovery::claim(const QString& snapshotPath, PatternDocument& document, QString* error) {
    auto lock = lockSnapshot(snapshotPath);
    if (!lock) {
        if (error) *error = QStringLiteral("Recovery snapshot is already in use or cannot be locked");
        return false;
    }
    if (!document.load(snapshotPath, error))
        return false;
    path_ = snapshotPath;
    lock_ = std::move(lock);
    return true;
}

bool ProjectRecovery::clear() {
    if (!lock_)
        return true;
    const bool removed = !QFile::exists(path_) || QFile::remove(path_);
    lock_.reset();
    path_.clear();
    return removed;
}
}
