#pragma once

#include <QCoro/QCoroTask>

#include <QObject>
#include <QString>
#include <QStringList>
#include <QtQml/qqmlregistration.h>

#include <memory>

namespace core::link
{
    class ILink;
    class ISymbolicLink;
}

namespace backend
{
    class AdsConfigBackend : public QObject
    {
        Q_OBJECT
        QML_NAMED_ELEMENT(AdsConfigBackend)
        QML_UNCREATABLE("Use Backend.adsConfig")

        Q_PROPERTY(QString serverIp READ serverIp WRITE setServerIp NOTIFY serverIpChanged)
        Q_PROPERTY(QString serverNetId READ serverNetId WRITE setServerNetId NOTIFY serverNetIdChanged)
        Q_PROPERTY(QString localNetId READ localNetId WRITE setLocalNetId NOTIFY localNetIdChanged)
        Q_PROPERTY(int adsPort READ adsPort WRITE setAdsPort NOTIFY adsPortChanged)
        Q_PROPERTY(QString variableName READ variableName WRITE setVariableName NOTIFY variableNameChanged)
        Q_PROPERTY(QString variableType READ variableType WRITE setVariableType NOTIFY variableTypeChanged)
        Q_PROPERTY(
          QString variableValue READ variableValue WRITE setVariableValue NOTIFY variableValueChanged)
        Q_PROPERTY(QStringList variableTypes READ variableTypes CONSTANT)
        Q_PROPERTY(ConnectionState connectionState READ connectionState NOTIFY connectionStateChanged)
        Q_PROPERTY(QString connectionStateLabel READ connectionStateLabel NOTIFY connectionStateChanged)
        Q_PROPERTY(bool connected READ connected NOTIFY connectionStateChanged)
        Q_PROPERTY(bool canConnect READ canConnect NOTIFY connectionStateChanged)
        Q_PROPERTY(bool canDisconnect READ canDisconnect NOTIFY connectionStateChanged)
        Q_PROPERTY(bool canSave READ canSave NOTIFY dirtyChanged)
        Q_PROPERTY(bool canDiscard READ canDiscard NOTIFY dirtyChanged)
        Q_PROPERTY(bool dirty READ dirty NOTIFY dirtyChanged)
        Q_PROPERTY(QString statusMessage READ statusMessage NOTIFY statusChanged)
        Q_PROPERTY(bool statusIsError READ statusIsError NOTIFY statusChanged)

      public:
        struct AdsSettings
        {
            QString serverIp;
            QString serverNetId;
            QString localNetId;
            int adsPort{ 851 };
            QString variableName;
            QString variableType;

            auto operator==(const AdsSettings&) const -> bool = default;
        };

        enum class ConnectionState
        {
            Disconnected,
            Connecting,
            Connected,
            Error
        };
        Q_ENUM(ConnectionState)

        explicit AdsConfigBackend(QObject* parent = nullptr);
        ~AdsConfigBackend() override;

        auto serverIp() const -> QString;
        auto serverNetId() const -> QString;
        auto localNetId() const -> QString;
        auto adsPort() const -> int;

        auto variableName() const -> QString;
        auto variableType() const -> QString;
        auto variableValue() const -> QString;
        auto variableTypes() const -> QStringList;

        auto connectionState() const -> ConnectionState;
        auto connectionStateLabel() const -> QString;
        auto connected() const -> bool;
        auto canConnect() const -> bool;
        auto canDisconnect() const -> bool;
        auto canSave() const -> bool;
        auto canDiscard() const -> bool;
        auto dirty() const -> bool;
        auto statusMessage() const -> QString;
        auto statusIsError() const -> bool;

        void setServerIp(const QString& value);
        void setServerNetId(const QString& value);
        void setLocalNetId(const QString& value);
        void setAdsPort(int value);
        void setVariableName(const QString& value);
        void setVariableType(const QString& value);
        void setVariableValue(const QString& value);

        Q_INVOKABLE void connectToAds();
        Q_INVOKABLE void disconnectFromAds();
        Q_INVOKABLE void readVariable();
        Q_INVOKABLE void writeVariable();
        Q_INVOKABLE void saveConfig();
        Q_INVOKABLE void discardChanges();

      signals:
        void serverIpChanged();
        void serverNetIdChanged();
        void localNetIdChanged();
        void adsPortChanged();
        void variableNameChanged();
        void variableTypeChanged();
        void variableValueChanged();
        void connectionStateChanged();
        void dirtyChanged();
        void statusChanged();

      private:
        void loadConfig();
        void applySettings(const AdsSettings& settings);
        void updateDirty();
        void setConnectionState(ConnectionState state);
        void setStatus(const QString& message, bool isError);
        void resetLink();
        auto validateSettings(QString& errorMessage) const -> bool;
        auto configFilePath(bool createDirectory) const -> QString;
        void launchTask(QCoro::Task<void>&& task);

        auto connectToAdsAsync() -> QCoro::Task<void>;
        auto disconnectFromAdsAsync() -> QCoro::Task<void>;
        auto readVariableAsync() -> QCoro::Task<void>;
        auto writeVariableAsync() -> QCoro::Task<void>;

        template<typename T>
        auto readValueAs() -> QCoro::Task<void>;

        template<typename T>
        auto writeValueAs() -> QCoro::Task<void>;

        AdsSettings m_savedSettings;
        AdsSettings m_draftSettings;
        QString m_variableValue;
        ConnectionState m_connectionState{ ConnectionState::Disconnected };
        QString m_statusMessage;
        bool m_statusIsError{ false };
        bool m_dirty{ false };
        std::unique_ptr<core::link::ILink> m_link;
        core::link::ISymbolicLink* m_symbolicLink{ nullptr };
    };
}