#include "zerotier_environment.hpp"
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QRegularExpression>
#include <vector>
#ifdef Q_OS_WIN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <winsvc.h>
#endif
#define S(x) QStringLiteral(x)

namespace gamelink {
ServiceState windowsServiceState(quint32 state) {
    switch (state) { // SERVICE_* constants from winsvc.h; also testable off Windows.
    case 1: return ServiceState::Stopped;
    case 2: return ServiceState::Starting;
    case 3: return ServiceState::Stopping;
    case 4: return ServiceState::Running;
    case 5: case 6: return ServiceState::Changing;
    case 7: return ServiceState::Paused;
    default: return ServiceState::Unknown;
    }
}
ServiceState windowsServiceError(quint32 error) {
    if (error == 1060) return ServiceState::NotFound; // ERROR_SERVICE_DOES_NOT_EXIST
    if (error == 5) return ServiceState::AccessDenied;
    return ServiceState::Unknown;
}
QString serviceExecutable(const QString& commandLine) {
    const auto text = commandLine.trimmed();
    if (text.startsWith(QLatin1Char('"'))) {
        const auto end = text.indexOf(QLatin1Char('"'), 1);
        return end > 1 ? text.mid(1, end - 1) : QString{};
    }
    const auto match = QRegularExpression(S("\\.exe(?=\\s|$)"), QRegularExpression::CaseInsensitiveOption).match(text);
    return match.hasMatch() ? text.left(match.capturedEnd()) : QString{};
}
ServiceInfo queryWindowsService(const QString& name) {
    ServiceInfo result;
#ifdef Q_OS_WIN
    struct Handle {
        SC_HANDLE value;
        explicit Handle(SC_HANDLE h) : value(h) {}
        ~Handle() { if (value) CloseServiceHandle(value); }
        Handle(const Handle&) = delete;
        Handle& operator=(const Handle&) = delete;
    };
    Handle manager(OpenSCManagerW(nullptr, nullptr, SC_MANAGER_CONNECT));
    if (!manager.value) { result.errorCode = GetLastError(); result.state = windowsServiceError(result.errorCode); return result; }
    const auto wideName = name.toStdWString();
    Handle service(OpenServiceW(manager.value, wideName.c_str(), SERVICE_QUERY_STATUS));
    if (!service.value) { result.errorCode = GetLastError(); result.state = windowsServiceError(result.errorCode); return result; }
    result.registered = true;
    SERVICE_STATUS_PROCESS status{}; DWORD needed = 0;
    if (QueryServiceStatusEx(service.value, SC_STATUS_PROCESS_INFO, reinterpret_cast<LPBYTE>(&status), sizeof(status), &needed)) {
        result.state = windowsServiceState(status.dwCurrentState);
    } else { result.errorCode = GetLastError(); result.state = windowsServiceError(result.errorCode); }
    // Config needs a separate read right. A denial must not erase a successful status query.
    Handle config(OpenServiceW(manager.value, wideName.c_str(), SERVICE_QUERY_CONFIG));
    if (!config.value) { result.configErrorCode = GetLastError(); return result; }
    needed = 0;
    QueryServiceConfigW(config.value, nullptr, 0, &needed);
    if (GetLastError() != ERROR_INSUFFICIENT_BUFFER || needed == 0 || needed > 8192) {
        result.configErrorCode = GetLastError(); return result;
    }
    std::vector<quintptr> buffer((needed + sizeof(quintptr) - 1) / sizeof(quintptr));
    auto* cfg = reinterpret_cast<QUERY_SERVICE_CONFIGW*>(buffer.data());
    if (!QueryServiceConfigW(config.value, cfg, needed, &needed)) { result.configErrorCode = GetLastError(); return result; }
    const auto command = QString::fromWCharArray(cfg->lpBinaryPathName);
    auto executable = serviceExecutable(command);
    const auto input = executable.toStdWString();
    const DWORD size = ExpandEnvironmentStringsW(input.c_str(), nullptr, 0);
    if (size > 0 && size <= 32768) {
        std::vector<wchar_t> expanded(size);
        const auto written = ExpandEnvironmentStringsW(input.c_str(), expanded.data(), size);
        if (written > 0 && written <= size) executable = QString::fromWCharArray(expanded.data());
    }
    result.executable = executable; // Command arguments are discarded, never logged or executed.
#else
    Q_UNUSED(name);
    result.state = ServiceState::Unsupported;
#endif
    return result;
}
EnvironmentReport inspectInstallation(const ServiceInfo& service, const QStringList& executables, const QString& dataDirectory) {
    EnvironmentReport report; report.service = service;
    auto candidates = executables;
    if (!service.executable.isEmpty()) candidates.prepend(service.executable);
    for (const auto& candidate : candidates) {
        if (QFileInfo(candidate).isFile()) { report.installation = InstallationState::Installed; report.executable = candidate; break; }
    }
    if (report.executable.isEmpty()) {
        if (service.registered) report.installation = service.executable.isEmpty() ? InstallationState::Registered : InstallationState::BrokenRegistration;
        else if (!dataDirectory.isEmpty() && QDir(dataDirectory).exists()) report.installation = InstallationState::DataOnly;
        else if (service.state == ServiceState::NotFound) report.installation = InstallationState::NotDetected;
        else report.installation = InstallationState::Unknown;
    }
    // This file contains only the local API port, never the authentication token.
    QFile portFile(QDir(dataDirectory).filePath(S("zerotier-one.port")));
    if (dataDirectory.isEmpty() || !QFileInfo(portFile).isFile()) report.portDetail = S("未发现可读取的端口文件；Settings 中的端口仍由用户控制");
    else if (!portFile.open(QIODevice::ReadOnly)) report.portDetail = S("端口文件不可读；请检查当前用户的读取权限");
    else {
        const auto bytes = portFile.read(33); bool valid = false;
        const auto port = QString::fromLatin1(bytes.trimmed()).toInt(&valid);
        if (bytes.size() <= 32 && valid && port > 0 && port <= 65535) {
            report.detectedPort = port; report.portDetail = S("端口文件报告 API 端口 %1（尚未证明该端口可访问）").arg(port);
        } else report.portDetail = S("端口文件格式无效；请核对 ZeroTier 的本地 API 端口");
    }
    return report;
}
EnvironmentReport inspectZeroTierEnvironment() {
#ifdef Q_OS_WIN
    QStringList candidates;
    for (const auto& root : {qEnvironmentVariable("ProgramW6432"), qEnvironmentVariable("ProgramFiles"), qEnvironmentVariable("ProgramFiles(x86)")}) {
        if (root.isEmpty()) continue;
        for (const auto& folder : {S("ZeroTier/One"), S("ZeroTier One")})
            for (const auto& binary : {S("zerotier-one_x64.exe"), S("zerotier-one_x86.exe"), S("zerotier-one.exe")})
                candidates << QDir(root).filePath(folder + QLatin1Char('/') + binary);
    }
    const auto programData = qEnvironmentVariable("PROGRAMDATA");
    return inspectInstallation(queryWindowsService(S("ZeroTierOneService")), candidates,
                               programData.isEmpty() ? QString{} : QDir(programData).filePath(S("ZeroTier/One")));
#else
    EnvironmentReport report; report.installation = InstallationState::Unsupported;
    report.service.state = ServiceState::Unsupported;
    report.portDetail = S("自动安装/服务检测仅支持 Windows；本地 API 检测仍可使用");
    return report;
#endif
}
QString installationDescription(const EnvironmentReport& report) {
    switch (report.installation) {
    case InstallationState::Installed: return S("已发现 ZeroTier 程序：%1").arg(QDir::toNativeSeparators(report.executable));
    case InstallationState::Registered: return S("服务已注册；程序路径未确认（配置读取可能受限）");
    case InstallationState::BrokenRegistration: return S("服务已注册，但注册路径未发现程序文件；请检查或修复官方安装");
    case InstallationState::DataOnly: return S("仅发现数据目录，不能确认当前仍已安装 ZeroTier");
    case InstallationState::NotDetected: return S("未发现标准安装：服务不存在，常见目录未发现程序；自定义/便携安装需人工核对");
    case InstallationState::Unknown: return S("未知：检测受限，不能据此认定未安装");
    case InstallationState::Unsupported: return S("当前平台不支持 Windows 安装检测");
    }
    return {};
}
QString serviceDescription(const ServiceInfo& info) {
    QString text;
    switch (info.state) {
    case ServiceState::Running: text = S("运行中（Running）"); break;
    case ServiceState::Stopped: text = S("已停止（Stopped）；可在 services.msc 中手动启动 ZeroTierOneService"); break;
    case ServiceState::Starting: text = S("正在启动；请稍后重新检测"); break;
    case ServiceState::Stopping: text = S("正在停止"); break;
    case ServiceState::Paused: text = S("已暂停；请在 Windows 服务管理器中检查"); break;
    case ServiceState::Changing: text = S("服务正在切换状态；请稍后重新检测"); break;
    case ServiceState::NotFound: text = S("服务未注册（ZeroTierOneService）；请核对官方安装"); break;
    case ServiceState::AccessDenied: text = S("查询权限不足，运行状态未知；不要因此把 GUI 以管理员身份运行"); break;
    case ServiceState::Unknown: text = S("查询失败，运行状态未知"); break;
    case ServiceState::Unsupported: text = S("当前平台不支持 Windows 服务查询"); break;
    }
    if (info.errorCode) text += S("；Windows 错误码 %1").arg(info.errorCode);
    if (info.configErrorCode) text += S("；服务配置读取错误码 %1（不影响已读取的运行状态）").arg(info.configErrorCode);
    return text;
}
}
