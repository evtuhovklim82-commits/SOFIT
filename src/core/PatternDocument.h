#pragma once
#include <QPointF>
#include <QString>
#include <QVector>

namespace opc {
enum class Shape { Point, Line, Rectangle, Circle, Curve };

struct Entity {
    Shape shape;
    QPointF a, b;
    QPointF control{};
};

class PatternDocument {
public:
    void insert(qsizetype index, const Entity& entity) { entities_.insert(index, entity); }
    void replace(qsizetype index, const Entity& entity) { entities_[index] = entity; }
    void remove(qsizetype index) { entities_.removeAt(index); }
    const QVector<Entity>& entities() const { return entities_; }
    bool save(const QString& path, QString* error = nullptr) const;
    bool load(const QString& path, QString* error = nullptr);

private:
    QVector<Entity> entities_;
};
}
