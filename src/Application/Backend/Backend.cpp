#include "Backend.h"

namespace backend
{
    Backend::Backend(QObject* parent)
      : QObject(parent)
      , m_adsConfig(new AdsConfigBackend(this))
    {
    }

    auto Backend::adsConfig() const -> AdsConfigBackend* { return m_adsConfig; }

    QString Backend::welcomeMessage() const { return QStringLiteral("Hello from C++ Backend!"); }
}
