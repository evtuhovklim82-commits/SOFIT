/****************************************************************************
** Meta object code from reading C++ file 'PatternView.h'
**
** Created by: The Qt Meta Object Compiler version 69 (Qt 6.12.0)
**
** WARNING! All changes made in this file will be lost!
*****************************************************************************/

#include "../../../src/ui/PatternView.h"
#include <QtCore/qmetatype.h>

#include <QtCore/qtmochelpers.h>

#include <memory>


#include <QtCore/qxptype_traits.h>
#if !defined(Q_MOC_OUTPUT_REVISION)
#error "The header file 'PatternView.h' doesn't include <QObject>."
#elif Q_MOC_OUTPUT_REVISION != 69
#error "This file was generated using the moc from 6.12.0. It"
#error "cannot be used with the include files from this version of Qt."
#error "(The moc has changed too much.)"
#endif

#ifndef Q_CONSTINIT
#define Q_CONSTINIT
#endif

QT_WARNING_PUSH
QT_WARNING_DISABLE_DEPRECATED
QT_WARNING_DISABLE_GCC("-Wuseless-cast")
namespace {
struct qt_meta_tag_ZN11PatternViewE_t {};
} // unnamed namespace

template <> constexpr inline auto PatternView::qt_create_metaobjectdata<qt_meta_tag_ZN11PatternViewE_t>()
{
    namespace QMC = QtMocConstants;
    QtMocHelpers::StringRefStorage qt_stringData {
        "PatternView",
        "entityDrawn",
        "",
        "opc::Entity",
        "entity",
        "cursorPositionChanged",
        "QPointF",
        "position",
        "cursorLeftCanvas",
        "zoomChanged",
        "pixelsPerMillimeter",
        "measured",
        "distance",
        "entityMoved",
        "qsizetype",
        "index",
        "offset"
    };

    QtMocHelpers::UintData qt_methods {
        // Signal 'entityDrawn'
        QtMocHelpers::SignalData<void(const opc::Entity &)>(1, 2, QMC::AccessPublic, QMetaType::Void, {{
            { 0x80000000 | 3, 4 },
        }}),
        // Signal 'cursorPositionChanged'
        QtMocHelpers::SignalData<void(const QPointF &)>(5, 2, QMC::AccessPublic, QMetaType::Void, {{
            { 0x80000000 | 6, 7 },
        }}),
        // Signal 'cursorLeftCanvas'
        QtMocHelpers::SignalData<void()>(8, 2, QMC::AccessPublic, QMetaType::Void),
        // Signal 'zoomChanged'
        QtMocHelpers::SignalData<void(qreal)>(9, 2, QMC::AccessPublic, QMetaType::Void, {{
            { QMetaType::QReal, 10 },
        }}),
        // Signal 'measured'
        QtMocHelpers::SignalData<void(qreal)>(11, 2, QMC::AccessPublic, QMetaType::Void, {{
            { QMetaType::QReal, 12 },
        }}),
        // Signal 'entityMoved'
        QtMocHelpers::SignalData<void(qsizetype, const QPointF &)>(13, 2, QMC::AccessPublic, QMetaType::Void, {{
            { 0x80000000 | 14, 15 }, { 0x80000000 | 6, 16 },
        }}),
    };
    QtMocHelpers::UintData qt_properties {
    };
    QtMocHelpers::UintData qt_enums {
    };
    return QtMocHelpers::metaObjectData<PatternView, qt_meta_tag_ZN11PatternViewE_t>(QMC::MetaObjectFlag{}, qt_stringData,
            qt_methods, qt_properties, qt_enums);
}
Q_CONSTINIT const QMetaObject PatternView::staticMetaObject = { {
    QMetaObject::SuperData::link<QGraphicsView::staticMetaObject>(),
    qt_staticMetaObjectStaticContent<qt_meta_tag_ZN11PatternViewE_t>.stringdata,
    qt_staticMetaObjectStaticContent<qt_meta_tag_ZN11PatternViewE_t>.data,
    qt_static_metacall,
    nullptr,
    qt_staticMetaObjectRelocatingContent<qt_meta_tag_ZN11PatternViewE_t>.metaTypes,
    nullptr
} };

