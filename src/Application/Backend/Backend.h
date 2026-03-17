#pragma once

#include "AdsConfigBackend.h"
#include "ConveyorBackend.h"
#include "RotaryTableBackend.h"

#include <QCoro/QCoroTask>

#include <QObject>
#include <QString>
#include <QtQml/qqmlregistration.h>

#include <array>
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
        Q_PROPERTY(backend::ConveyorBackend* conveyor READ conveyor CONSTANT)
        Q_PROPERTY(backend::RotaryTableBackend* rotaryTable READ rotaryTable CONSTANT)
        Q_PROPERTY(QString welcomeMessage READ welcomeMessage CONSTANT)

      public:
        explicit Backend(QObject* parent = nullptr);
        ~Backend() override;

        auto adsConfig() const -> AdsConfigBackend*;
        auto conveyor() const -> ConveyorBackend*;
        auto rotaryTable() const -> RotaryTableBackend*;
        QString welcomeMessage() const;

      private:
        struct SharedAdsConfig;

        void launchTask(QCoro::Task<void>&& task);
        auto initializeSharedAdsLinkAsync() -> QCoro::Task<void>;
        auto configFilePath() const -> QString;
        auto loadSharedAdsConfig() const -> SharedAdsConfig;

        AdsConfigBackend* m_adsConfig{ nullptr };
        ConveyorBackend* m_conveyor{ nullptr };
        RotaryTableBackend* m_rotaryTable{ nullptr };
        std::unique_ptr<core::link::ILink> m_sharedAdsLink;
        core::link::ISymbolicLink* m_sharedAdsSymbolicLink{ nullptr };
    };
}
