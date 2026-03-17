#include "AdsConfigBackend.h"

#include "Coroutines/Task.hpp"
#include "Link/ILink.hpp"
#include "Link/LinkFactory.hpp"
#include "Link/Symbolic/ISymbolicLink.hpp"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>

#include <cstdint>
#include <limits>
#include <optional>
#include <string>
#include <system_error>
#include <type_traits>
#include <utility>

namespace
{
    using backend::AdsConfigBackend;

    const QString kDefaultServerIp = QStringLiteral("127.0.0.1");
    const QString kDefaultServerNetId = QStringLiteral("127.0.0.1.1.1");
    const QString kDefaultLocalNetId = QStringLiteral("127.0.0.1.1.20");
    const QString kDefaultVariableType = QStringLiteral("int32");
    constexpr int kDefaultAdsPort = 851;

    const QStringList kVariableTypes{
        QStringLiteral("bool"),  QStringLiteral("char"),   QStringLiteral("int8"),  QStringLiteral("uint8"),
        QStringLiteral("int16"), QStringLiteral("uint16"), QStringLiteral("int32"), QStringLiteral("uint32"),
        QStringLiteral("int64"), QStringLiteral("uint64"), QStringLiteral("float"), QStringLiteral("double")
    };

    auto defaultSettings() -> AdsConfigBackend::AdsSettings
    {
        return { .serverIp = kDefaultServerIp,
                 .serverNetId = kDefaultServerNetId,
                 .localNetId = kDefaultLocalNetId,
                 .adsPort = kDefaultAdsPort,
                 .variableName = {},
                 .variableType = kDefaultVariableType };
    }

    auto errorToQString(const std::error_code& error) -> QString
    {
        if (!error) {
            return QString();
        }
        return QString::fromStdString(error.message());
    }

    auto isValidIpv4(const QString& text) -> bool
    {
        static const QRegularExpression pattern(
          QStringLiteral(R"(^((25[0-5]|2[0-4]\d|1\d\d|[1-9]?\d)\.){3}(25[0-5]|2[0-4]\d|1\d\d|[1-9]?\d)$)"));
        return pattern.match(text.trimmed()).hasMatch();
    }

    auto isValidNetId(const QString& text) -> bool
    {
        static const QRegularExpression pattern(
          QStringLiteral(R"(^((25[0-5]|2[0-4]\d|1\d\d|[1-9]?\d)\.){5}(25[0-5]|2[0-4]\d|1\d\d|[1-9]?\d)$)"));
        return pattern.match(text.trimmed()).hasMatch();
    }

    template<typename T>
    auto toQCoroTask(core::coro::Task<T>&& task) -> QCoro::Task<T>
    {
        co_return co_await std::move(task);
    }

    auto toQCoroTask(core::coro::Task<void>&& task) -> QCoro::Task<void> { co_await std::move(task); }

    template<typename Integer>
    auto parseInteger(const QString& text) -> std::optional<Integer>
    {
        bool ok = false;
        const auto value = text.trimmed().toLongLong(&ok, 10);
        if (!ok) {
            return std::nullopt;
        }

        if (value < std::numeric_limits<Integer>::min() || value > std::numeric_limits<Integer>::max()) {
            return std::nullopt;
        }

        return static_cast<Integer>(value);
    }

    template<typename Integer>
    auto parseUnsignedInteger(const QString& text) -> std::optional<Integer>
    {
        bool ok = false;
        const auto value = text.trimmed().toULongLong(&ok, 10);
        if (!ok) {
            return std::nullopt;
        }

        if (value > std::numeric_limits<Integer>::max()) {
            return std::nullopt;
        }

        return static_cast<Integer>(value);
    }

