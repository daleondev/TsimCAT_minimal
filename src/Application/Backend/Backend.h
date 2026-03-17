#pragma once

#include <QObject>
#include <QString>
#include <QtQml/qqmlregistration.h>

namespace backend
{
    class Backend : public QObject
    {
        Q_OBJECT
        QML_ELEMENT
        Q_PROPERTY(QString welcomeMessage READ welcomeMessage CONSTANT)

      public:
        explicit Backend(QObject* parent = nullptr);
        ~Backend() override = default;

        QString welcomeMessage() const;
    };
}
