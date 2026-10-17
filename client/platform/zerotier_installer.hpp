#pragma once
#include <QObject>
#include <QList>
#include <QString>
#include <functional>
#include <memory>

namespace gamelink {
enum class InstallerAction { Install, Uninstall };
enum class InstallerStatus { Success, RebootRequired, Cancelled, Failed };
struct InstallerResult {
    InstallerStatus status = InstallerStatus::Failed;
    quint32 code = 0;
    QString message;
};
struct InstallerPackage {
    bool valid = false;
    QString path, productCode, name, version, publisher, error;
};
struct InstalledProduct { QString code, name, version; };
struct ProductInventory {
    bool supported = false;
    QList<InstalledProduct> products;
    QString error;
};
using InstallerDone = std::function<void(InstallerResult)>;
class InstallerPlatform {
public:
    virtual ~InstallerPlatform() = default;
    virtual InstallerPackage inspect(const QString& path) = 0;
    virtual ProductInventory inventory() = 0;
    virtual void execute(InstallerAction, const QString& package, const QString& productCode,
                         quintptr ownerWindow, QObject* lifetime, InstallerDone) = 0;
};
std::shared_ptr<InstallerPlatform> nativeInstallerPlatform();
bool isZeroTierProduct(const QString& name, const QString& manufacturer);
bool isProductCode(const QString& code);
InstallerResult windowsInstallerResult(quint32 code);

// Owns the operation state. Tests inject a platform without changing any real service.
class ZeroTierInstaller final : public QObject {
    Q_OBJECT
public:
    explicit ZeroTierInstaller(std::shared_ptr<InstallerPlatform> platform, QObject* parent = nullptr);
    InstallerPackage inspect(const QString& path) const;
    ProductInventory inventory() const;
    bool busy() const { return busy_; }
    void install(const QString& path, quintptr ownerWindow = 0);
    void uninstall(const QString& productCode, quintptr ownerWindow = 0);
signals:
    void busyChanged(bool busy);
    void completed(const gamelink::InstallerResult& result);
private:
    std::shared_ptr<InstallerPlatform> platform_;
    bool busy_ = false;
    void start(InstallerAction, const QString&, const QString&, quintptr);
};
}
