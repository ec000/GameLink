#pragma once
#include <QStringList>
#include <functional>

namespace gamelink {
using CredentialLocator = std::function<QStringList()>;
QString legacyGameLinkTokenPath();
QStringList windowsCredentialPaths(const QString& localAppData, const QString& programData);
QStringList zeroTierCredentialPaths();
}
