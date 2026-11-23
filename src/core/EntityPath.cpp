#include "EntityPath.h"
#include <QLineF>

namespace opc {
QPainterPath entityPath(const Entity& entity) {
    QPainterPath path;
    switch (entity.shape) {
    case Shape::Point:
        path.addEllipse(entity.a, 3, 3);
        break;
    case Shape::Line:
        if (entity.a.x() == entity.b.x() && entity.a.y() == entity.b.y())
            path.addEllipse(entity.a, 0.25, 0.25);
        else {
            path.moveTo(entity.a);
            path.lineTo(entity.b);
        }
        break;
    case Shape::Rectangle:
        path.addRect(QRectF(entity.a, entity.b).normalized());
        break;
    case Shape::Circle: {
        const qreal radius = QLineF(entity.a, entity.b).length();
        path.addEllipse(entity.a, radius, radius);
        break;
    }
    case Shape::Curve:
        path.moveTo(entity.a);
        path.quadTo(entity.control, entity.b);
        break;
    }
    return path;
}
}
