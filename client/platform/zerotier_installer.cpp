#include "zerotier_installer.hpp"
#include <QDir>
#include <QFileInfo>
#include <QPointer>
#include <QRegularExpression>
#include <QTimer>
#include <vector>
#ifdef Q_OS_WIN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef _WIN32_MSI
#define _WIN32_MSI 500
#endif
#include <windows.h>
#include <shellapi.h>
#include <msi.h>
#include <msiquery.h>
#include <wincrypt.h>
#include <wintrust.h>
#include <softpub.h>
#endif
#define S(x) QStringLiteral(x)
namespace gamelink {
namespace {
QString normalizedPublisher(QString value) {
    value.remove(QRegularExpression(S("[\\s,.]")));
    return value.toCaseFolded();
}
#ifdef Q_OS_WIN
struct FileLock {
    HANDLE handle = INVALID_HANDLE_VALUE;
    ~FileLock() { if (handle != INVALID_HANDLE_VALUE) CloseHandle(handle); }
};
struct MsiHandle {
    MSIHANDLE handle = 0;
    ~MsiHandle() { if (handle) MsiCloseHandle(handle); }
};
QString msiString(MSIHANDLE record, UINT field) {
    DWORD size = 0; wchar_t empty = L'\0';
    if (MsiRecordGetStringW(record, field, &empty, &size) != ERROR_MORE_DATA) return {};
    std::vector<wchar_t> buffer(size + 1); DWORD capacity = size + 1;
    if (MsiRecordGetStringW(record, field, buffer.data(), &capacity) != ERROR_SUCCESS) return {};
    return QString::fromWCharArray(buffer.data());
}
QString packageProperty(MSIHANDLE database, const wchar_t* property) {
    MsiHandle view, record;
    const auto query = S("SELECT `Value` FROM `Property` WHERE `Property`='%1'").arg(QString::fromWCharArray(property)).toStdWString();
    if (MsiDatabaseOpenViewW(database, query.c_str(), &view.handle) != ERROR_SUCCESS ||
        MsiViewExecute(view.handle, 0) != ERROR_SUCCESS || MsiViewFetch(view.handle, &record.handle) != ERROR_SUCCESS) return {};
    return msiString(record.handle, 1);
}
QString productProperty(const wchar_t* code, const wchar_t* property) {
    DWORD size = 0; wchar_t empty = L'\0';
    const auto result = MsiGetProductInfoExW(code, nullptr, MSIINSTALLCONTEXT_MACHINE, property, &empty, &size);
    if (result != ERROR_MORE_DATA && result != ERROR_SUCCESS) return {};
    std::vector<wchar_t> buffer(size + 1); DWORD capacity = size + 1;
    if (MsiGetProductInfoExW(code, nullptr, MSIINSTALLCONTEXT_MACHINE, property, buffer.data(), &capacity) != ERROR_SUCCESS) return {};
    return QString::fromWCharArray(buffer.data());
}
QString trustedPublisher(const QString& path, HANDLE handle, QString& error) {
    const auto widePath = QDir::toNativeSeparators(path).toStdWString();
    WINTRUST_FILE_INFO file{}; file.cbStruct = sizeof(file); file.pcwszFilePath = widePath.c_str(); file.hFile = handle;
    WINTRUST_DATA data{}; data.cbStruct = sizeof(data); data.dwUIChoice = WTD_UI_NONE;
    data.fdwRevocationChecks = WTD_REVOKE_WHOLECHAIN; data.dwUnionChoice = WTD_CHOICE_FILE;
    data.pFile = &file; data.dwStateAction = WTD_STATEACTION_VERIFY;
    // No network waits on the UI thread. Missing/stale trust information fails closed.
    data.dwProvFlags = WTD_CACHE_ONLY_URL_RETRIEVAL | WTD_REVOCATION_CHECK_CHAIN_EXCLUDE_ROOT;
    GUID policy = WINTRUST_ACTION_GENERIC_VERIFY_V2;
    const LONG result = WinVerifyTrust(nullptr, &policy, &data);
    QString publisher;
    if (result == ERROR_SUCCESS) {
        auto* provider = WTHelperProvDataFromStateData(data.hWVTStateData);
        auto* signer = provider ? WTHelperGetProvSignerFromChain(provider, 0, FALSE, 0) : nullptr;
        if (signer && signer->csCertChain > 0) {
            auto* certificate = signer->pasCertChain[0].pCert;
            const DWORD length = CertGetNameStringW(certificate, CERT_NAME_ATTR_TYPE, 0,
                                                    const_cast<char*>(szOID_ORGANIZATION_NAME), nullptr, 0);
            if (length > 1 && length < 4096) {
                std::vector<wchar_t> buffer(length);
                CertGetNameStringW(certificate, CERT_NAME_ATTR_TYPE, 0,
                                   const_cast<char*>(szOID_ORGANIZATION_NAME), buffer.data(), length);
                publisher = QString::fromWCharArray(buffer.data());
            }
        }
    }
    data.dwStateAction = WTD_STATEACTION_CLOSE;
    WinVerifyTrust(nullptr, &policy, &data);
    if (result != ERROR_SUCCESS) error = S("[MSI_SIGNATURE] 无法验证安装包签名/证书链（0x%1）。请使用官方完整 MSI，并检查 Windows 证书与吊销缓存。").arg(static_cast<quint32>(result), 8, 16, QLatin1Char('0'));
    else if (normalizedPublisher(publisher) != S("zerotierinc")) error = S("[MSI_PUBLISHER] 安装包签名发布者不是受支持的 ZeroTier, Inc.，已拒绝执行。");
    return error.isEmpty() ? publisher : QString{};
}
InstallerPackage inspectLockedPackage(const QString& input, const std::shared_ptr<FileLock>& lock) {
    InstallerPackage package;
    const QFileInfo info(input);
    if (!info.isAbsolute() || !info.isFile() || info.isSymLink() || info.suffix().compare(S("msi"), Qt::CaseInsensitive) != 0) {
        package.error = S("[MSI_PATH] 请选择本机的完整 .msi 文件（不支持链接、目录或远程路径）。"); return package;
    }
    package.path = info.canonicalFilePath();
    const auto native = QDir::toNativeSeparators(package.path);
    if (native.startsWith(S("\\\\")) || native.contains(QLatin1Char('"')) || native.contains(QLatin1Char('\n')) || native.contains(QLatin1Char('\r'))) {
        package.error = S("[MSI_PATH] 安装包必须位于本机磁盘，路径中不能包含控制字符。"); return package;
    }
    const auto root = native.left(3).toStdWString();
    if (!QRegularExpression(S("^[A-Za-z]:\\\\")).match(native).hasMatch() ||
        native.mid(2).contains(QLatin1Char(':')) || GetDriveTypeW(root.c_str()) == DRIVE_REMOTE) {
        package.error = S("[MSI_PATH] 不支持网络映射磁盘或替代数据流，请将官方 MSI 复制到本机普通文件后重试。"); return package;
    }
    const auto widePath = native.toStdWString();
    lock->handle = CreateFileW(widePath.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (lock->handle == INVALID_HANDLE_VALUE) {
        package.error = S("[MSI_READ] 无法锁定安装包供只读验证，Windows 错误码 %1。").arg(GetLastError()); return package;
    }
    package.publisher = trustedPublisher(package.path, lock->handle, package.error);
    if (!package.error.isEmpty()) return package;
    MsiHandle database;
    const auto opened = MsiOpenDatabaseW(widePath.c_str(), MSIDBOPEN_READONLY, &database.handle);
    if (opened != ERROR_SUCCESS) { package.error = S("[MSI_DATABASE] 无法读取 MSI 属性，Windows Installer 错误码 %1。").arg(opened); return package; }
    package.name = packageProperty(database.handle, L"ProductName");
    package.version = packageProperty(database.handle, L"ProductVersion");
    package.productCode = packageProperty(database.handle, L"ProductCode");
    const auto manufacturer = packageProperty(database.handle, L"Manufacturer");
    if (!isZeroTierProduct(package.name, manufacturer) || !isProductCode(package.productCode) || package.version.isEmpty()) {
        package.error = S("[MSI_PRODUCT] 安装包产品信息不是受支持的 ZeroTier One 官方 MSI。"); return package;
    }
    package.valid = true; return package;
}
#endif
class NativeInstallerPlatform final : public InstallerPlatform {
public:
    InstallerPackage inspect(const QString& path) override {
#ifdef Q_OS_WIN
        return inspectLockedPackage(path, std::make_shared<FileLock>());
#else
        Q_UNUSED(path);
        InstallerPackage package; package.error = S("[PLATFORM_UNSUPPORTED] 服务安装/卸载仅支持 Windows。"); return package;
#endif
    }
    ProductInventory inventory() override {
        ProductInventory inventory;
#ifdef Q_OS_WIN
        inventory.supported = true;
        for (DWORD index = 0; index < 4096; ++index) {
            wchar_t code[39]{}, sid[256]{}; MSIINSTALLCONTEXT context{}; DWORD sidLength = 256;
            const auto result = MsiEnumProductsExW(nullptr, nullptr, MSIINSTALLCONTEXT_MACHINE, index, code, &context, sid, &sidLength);
            if (result == ERROR_NO_MORE_ITEMS) return inventory;
            if (result != ERROR_SUCCESS) { inventory.error = S("[MSI_ENUM] 无法枚举计算机级安装产品，Windows Installer 错误码 %1。").arg(result); return inventory; }
            const auto name = productProperty(code, L"InstalledProductName");
            const auto publisher = productProperty(code, L"Publisher");
            if (isZeroTierProduct(name, publisher)) inventory.products.append({QString::fromWCharArray(code), name, productProperty(code, L"VersionString")});
        }
        inventory.error = S("[MSI_ENUM_LIMIT] 产品数量超过枚举限制，请使用 Windows 已安装的应用管理。");
#else
        inventory.error = S("服务安装/卸载仅支持 Windows；当前平台保留状态检测与房间功能。");
#endif
        return inventory;
    }
    void execute(InstallerAction action, const QString& input, const QString& productCode,
                 quintptr ownerWindow, QObject* lifetime, InstallerDone done) override {
#ifdef Q_OS_WIN
        const auto lock = std::make_shared<FileLock>();
        QString arguments;
        if (action == InstallerAction::Install) {
            const auto package = inspectLockedPackage(input, lock);
            if (!package.valid) { done({InstallerStatus::Failed, 0, package.error}); return; }
            arguments = S("/i \"%1\" /norestart REBOOT=ReallySuppress").arg(QDir::toNativeSeparators(package.path));
        } else {
            // Never execute a registry UninstallString. Only a current, machine-level
            // ZeroTier product GUID can be passed to the system Windows Installer.
            const auto current = inventory(); bool found = false;
            for (const auto& product : current.products) if (product.code.compare(productCode, Qt::CaseInsensitive) == 0) found = true;
            if (!current.error.isEmpty() || !isProductCode(productCode) || !found) {
                done({InstallerStatus::Failed, 1605, current.error.isEmpty() ? S("[MSI_NOT_INSTALLED] 未找到对应的计算机级 ZeroTier 安装；请刷新检测。") : current.error}); return;
            }
            arguments = S("/x %1 /norestart REBOOT=ReallySuppress").arg(productCode);
        }
        wchar_t systemDirectory[MAX_PATH]{};
        const auto length = GetSystemDirectoryW(systemDirectory, MAX_PATH);
        if (length == 0 || length >= MAX_PATH) { done({InstallerStatus::Failed, ERROR_PATH_NOT_FOUND, S("[MSI_SYSTEM_DIRECTORY] 无法定位 Windows 系统安装器。")}); return; }
        const auto executable = QDir::toNativeSeparators(QString::fromWCharArray(systemDirectory) + S("/msiexec.exe")).toStdWString();
        const auto parameters = arguments.toStdWString();
        SHELLEXECUTEINFOW execute{}; execute.cbSize = sizeof(execute);
        execute.fMask = SEE_MASK_NOCLOSEPROCESS | SEE_MASK_NOASYNC;
        execute.hwnd = reinterpret_cast<HWND>(ownerWindow); execute.lpVerb = L"runas";
        execute.lpFile = executable.c_str(); execute.lpParameters = parameters.c_str(); execute.nShow = SW_SHOWNORMAL;
        if (!ShellExecuteExW(&execute)) { done(windowsInstallerResult(GetLastError())); return; }
        if (!execute.hProcess) { done({InstallerStatus::Failed, 0, S("[MSI_PROCESS] 安装进程未返回可跟踪的句柄；请检查 Windows 安装器。")}); return; }
        const auto process = std::shared_ptr<void>(execute.hProcess, [](void* handle) { CloseHandle(handle); });
        auto* timer = new QTimer(lifetime);
        QObject::connect(timer, &QTimer::timeout, timer, [timer, process, lock, done = std::move(done)] {
            const auto wait = WaitForSingleObject(process.get(), 0);
            if (wait == WAIT_TIMEOUT) return;
            timer->stop(); DWORD code = ERROR_GEN_FAILURE;
            if (wait != WAIT_OBJECT_0 || !GetExitCodeProcess(process.get(), &code)) { code = GetLastError(); if (code == 0) code = ERROR_GEN_FAILURE; }
            timer->deleteLater(); done(windowsInstallerResult(code));
        });
        timer->start(300);
#else
        Q_UNUSED(action); Q_UNUSED(input); Q_UNUSED(productCode); Q_UNUSED(ownerWindow); Q_UNUSED(lifetime);
        done({InstallerStatus::Failed, 0, S("[PLATFORM_UNSUPPORTED] 服务安装/卸载仅支持 Windows。")});
#endif
    }
};
}
bool isZeroTierProduct(const QString& name, const QString& manufacturer) {
    const auto product = name.trimmed();
    return (product.compare(S("ZeroTier One"), Qt::CaseInsensitive) == 0 || product.compare(S("ZeroTier"), Qt::CaseInsensitive) == 0) &&
        normalizedPublisher(manufacturer) == S("zerotierinc");
}
bool isProductCode(const QString& code) {
    return QRegularExpression(S("^\\{[0-9a-f]{8}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{12}\\}$"), QRegularExpression::CaseInsensitiveOption).match(code).hasMatch();
}
InstallerResult windowsInstallerResult(quint32 code) {
    if (code == 0) return {InstallerStatus::Success, code, S("Windows Installer 已完成。请以刷新后的安装、服务与 API 检测结果为准。")};
    if (code == 3010 || code == 1641) return {InstallerStatus::RebootRequired, code, S("操作已完成，Windows Installer 要求重启电脑后生效。GameLink 不会主动重启电脑。")};
    if (code == 1223 || code == 1602) return {InstallerStatus::Cancelled, code, S("用户取消了管理员授权或安装向导，操作未完成。")};
    QString reason;
    switch (code) {
    case 5: reason = S("权限不足，请允许 Windows 管理员授权。"); break;
    case 1603: reason = S("安装/卸载失败；请查看 Windows 安装向导与系统事件日志。"); break;
    case 1605: reason = S("该版本未安装，可能已经卸载；请刷新检测。"); break;
    case 1618: reason = S("另一个安装任务正在运行，请完成后重试。"); break;
    case 1619: case 1620: reason = S("无法打开安装包或安装包无效，请重新选择官方完整 MSI。"); break;
    case 1625: reason = S("系统策略阻止了安装，请联系电脑管理员。"); break;
    case 1638: reason = S("已安装其他版本，请使用匹配的升级包或先卸载现有版本。"); break;
    default: reason = S("请检查 Windows Installer 和系统事件日志。"); break;
    }
    return {InstallerStatus::Failed, code, S("[MSI_ERROR_%1] %2").arg(code).arg(reason)};
}
std::shared_ptr<InstallerPlatform> nativeInstallerPlatform() { return std::make_shared<NativeInstallerPlatform>(); }
ZeroTierInstaller::ZeroTierInstaller(std::shared_ptr<InstallerPlatform> platform, QObject* parent)
    : QObject(parent), platform_(platform ? std::move(platform) : nativeInstallerPlatform()) {}
InstallerPackage ZeroTierInstaller::inspect(const QString& path) const { return platform_->inspect(path); }
ProductInventory ZeroTierInstaller::inventory() const { return platform_->inventory(); }
void ZeroTierInstaller::install(const QString& path, quintptr ownerWindow) { start(InstallerAction::Install, path, {}, ownerWindow); }
void ZeroTierInstaller::uninstall(const QString& code, quintptr ownerWindow) { start(InstallerAction::Uninstall, {}, code, ownerWindow); }
void ZeroTierInstaller::start(InstallerAction action, const QString& path, const QString& code, quintptr ownerWindow) {
    if (busy_) return;
    if (action == InstallerAction::Install) {
        const auto package = inspect(path);
        if (!package.valid) { emit completed({InstallerStatus::Failed, 0, package.error}); return; }
    } else {
        const auto current = inventory(); bool found = false;
        for (const auto& product : current.products) if (product.code.compare(code, Qt::CaseInsensitive) == 0) found = true;
        if (!current.supported || !current.error.isEmpty() || !isProductCode(code) || !found) {
            emit completed({InstallerStatus::Failed, 1605, current.error.isEmpty() ? S("[MSI_PRODUCT_CODE] 未选择有效的已安装 ZeroTier 产品。") : current.error}); return;
        }
    }
    busy_ = true; emit busyChanged(true);
    const QPointer<ZeroTierInstaller> guard(this);
    platform_->execute(action, path, code, ownerWindow, this, [guard](InstallerResult result) {
        if (!guard || !guard->busy_) return;
        guard->busy_ = false; emit guard->busyChanged(false); emit guard->completed(result);
    });
}
}
