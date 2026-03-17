#pragma once

#include "AdsConfigBackend.h"

#include <QObject>
#include <QString>
#include <QtQml/qqmlregistration.h>

namespace backend
{
    class Backend : public QObject
    {
        Q_OBJECT
        QML_ELEMENT
        Q_PROPERTY(backend::AdsConfigBackend* adsConfig READ adsConfig CONSTANT)
        Q_PROPERTY(QString welcomeMessage READ welcomeMessage CONSTANT)

      public:
        explicit Backend(QObject* parent = nullptr);
        ~Backend() override = default;

        auto adsConfig() const -> AdsConfigBackend*;
        QString welcomeMessage() const;

      private:
        AdsConfigBackend* m_adsConfig{ nullptr };
    };
}