void PatternView::qt_static_metacall(QObject *_o, QMetaObject::Call _c, int _id, void **_a)
{
    auto *_t = static_cast<PatternView *>(_o);
    if (_c == QMetaObject::InvokeMetaMethod) {
        switch (_id) {
        case 0: _t->entityDrawn((*reinterpret_cast<std::add_pointer_t<opc::Entity>>(_a[1]))); break;
        case 1: _t->cursorPositionChanged((*reinterpret_cast<std::add_pointer_t<QPointF>>(_a[1]))); break;
        case 2: _t->cursorLeftCanvas(); break;
        case 3: _t->zoomChanged((*reinterpret_cast<std::add_pointer_t<qreal>>(_a[1]))); break;
        case 4: _t->measured((*reinterpret_cast<std::add_pointer_t<qreal>>(_a[1]))); break;
        case 5: _t->entityMoved((*reinterpret_cast<std::add_pointer_t<qsizetype>>(_a[1])),(*reinterpret_cast<std::add_pointer_t<QPointF>>(_a[2]))); break;
        default: ;
        }
    }
    if (_c == QMetaObject::IndexOfMethod) {
        if (QtMocHelpers::indexOfMethod<void (PatternView::*)(const opc::Entity & )>(_a, &PatternView::entityDrawn, 0))
            return;
        if (QtMocHelpers::indexOfMethod<void (PatternView::*)(const QPointF & )>(_a, &PatternView::cursorPositionChanged, 1))
            return;
        if (QtMocHelpers::indexOfMethod<void (PatternView::*)()>(_a, &PatternView::cursorLeftCanvas, 2))
            return;
        if (QtMocHelpers::indexOfMethod<void (PatternView::*)(qreal )>(_a, &PatternView::zoomChanged, 3))
            return;
        if (QtMocHelpers::indexOfMethod<void (PatternView::*)(qreal )>(_a, &PatternView::measured, 4))
            return;
        if (QtMocHelpers::indexOfMethod<void (PatternView::*)(qsizetype , const QPointF & )>(_a, &PatternView::entityMoved, 5))
            return;
    }
}

const QMetaObject *PatternView::metaObject() const
{
    return QObject::d_ptr->metaObject ? QObject::d_ptr->dynamicMetaObject() : &staticMetaObject;
}

void *PatternView::qt_metacast(const char *_clname)
{
    if (!_clname) return nullptr;
    if (!strcmp(_clname, qt_staticMetaObjectStaticContent<qt_meta_tag_ZN11PatternViewE_t>.strings))
        return static_cast<void*>(this);
    return QGraphicsView::qt_metacast(_clname);
}

int PatternView::qt_metacall(QMetaObject::Call _c, int _id, void **_a)
{
    _id = QGraphicsView::qt_metacall(_c, _id, _a);
    if (_id < 0)
        return _id;
    if (_c == QMetaObject::InvokeMetaMethod) {
        if (_id < 6)
            qt_static_metacall(this, _c, _id, _a);
        _id -= 6;
    }
    if (_c == QMetaObject::RegisterMethodArgumentMetaType) {
        if (_id < 6)
            *reinterpret_cast<QMetaType *>(_a[0]) = QMetaType();
        _id -= 6;
    }
    return _id;
}

// SIGNAL 0
void PatternView::entityDrawn(const opc::Entity & _t1)
{
    QMetaObject::activate<void>(this, &staticMetaObject, 0, nullptr, _t1);
}

// SIGNAL 1
void PatternView::cursorPositionChanged(const QPointF & _t1)
{
    QMetaObject::activate<void>(this, &staticMetaObject, 1, nullptr, _t1);
}

// SIGNAL 2
void PatternView::cursorLeftCanvas()
{
    QMetaObject::activate(this, &staticMetaObject, 2, nullptr);
}

// SIGNAL 3
void PatternView::zoomChanged(qreal _t1)
{
    QMetaObject::activate<void>(this, &staticMetaObject, 3, nullptr, _t1);
}

// SIGNAL 4
void PatternView::measured(qreal _t1)
{
    QMetaObject::activate<void>(this, &staticMetaObject, 4, nullptr, _t1);
}

// SIGNAL 5
void PatternView::entityMoved(qsizetype _t1, const QPointF & _t2)
{
    QMetaObject::activate<void>(this, &staticMetaObject, 5, nullptr, _t1, _t2);
}
QT_WARNING_POP