    template<typename T>
    auto formatValue(const T& value) -> QString
    {
        if constexpr (std::is_same_v<T, bool>) {
            return value ? QStringLiteral("true") : QStringLiteral("false");
        }
        else if constexpr (std::is_same_v<T, char>) {
            return QString(QChar::fromLatin1(value));
        }
        else if constexpr (std::is_floating_point_v<T>) {
            return QString::number(value, 'g', 12);
        }
        else {
            return QString::number(value);
        }
    }
}

namespace backend
{
    AdsConfigBackend::AdsConfigBackend(QObject* parent)
      : QObject(parent)
      , m_savedSettings(defaultSettings())
      , m_draftSettings(m_savedSettings)
    {
        loadConfig();
        m_variableValue.clear();
    }

    AdsConfigBackend::~AdsConfigBackend() { resetLink(); }

    auto AdsConfigBackend::serverIp() const -> QString { return m_draftSettings.serverIp; }
    auto AdsConfigBackend::serverNetId() const -> QString { return m_draftSettings.serverNetId; }
    auto AdsConfigBackend::localNetId() const -> QString { return m_draftSettings.localNetId; }
    auto AdsConfigBackend::adsPort() const -> int { return m_draftSettings.adsPort; }
    auto AdsConfigBackend::variableName() const -> QString { return m_draftSettings.variableName; }
    auto AdsConfigBackend::variableType() const -> QString { return m_draftSettings.variableType; }
    auto AdsConfigBackend::variableValue() const -> QString { return m_variableValue; }
    auto AdsConfigBackend::variableTypes() const -> QStringList { return kVariableTypes; }
    auto AdsConfigBackend::connectionState() const -> ConnectionState { return m_connectionState; }
    auto AdsConfigBackend::connected() const -> bool
    {
        return m_connectionState == ConnectionState::Connected;
    }
    auto AdsConfigBackend::canConnect() const -> bool
    {
        return m_connectionState != ConnectionState::Connecting &&
               m_connectionState != ConnectionState::Connected;
    }
    auto AdsConfigBackend::canDisconnect() const -> bool
    {
        return m_link != nullptr && m_connectionState != ConnectionState::Connecting;
    }
    auto AdsConfigBackend::canSave() const -> bool { return m_dirty; }
    auto AdsConfigBackend::canDiscard() const -> bool { return m_dirty; }
    auto AdsConfigBackend::dirty() const -> bool { return m_dirty; }
    auto AdsConfigBackend::statusMessage() const -> QString { return m_statusMessage; }
    auto AdsConfigBackend::statusIsError() const -> bool { return m_statusIsError; }

    auto AdsConfigBackend::connectionStateLabel() const -> QString
    {
        switch (m_connectionState) {
            case ConnectionState::Disconnected:
                return QStringLiteral("Disconnected");
            case ConnectionState::Connecting:
                return QStringLiteral("Connecting");
            case ConnectionState::Connected:
                return QStringLiteral("Connected");
            case ConnectionState::Error:
                return QStringLiteral("Error");
        }

        return QStringLiteral("Unknown");
    }

    void AdsConfigBackend::setServerIp(const QString& value)
    {
        const auto trimmed = value.trimmed();
        if (m_draftSettings.serverIp == trimmed) {
            return;
        }

        m_draftSettings.serverIp = trimmed;
        emit serverIpChanged();
        updateDirty();
    }

    void AdsConfigBackend::setServerNetId(const QString& value)
    {
        const auto trimmed = value.trimmed();
        if (m_draftSettings.serverNetId == trimmed) {
            return;
        }

        m_draftSettings.serverNetId = trimmed;
        emit serverNetIdChanged();
        updateDirty();
    }

    void AdsConfigBackend::setLocalNetId(const QString& value)
    {
        const auto trimmed = value.trimmed();
        if (m_draftSettings.localNetId == trimmed) {
            return;
        }

        m_draftSettings.localNetId = trimmed;
        emit localNetIdChanged();
        updateDirty();
    }

