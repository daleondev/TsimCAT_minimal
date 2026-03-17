#include "RotaryTableBackend.h"

#include "Link/Symbolic/ISymbolicLink.hpp"

#include <utility>

namespace
{
    template<typename T>
    auto toQCoroTask(core::coro::Task<T>&& task) -> QCoro::Task<T>
    {
        co_return co_await std::move(task);
    }
}

namespace backend
{
    RotaryTableBackend::RotaryTableBackend(QObject* parent)
      : QObject(parent)
    {
    }

    RotaryTableBackend::~RotaryTableBackend()
    {
        if (m_symbolicLink && m_actualPositionSubscription.isValid()) {
            m_symbolicLink->unsubscribeRawSync(m_actualPositionSubscription.id());
        }
    }

    auto RotaryTableBackend::angleDegrees() const -> double { return m_angleDegrees; }

    void RotaryTableBackend::subscribeActualPosition(core::link::ISymbolicLink* symbolicLink,
                                                     const QString& variableName)
    {
        if (m_symbolicLink && m_actualPositionSubscription.isValid()) {
            m_symbolicLink->unsubscribeRawSync(m_actualPositionSubscription.id());
        }

        m_symbolicLink = symbolicLink;
        m_actualPositionSubscription = {};

        if (!m_symbolicLink || variableName.trimmed().isEmpty()) {
            return;
        }

        launchTask(subscribeActualPositionAsync(variableName));
    }

    void RotaryTableBackend::launchTask(QCoro::Task<void>&& task)
    {
        auto guarded = std::move(task).then([]() {}, [](const std::exception&) {});
        QCoro::connect(std::move(guarded), this, []() {});
    }

    void RotaryTableBackend::setAngleDegrees(double value)
    {
        if (qFuzzyCompare(m_angleDegrees, value)) {
            return;
        }

        m_angleDegrees = value;
        emit angleDegreesChanged();
    }

    auto RotaryTableBackend::subscribeActualPositionAsync(QString variableName) -> QCoro::Task<void>
    {
        if (!m_symbolicLink || variableName.trimmed().isEmpty()) {
            co_return;
        }

        auto subscriptionResult =
          co_await toQCoroTask(m_symbolicLink->subscribe<double>(variableName.toStdString()));
        if (!subscriptionResult) {
            co_return;
        }

        m_actualPositionSubscription = std::move(subscriptionResult).value();
        launchTask(consumeActualPositionAsync());
    }

    auto RotaryTableBackend::consumeActualPositionAsync() -> QCoro::Task<void>
    {
        while (m_actualPositionSubscription.isValid()) {
            auto nextValue = co_await toQCoroTask(m_actualPositionSubscription.stream.next());
            if (!nextValue.has_value()) {
                break;
            }

            setAngleDegrees(nextValue.value());
        }
    }
}