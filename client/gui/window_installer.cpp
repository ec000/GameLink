#include "window.hpp"
#include <QCloseEvent>
#include <QCoreApplication>
#include <QFileInfo>
#include <QComboBox>
#include <QFileDialog>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#define S(x) QStringLiteral(x)
void Window::updateInstallerButtons() {
 const bool enabled=installerInventory.supported && installerInventory.error.isEmpty() && !installer.busy() && !networkMutationBusy && !credentialBusy;
 chooseInstaller->setEnabled(enabled);refreshProducts->setEnabled(!installer.busy() && !networkMutationBusy && !credentialBusy);
 installZeroTier->setEnabled(enabled && !installerPath->text().isEmpty());
 uninstallZeroTier->setEnabled(enabled && installedProducts->currentIndex()>=0);
 installedProducts->setEnabled(enabled && installedProducts->count()>0);
}
void Window::refreshInstaller() {
 if(installer.busy() || networkMutationBusy || credentialBusy)return;
 const auto selected=installedProducts->currentData().toString();
 installerInventory=installer.inventory();installedProducts->clear();
 for(const auto& product:installerInventory.products)installedProducts->addItem(product.name+S(" · ")+product.version,product.code);
 const auto index=installedProducts->findData(selected);if(index>=0)installedProducts->setCurrentIndex(index);
 if(!installerInventory.error.isEmpty())installerStatus->setText(installerInventory.error);
 else if(installerInventory.products.isEmpty())installerStatus->setText(S("未发现计算机级 ZeroTier MSI 安装。可选择官方 MSI 安装；便携版/仅服务注册不通过此处卸载。"));
 else installerStatus->setText(S("已发现 %1 个 ZeroTier 安装。安装/修复或卸载会影响本机所有使用 ZeroTier 的连接。").arg(installerInventory.products.size()));
 if(installerInventory.supported && installerPath->text().isEmpty()) {
  const auto bundled=QCoreApplication::applicationDirPath()+S("/prerequisites/ZeroTier-One.msi");
  if(QFileInfo::exists(bundled)) {
   const auto package=installer.inspect(bundled);
   if(package.valid) {
    installerPath->setText(package.path);
    installerStatus->setText(S("已验证本地缓存的 ZeroTier %1；可直接安装/修复，Windows 将请求管理员授权。").arg(package.version));
   } else installerStatus->setText(package.error);
  }
 }
 updateInstallerButtons();
}
void Window::selectInstaller() {
 const auto path=QFileDialog::getOpenFileName(this,S("选择官方 ZeroTier One 安装包"),{},S("Windows Installer (*.msi)"));
 if(path.isEmpty())return;
 const auto package=installer.inspect(path);
 if(!package.valid){installerPath->clear();installerStatus->setText(package.error);log(package.error);updateInstallerButtons();return;}
 installerPath->setText(package.path);
 installerStatus->setText(S("已验证 %1 %2 · 发布者 %3。点击安装后，Windows 将请求管理员授权。").arg(package.name,package.version,package.publisher));
 updateInstallerButtons();
}
void Window::installService() {
 if(installer.busy() || networkMutationBusy || credentialBusy)return;
 const auto package=installer.inspect(installerPath->text());
 if(!package.valid){installerStatus->setText(package.error);log(package.error);return;}
 const auto details=S("安装/修复 %1 %2？\n\n文件：%3\n签名发布者：%4\n\n将运行官方 MSI 安装向导，安装或升级 ZeroTier 服务与虚拟网卡驱动；已有连接可能中断。Windows 将单独请求管理员授权。\n不会导入或替换 planet/moon 文件。").arg(package.name,package.version,package.path,package.publisher);
 if(QMessageBox::question(this,S("安装 ZeroTier 服务"),details,QMessageBox::Yes|QMessageBox::No,QMessageBox::No)!=QMessageBox::Yes)return;
 installer.install(package.path,static_cast<quintptr>(winId()));
}
void Window::uninstallService() {
 if(installer.busy() || networkMutationBusy || credentialBusy)return;
 const auto code=installedProducts->currentData().toString();
 const auto current=installer.inventory();
 if(!current.error.isEmpty()){installerStatus->setText(current.error);return;}
 for(const auto& product:current.products){
  if(product.code!=code)continue;
  const auto details=S("卸载 %1 %2？\n\n产品：%3\n这将卸载整个官方 ZeroTier 产品（含服务与虚拟网卡），中断本机所有 ZeroTier 网络及其他软件的相关连接。\n\n安装器可能调整或移除自身数据，请先确认需要保留的配置已备份。GameLink 不会主动删除 identity、网络、planet 或 moon 文件，但不能保证官方安装器保留它们。\nWindows 将单独请求管理员授权。").arg(product.name,product.version,product.code);
  if(QMessageBox::question(this,S("卸载 ZeroTier 服务"),details,QMessageBox::Yes|QMessageBox::No,QMessageBox::No)!=QMessageBox::Yes)return;
  installer.uninstall(product.code,static_cast<quintptr>(winId()));return;
 }
 refreshInstaller();installerStatus->setText(S("所选 ZeroTier 版本已不在已安装列表中，请刷新后重试。"));
}
void Window::closeEvent(QCloseEvent* event) {
 if(installer.busy()){
  event->ignore();QMessageBox::information(this,S("安装任务仍在运行"),S("请先在 Windows Installer 中完成或取消当前操作，再关闭 GameLink。"));return;
 }
 QMainWindow::closeEvent(event);
}