    void AdsConfigBackend::setAdsPort(int value)
    {
        if (m_draftSettings.adsPort == value || value <= 0 || value > 65535) {
            return;
        }

        m_draftSettings.adsPort = value;
        emit adsPortChanged();
        updateDirty();
    }

    void AdsConfigBackend::setVariableName(const QString& value)
    {
        if (m_draftSettings.variableName == value) {
            return;
        }

        m_draftSettings.variableName = value;
        emit variableNameChanged();
        updateDirty();
    }

    void AdsConfigBackend::setVariableType(const QString& value)
    {
        const auto selected = kVariableTypes.contains(value) ? value : kDefaultVariableType;
        if (m_draftSettings.variableType == selected) {
            return;
        }

        m_draftSettings.variableType = selected;
        emit variableTypeChanged();
        updateDirty();
    }

    void AdsConfigBackend::setVariableValue(const QString& value)
    {
        if (m_variableValue == value) {
            return;
        }

        m_variableValue = value;
        emit variableValueChanged();
    }

    void AdsConfigBackend::connectToAds()
    {
        if (!canConnect()) {
            return;
        }

        QString errorMessage;
        if (!validateSettings(errorMessage)) {
            setStatus(errorMessage, true);
            return;
        }

        launchTask(connectToAdsAsync());
    }

    void AdsConfigBackend::disconnectFromAds()
    {
        if (!canDisconnect()) {
            return;
        }

        launchTask(disconnectFromAdsAsync());
    }

    void AdsConfigBackend::readVariable()
    {
        if (!connected()) {
            setStatus(QStringLiteral("Connect to ADS before reading a variable."), true);
            return;
        }
        if (m_draftSettings.variableName.trimmed().isEmpty()) {
            setStatus(QStringLiteral("Provide an ADS variable name before reading."), true);
            return;
        }

        launchTask(readVariableAsync());
    }

    void AdsConfigBackend::writeVariable()
    {
        if (!connected()) {
            setStatus(QStringLiteral("Connect to ADS before writing a variable."), true);
            return;
        }
        if (m_draftSettings.variableName.trimmed().isEmpty()) {
            setStatus(QStringLiteral("Provide an ADS variable name before writing."), true);
            return;
        }

        launchTask(writeVariableAsync());
    }

    void AdsConfigBackend::saveConfig()
    {
        QString errorMessage;
        if (!validateSettings(errorMessage)) {
            setStatus(errorMessage, true);
            return;
        }

        const auto path = configFilePath(true);
        QFile file(path);
        if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
            setStatus(QStringLiteral("Failed to save ADS config: %1").arg(file.errorString()), true);
            return;
        }

        QJsonObject root;
        root.insert(QStringLiteral("serverIp"), m_draftSettings.serverIp);
        root.insert(QStringLiteral("serverNetId"), m_draftSettings.serverNetId);
        root.insert(QStringLiteral("localNetId"), m_draftSettings.localNetId);
        root.insert(QStringLiteral("adsPort"), m_draftSettings.adsPort);
        root.insert(QStringLiteral("variableName"), m_draftSettings.variableName);
        root.insert(QStringLiteral("variableType"), m_draftSettings.variableType);

        const auto bytes = QJsonDocument(root).toJson(QJsonDocument::Indented);
        if (file.write(bytes) != bytes.size()) {
            setStatus(QStringLiteral("Failed to write ADS config to %1.").arg(path), true);
            return;
        }

