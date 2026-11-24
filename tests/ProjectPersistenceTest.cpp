#include "PatternDocument.h"
#include "ProjectRecovery.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QTemporaryDir>
#include <QTimer>
#include <cstdlib>
#include <iostream>
#include <limits>

namespace {
void check(bool condition, const QString& message) {
    if (condition)
        return;
    std::cerr << message.toStdString() << '\n';
    std::exit(1);
}

QByteArray read(const QString& path) {
    QFile file(path);
    check(file.open(QIODevice::ReadOnly), file.errorString());
    return file.readAll();
}

void write(const QString& path, const QByteArray& bytes) {
    QFile file(path);
    check(file.open(QIODevice::WriteOnly) && file.write(bytes) == bytes.size(), file.errorString());
}

opc::PatternDocument sample() {
    opc::PatternDocument document;
    document.insert(0, {opc::Shape::Line, {10, 12}, {30, 40}});
    document.production().useDraftMeasurements = false;
    document.setLayerVisible(opc::Layer::Allowance, false);
    return document;
}
}

int main(int argc, char** argv) {
    QCoreApplication app(argc, argv);
    const auto arguments = app.arguments();
    QString error;
    if (arguments.size() == 3 && arguments[1] == "--crash-writer") {
        opc::ProjectRecovery recovery;
        check(recovery.capture(sample(), arguments[2], "Crash test.sofit", &error), error);
        std::cout << "snapshot ready" << std::endl;
        QTimer::singleShot(20000, &app, &QCoreApplication::quit);
        return app.exec();
    }
    check(arguments.size() == 2, "Pass the directory containing the legacy examples");

    QTemporaryDir directory;
    check(directory.isValid(), "Create an isolated project directory");
    const auto path = directory.filePath("выкройка.sofit");
    auto source = sample();
    check(source.save(path, &error), error);
    const auto original = read(path);
    auto envelope = QJsonDocument::fromJson(original).object();
    check(envelope.value("version").toInt() == 14 && envelope.value("sha256").toString().size() == 64,
          "Write a versioned SHA-256 envelope");

    opc::PatternDocument active;
    check(active.load(path, &error), error);
    check(active.entities().size() == 1 && active.entities()[0].a == QPointF(10, 12)
          && !active.production().useDraftMeasurements && !active.layerVisible(opc::Layer::Allowance),
          "Retain geometry, size measurement mode and layer visibility");
    auto invalid = source.entities()[0];
    invalid.a.setX(std::numeric_limits<double>::quiet_NaN());
    source.replace(0, invalid);
    check(!source.save(path, &error) && !error.isEmpty() && read(path) == original,
          "An invalid edit must never overwrite a readable project");
    check(!active.save(directory.path(), &error) && !error.isEmpty(),
          "Report a destination that cannot be replaced");

    const auto bad = directory.filePath("damaged.sofit");
    auto altered = envelope;
    auto payload = envelope.value("payload").toString();
    payload.replace("10,12", "11,12");
    check(payload != envelope.value("payload").toString(), "Change valid JSON geometry without changing its hash");
    altered["payload"] = payload;
    write(bad, QJsonDocument(altered).toJson());
    check(!active.load(bad, &error) && error.contains("SHA-256") && active.entities()[0].a == QPointF(10, 12),
          "Detect valid-looking corruption and preserve the current document");
    write(bad, original.first(original.size() / 2));
    check(!active.load(bad, &error) && error.contains("byte"), "Report a truncated JSON file");
    altered = envelope;
    altered["version"] = 16;
    write(bad, QJsonDocument(altered).toJson());
    check(!active.load(bad, &error) && error.contains("newer"), "Explain a future schema version");
    QFile oversized(bad);
    check(oversized.open(QIODevice::WriteOnly) && oversized.resize(64 * 1024 * 1024 + 1),
          "Create a project exceeding the input limit");
    oversized.close();
    check(!active.load(bad, &error) && error.contains("64 MiB"), "Bound project reads");

    auto professional = sample();
    professional.productInformation().title = QStringLiteral("Куртка");
    professional.production().pieceClearanceMm = 6.25;
    check(professional.save(bad, &error) && active.load(bad, &error), error);
    check(QJsonDocument::fromJson(read(bad)).object().value("version").toInt() == 15
          && active.productInformation() == professional.productInformation()
          && active.production().pieceClearanceMm == 6.25, "Round-trip schema 15 production data");
    check(active.load(path, &error) && active.productInformation() == opc::ProductInformation{}
          && active.production().pieceClearanceMm == 0, "Apply defaults when reopening schema 14");

    const auto second = directory.filePath("resaved.sofit");
    for (const auto* name : {"bib-apron.sofit", "graded-skirt.sofit", "linked-trousers.sofit"}) {
        const auto example = QDir(arguments[1]).filePath(QString::fromLatin1(name));
        const auto legacyBytes = read(example);
        opc::PatternDocument migrated;
        check(migrated.load(example, &error) && migrated.save(path, &error), error);
        check(active.load(path, &error) && active.save(second, &error), error);
        check(read(path) == read(second) && read(example) == legacyBytes,
              QString("Migration and repeated saves preserve all persisted fields in %1").arg(name));
    }

    for (const auto* name : {"graded-skirt-v14.sofit", "linked-trousers-v14.sofit"}) {
        const auto example = QDir(arguments[1]).filePath(QString::fromLatin1(name));
        check(active.load(example, &error) && active.save(path, &error), error);
        check(active.load(path, &error), error);
    }

    const auto recoveryDir = directory.filePath("recovery");
    QProcess writer;
    writer.start(QCoreApplication::applicationFilePath(), {"--crash-writer", recoveryDir});
    check(writer.waitForStarted(), writer.errorString());
    check(writer.waitForReadyRead(10000) && writer.readAllStandardOutput().contains("snapshot ready"),
          "Write an atomic snapshot in a separate editor process");
    check(opc::ProjectRecovery::available(recoveryDir).isEmpty(),
          "Other editors cannot recover a live session");
    writer.kill();
    check(writer.waitForFinished(10000), "Simulate a crash without running cleanup");
    const auto snapshots = opc::ProjectRecovery::available(recoveryDir);
    check(snapshots.size() == 1, "Discover the snapshot after the writer process dies");
    opc::ProjectRecovery recovery;
    check(recovery.claim(snapshots.first(), active, &error), error);
    check(active.entities()[0].a == QPointF(10, 12)
          && opc::ProjectRecovery::available(recoveryDir).isEmpty(),
          "Restore the draft and prevent a competing claim");
    check(!recovery.capture(source, recoveryDir, {}, &error),
          "Reject an invalid autosave update before replacing a good snapshot");
    opc::PatternDocument previous;
    check(previous.load(snapshots.first(), &error) && previous.entities()[0].a == QPointF(10, 12),
          "Preserve the last complete snapshot on autosave failure");
    check(recovery.clear() && !QFile::exists(snapshots.first()), "Remove an explicitly retired snapshot");
    write(snapshots.first(), "corrupt snapshot");
    check(!recovery.claim(snapshots.first(), active, &error) && active.entities()[0].a == QPointF(10, 12),
          "A corrupt recovery file leaves the current document intact");
}
