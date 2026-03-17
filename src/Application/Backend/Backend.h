#pragma once

#include "AdsConfigBackend.h"
#include "RotaryTableBackend.h"

#include <QObject>
#include <QString>
#include <QtQml/qqmlregistration.h>

#include <memory>

namespace core::link
{
    class ILink;
    class ISymbolicLink;
}

namespace backend
{
    class Backend : public QObject
    {
        Q_OBJECT
        QML_ELEMENT
        Q_PROPERTY(backend::AdsConfigBackend* adsConfig READ adsConfig CONSTANT)
        Q_PROPERTY(backend::RotaryTableBackend* rotaryTable READ rotaryTable CONSTANT)
        Q_PROPERTY(QString welcomeMessage READ welcomeMessage CONSTANT)

      public:
        explicit Backend(QObject* parent = nullptr);
        ~Backend() override;

        auto adsConfig() const -> AdsConfigBackend*;
        auto rotaryTable() const -> RotaryTableBackend*;
        QString welcomeMessage() const;

      private:
        struct SharedAdsConfig;

        void launchTask(QCoro::Task<void>&& task);
        auto initializeSharedAdsLinkAsync() -> QCoro::Task<void>;
        auto configFilePath() const -> QString;
        auto loadSharedAdsConfig() const -> SharedAdsConfig;

        AdsConfigBackend* m_adsConfig{ nullptr };
        RotaryTableBackend* m_rotaryTable{ nullptr };
        std::unique_ptr<core::link::ILink> m_sharedAdsLink;
        core::link::ISymbolicLink* m_sharedAdsSymbolicLink{ nullptr };
    };
}
