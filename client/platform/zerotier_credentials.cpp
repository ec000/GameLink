#include "zerotier_credentials.hpp"
#include <QDir>
#define S(x) QStringLiteral(x)
namespace gamelink {
QString legacyGameLinkTokenPath() {
#ifdef Q_OS_WIN
 return qEnvironmentVariable("LOCALAPPDATA")+S("/GameLink/zerotier-token.secret");
#else
 return QDir::homePath()+S("/.config/gamelink/zerotier-token.secret");
#endif
}
QStringList windowsCredentialPaths(const QString& localAppData,const QString& programData) {
 QStringList paths;
 if(!localAppData.isEmpty())paths<<QDir(localAppData).filePath(S("ZeroTier/One/authtoken.secret"))
                                  <<QDir(localAppData).filePath(S("ZeroTier/authtoken.secret"));
 if(!programData.isEmpty())paths<<QDir(programData).filePath(S("ZeroTier/One/authtoken.secret"));
 return paths;
}
QStringList zeroTierCredentialPaths() {
#ifdef Q_OS_WIN
 return windowsCredentialPaths(qEnvironmentVariable("LOCALAPPDATA"),qEnvironmentVariable("PROGRAMDATA"));
#elif defined(Q_OS_MACOS)
 return {QDir::homePath()+S("/Library/Application Support/ZeroTier/One/authtoken.secret"),
         S("/Library/Application Support/ZeroTier/One/authtoken.secret")};
#else
 return {S("/var/lib/zerotier-one/authtoken.secret")};
#endif
}
}
