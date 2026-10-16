#pragma once
#include <QString>
#include <QStringList>
#include <functional>

namespace gamelink {
enum class ServiceState { Running, Stopped, Starting, Stopping, Paused, Changing, NotFound, AccessDenied, Unknown, Unsupported };
enum class InstallationState { Installed, Registered, BrokenRegistration, DataOnly, NotDetected, Unknown, Unsupported };
struct ServiceInfo {
    ServiceState state = ServiceState::Unknown;
    bool registered = false;
    QString executable;
    quint32 errorCode = 0;
    quint32 configErrorCode = 0;
};
struct EnvironmentReport {
    InstallationState installation = InstallationState::Unknown;
    ServiceInfo service;
    QString executable;
    int detectedPort = 0;
    QString portDetail;
};
using EnvironmentInspector = std::function<EnvironmentReport()>;
ServiceState windowsServiceState(quint32 state);
ServiceState windowsServiceError(quint32 error);
QString serviceExecutable(const QString& commandLine);
ServiceInfo queryWindowsService(const QString& name);
EnvironmentReport inspectInstallation(const ServiceInfo&, const QStringList& executables, const QString& dataDirectory);
EnvironmentReport inspectZeroTierEnvironment();
QString installationDescription(const EnvironmentReport&);
QString serviceDescription(const ServiceInfo&);
}
