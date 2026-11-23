#pragma once
#include <QPointF>
#include <QVector>
#include <QString>

namespace opc {
struct Segment { QPointF a, b; };
class PatternDocument {
public:
    void addSegment(QPointF a, QPointF b);
    void clear();
    const QVector<Segment>& segments() const;
    bool save(const QString& path, QString* error = nullptr) const;
    bool load(const QString& path, QString* error = nullptr);
private:
    QVector<Segment> segments_;
};
}