        m_savedSettings = m_draftSettings;
        updateDirty();
        setStatus(QStringLiteral("Saved ADS configuration to %1.").arg(QDir::toNativeSeparators(path)),
                  false);
    }

    void AdsConfigBackend::discardChanges()
    {
        applySettings(m_savedSettings);
        setStatus(QStringLiteral("Discarded unsaved ADS configuration changes."), false);
    }

    void AdsConfigBackend::loadConfig()
    {
        const auto path = configFilePath(false);
        QFile file(path);
        if (!file.exists()) {
            applySettings(defaultSettings());
            m_savedSettings = m_draftSettings;
            updateDirty();
            setStatus(QStringLiteral("Using default ADS configuration values."), false);
            return;
        }

        if (!file.open(QIODevice::ReadOnly)) {
            applySettings(defaultSettings());
            m_savedSettings = m_draftSettings;
            updateDirty();
            setStatus(QStringLiteral("Failed to load ADS config: %1").arg(file.errorString()), true);
            return;
        }

        QJsonParseError parseError;
        const auto document = QJsonDocument::fromJson(file.readAll(), &parseError);
        if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
            applySettings(defaultSettings());
            m_savedSettings = m_draftSettings;
            updateDirty();
            setStatus(QStringLiteral("Failed to parse ADS config: %1").arg(parseError.errorString()), true);
            return;
        }

        AdsSettings settings = defaultSettings();
        const auto object = document.object();
        settings.serverIp = object.value(QStringLiteral("serverIp")).toString(settings.serverIp);
        settings.serverNetId = object.value(QStringLiteral("serverNetId")).toString(settings.serverNetId);
        settings.localNetId = object.value(QStringLiteral("localNetId")).toString(settings.localNetId);
        settings.adsPort = object.value(QStringLiteral("adsPort")).toInt(settings.adsPort);
        settings.variableName = object.value(QStringLiteral("variableName")).toString(settings.variableName);
        settings.variableType = object.value(QStringLiteral("variableType")).toString(settings.variableType);

        if (!kVariableTypes.contains(settings.variableType)) {
            settings.variableType = kDefaultVariableType;
        }

        applySettings(settings);
        m_savedSettings = m_draftSettings;
        updateDirty();
        setStatus(QStringLiteral("Loaded ADS configuration from %1.").arg(QDir::toNativeSeparators(path)),
                  false);
    }

    void AdsConfigBackend::applySettings(const AdsSettings& settings)
    {
        const auto previous = m_draftSettings;
        m_draftSettings = settings;

        if (previous.serverIp != m_draftSettings.serverIp) {
            emit serverIpChanged();
        }
        if (previous.serverNetId != m_draftSettings.serverNetId) {
            emit serverNetIdChanged();
        }
        if (previous.localNetId != m_draftSettings.localNetId) {
            emit localNetIdChanged();
        }
        if (previous.adsPort != m_draftSettings.adsPort) {
            emit adsPortChanged();
        }
        if (previous.variableName != m_draftSettings.variableName) {
            emit variableNameChanged();
        }
        if (previous.variableType != m_draftSettings.variableType) {
            emit variableTypeChanged();
        }

        updateDirty();
    }

    void AdsConfigBackend::updateDirty()
    {
        const bool nextDirty = m_draftSettings != m_savedSettings;
        if (m_dirty == nextDirty) {
            return;
        }

        m_dirty = nextDirty;
        emit dirtyChanged();
    }

    void AdsConfigBackend::setConnectionState(ConnectionState state)
    {
        if (m_connectionState == state) {
            return;
        }

        m_connectionState = state;
        emit connectionStateChanged();
    }

    void AdsConfigBackend::setStatus(const QString& message, bool isError)
    {
        if (m_statusMessage == message && m_statusIsError == isError) {
            return;
        }

        m_statusMessage = message;
        m_statusIsError = isError;
        emit statusChanged();
    }

    void AdsConfigBackend::resetLink()
    {
        m_symbolicLink = nullptr;
        m_link.reset();
    }

    auto AdsConfigBackend::validateSettings(QString& errorMessage) const -> bool
    {
        if (!isValidIpv4(m_draftSettings.serverIp)) {
            errorMessage = QStringLiteral("ADS server IP address must be a valid IPv4 address.");
            return false;
        }

        if (!isValidNetId(m_draftSettings.serverNetId)) {
            errorMessage = QStringLiteral("ADS server Net ID must contain six numeric segments.");
            return false;
        }

        if (!isValidNetId(m_draftSettings.localNetId)) {
            errorMessage = QStringLiteral("Local Net ID must contain six numeric segments.");
            return false;
        }

        if (m_draftSettings.adsPort <= 0 || m_draftSettings.adsPort > 65535) {
            errorMessage = QStringLiteral("ADS port must be between 1 and 65535.");
            return false;
        }

        return true;
    }

    auto AdsConfigBackend::configFilePath(bool createDirectory) const -> QString
    {
        const QDir appDir(QCoreApplication::applicationDirPath());
        const QStringList candidateDirs{ appDir.filePath(QStringLiteral("config")),
                                         appDir.filePath(QStringLiteral("../../config")) };

        for (const auto& directory : candidateDirs) {
            const auto candidateFile = QDir(directory).filePath(QStringLiteral("ads_config.json"));
            if (QFileInfo::exists(candidateFile)) {
                return QDir::cleanPath(candidateFile);
            }
        }

        for (const auto& directory : candidateDirs) {
            if (QFileInfo(directory).exists()) {
                return QDir(directory).filePath(QStringLiteral("ads_config.json"));
            }
        }

        const auto fallbackDirectory = candidateDirs.front();
        if (createDirectory) {
            QDir().mkpath(fallbackDirectory);
        }
        return QDir(fallbackDirectory).filePath(QStringLiteral("ads_config.json"));
    }

    void AdsConfigBackend::launchTask(QCoro::Task<void>&& task)
    {
        auto guarded = std::move(task).then([]() {}, [this](const std::exception& exception) {
            setConnectionState(ConnectionState::Error);
            setStatus(QStringLiteral("Unexpected ADS backend error: %1").arg(exception.what()), true);
        });

        QCoro::connect(std::move(guarded), this, []() {});
    }

    auto AdsConfigBackend::connectToAdsAsync() -> QCoro::Task<void>
    {
        setConnectionState(ConnectionState::Connecting);
        setStatus(QStringLiteral("Connecting to ADS server..."), false);

        if (m_link) {
            auto* previousClient = m_link->asClient();
            if (previousClient) {
                (void)co_await toQCoroTask(previousClient->disconnect());
            }
            resetLink();
        }

        core::link::LinkConfig config;
        config.ip = m_draftSettings.serverIp.toStdString();
        config.port = static_cast<uint16_t>(m_draftSettings.adsPort);
        config.localNetId = m_draftSettings.localNetId.toStdString();
        config.remoteNetId = m_draftSettings.serverNetId.toStdString();

        auto linkResult = core::link::create(
          core::link::Role::Client, core::link::Mode::Symbolic, core::link::Protocol::Ads, config);
        if (!linkResult) {
            setConnectionState(ConnectionState::Error);
            setStatus(QStringLiteral("Failed to create ADS link: %1").arg(errorToQString(linkResult.error())),
                      true);
            co_return;
        }

        auto link = std::move(linkResult).value();
        auto* client = link->asClient();
        auto* symbolicLink = link->asSymbolic();
        if (!client || !symbolicLink) {
            setConnectionState(ConnectionState::Error);
            setStatus(
              QStringLiteral("The ADS link was created without the expected client/symbolic interfaces."),
              true);
            co_return;
        }

        auto connectResult = co_await toQCoroTask(client->connect());
        if (!connectResult) {
            setConnectionState(ConnectionState::Error);
            setStatus(QStringLiteral("ADS connect failed: %1").arg(errorToQString(connectResult.error())),
                      true);
            co_return;
        }

        m_symbolicLink = symbolicLink;
        m_link = std::move(link);
        setConnectionState(ConnectionState::Connected);
        setStatus(QStringLiteral("Connected to %1 (%2) on ADS port %3.")
                    .arg(m_draftSettings.serverIp, m_draftSettings.serverNetId)
                    .arg(m_draftSettings.adsPort),
                  false);
    }

    auto AdsConfigBackend::disconnectFromAdsAsync() -> QCoro::Task<void>
    {
        if (!m_link) {
            setConnectionState(ConnectionState::Disconnected);
            setStatus(QStringLiteral("ADS connection already disconnected."), false);
            co_return;
        }

        auto* client = m_link->asClient();
        if (client) {
            auto disconnectResult = co_await toQCoroTask(client->disconnect());
            if (!disconnectResult) {
                setConnectionState(ConnectionState::Error);
                setStatus(
                  QStringLiteral("ADS disconnect failed: %1").arg(errorToQString(disconnectResult.error())),
                  true);
                co_return;
            }
        }

        resetLink();
        setConnectionState(ConnectionState::Disconnected);
        setStatus(QStringLiteral("Disconnected from ADS server."), false);
    }

    auto AdsConfigBackend::readVariableAsync() -> QCoro::Task<void>
    {
        const auto type = m_draftSettings.variableType;
        if (type == QStringLiteral("bool")) {
            co_await readValueAs<bool>();
            co_return;
        }
        if (type == QStringLiteral("char")) {
            co_await readValueAs<char>();
            co_return;
        }
        if (type == QStringLiteral("int8")) {
            co_await readValueAs<std::int8_t>();
            co_return;
        }
        if (type == QStringLiteral("uint8")) {
            co_await readValueAs<std::uint8_t>();
            co_return;
        }
        if (type == QStringLiteral("int16")) {
            co_await readValueAs<std::int16_t>();
            co_return;
        }
        if (type == QStringLiteral("uint16")) {
            co_await readValueAs<std::uint16_t>();
            co_return;
        }
        if (type == QStringLiteral("int32")) {
            co_await readValueAs<std::int32_t>();
            co_return;
        }
        if (type == QStringLiteral("uint32")) {
            co_await readValueAs<std::uint32_t>();
            co_return;
        }
        if (type == QStringLiteral("int64")) {
            co_await readValueAs<std::int64_t>();
            co_return;
        }
        if (type == QStringLiteral("uint64")) {
            co_await readValueAs<std::uint64_t>();
            co_return;
        }
        if (type == QStringLiteral("float")) {
            co_await readValueAs<float>();
            co_return;
        }
        if (type == QStringLiteral("double")) {
            co_await readValueAs<double>();
            co_return;
        }

        setStatus(QStringLiteral("Unsupported ADS variable type: %1").arg(type), true);
    }

    auto AdsConfigBackend::writeVariableAsync() -> QCoro::Task<void>
    {
        const auto type = m_draftSettings.variableType;
        if (type == QStringLiteral("bool")) {
            co_await writeValueAs<bool>();
            co_return;
        }
        if (type == QStringLiteral("char")) {
            co_await writeValueAs<char>();
            co_return;
        }
        if (type == QStringLiteral("int8")) {
            co_await writeValueAs<std::int8_t>();
            co_return;
        }
        if (type == QStringLiteral("uint8")) {
            co_await writeValueAs<std::uint8_t>();
            co_return;
        }
        if (type == QStringLiteral("int16")) {
            co_await writeValueAs<std::int16_t>();
            co_return;
        }
        if (type == QStringLiteral("uint16")) {
            co_await writeValueAs<std::uint16_t>();
            co_return;
        }
        if (type == QStringLiteral("int32")) {
            co_await writeValueAs<std::int32_t>();
            co_return;
        }
        if (type == QStringLiteral("uint32")) {
            co_await writeValueAs<std::uint32_t>();
            co_return;
        }
        if (type == QStringLiteral("int64")) {
            co_await writeValueAs<std::int64_t>();
            co_return;
        }
        if (type == QStringLiteral("uint64")) {
            co_await writeValueAs<std::uint64_t>();
            co_return;
        }
        if (type == QStringLiteral("float")) {
            co_await writeValueAs<float>();
            co_return;
        }
        if (type == QStringLiteral("double")) {
            co_await writeValueAs<double>();
            co_return;
        }

        setStatus(QStringLiteral("Unsupported ADS variable type: %1").arg(type), true);
    }

    template<typename T>
    auto AdsConfigBackend::readValueAs() -> QCoro::Task<void>
    {
        auto result =
          co_await toQCoroTask(m_symbolicLink->read<T>(m_draftSettings.variableName.toStdString()));
        if (!result) {
            setStatus(QStringLiteral("ADS read failed: %1").arg(errorToQString(result.error())), true);
            co_return;
        }

        setVariableValue(formatValue(result.value()));
        setStatus(QStringLiteral("Read ADS variable %1 as %2.")
                    .arg(m_draftSettings.variableName, m_draftSettings.variableType),
                  false);
    }

    template<typename T>
    auto AdsConfigBackend::writeValueAs() -> QCoro::Task<void>
    {
        std::optional<T> parsed;

        if constexpr (std::is_same_v<T, bool>) {
            const auto trimmed = m_variableValue.trimmed().toLower();
            if (trimmed == QStringLiteral("true") || trimmed == QStringLiteral("1")) {
                parsed = true;
            }
            else if (trimmed == QStringLiteral("false") || trimmed == QStringLiteral("0")) {
                parsed = false;
            }
        }
        else if constexpr (std::is_same_v<T, char>) {
            const auto trimmed = m_variableValue.trimmed();
            if (trimmed.size() == 1) {
                parsed = trimmed.at(0).toLatin1();
            }
        }
        else if constexpr (std::is_same_v<T, std::int8_t>) {
            parsed = parseInteger<std::int8_t>(m_variableValue);
        }
        else if constexpr (std::is_same_v<T, std::uint8_t>) {
            parsed = parseUnsignedInteger<std::uint8_t>(m_variableValue);
        }
        else if constexpr (std::is_same_v<T, std::int16_t>) {
            parsed = parseInteger<std::int16_t>(m_variableValue);
        }
        else if constexpr (std::is_same_v<T, std::uint16_t>) {
            parsed = parseUnsignedInteger<std::uint16_t>(m_variableValue);
        }
        else if constexpr (std::is_same_v<T, std::int32_t>) {
            parsed = parseInteger<std::int32_t>(m_variableValue);
        }
        else if constexpr (std::is_same_v<T, std::uint32_t>) {
            parsed = parseUnsignedInteger<std::uint32_t>(m_variableValue);
        }
        else if constexpr (std::is_same_v<T, std::int64_t>) {
            parsed = parseInteger<std::int64_t>(m_variableValue);
        }
        else if constexpr (std::is_same_v<T, std::uint64_t>) {
            parsed = parseUnsignedInteger<std::uint64_t>(m_variableValue);
        }
        else if constexpr (std::is_same_v<T, float>) {
            bool ok = false;
            const auto value = m_variableValue.trimmed().toFloat(&ok);
            if (ok) {
                parsed = value;
            }
        }
        else if constexpr (std::is_same_v<T, double>) {
            bool ok = false;
            const auto value = m_variableValue.trimmed().toDouble(&ok);
            if (ok) {
                parsed = value;
            }
        }

        if (!parsed.has_value()) {
            setStatus(QStringLiteral("Value '%1' is not valid for ADS type %2.")
                        .arg(m_variableValue, m_draftSettings.variableType),
                      true);
            co_return;
        }

        auto result = co_await toQCoroTask(
          m_symbolicLink->write<T>(m_draftSettings.variableName.toStdString(), parsed.value()));
        if (!result) {
            setStatus(QStringLiteral("ADS write failed: %1").arg(errorToQString(result.error())), true);
            co_return;
        }

        setStatus(QStringLiteral("Wrote ADS variable %1 as %2.")
                    .arg(m_draftSettings.variableName, m_draftSettings.variableType),
                  false);
    }
}