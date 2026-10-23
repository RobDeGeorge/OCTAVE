// WebAssembly build: NetworkManager without processes. A browser tab has no
// Wi-Fi name to read (nmcli / netsh), no git checkout to self-update and no
// device to power off, and Qt's wasm QProcess is an empty stub. The page is
// online by definition (it was just downloaded), so isConnected is true and
// every update / restart action is a no-op. Replaces
// src/managers/networkmanager.cpp when OCTAVE_WASM.

#include "../../managers/networkmanager.h"

NetworkManager::NetworkManager(QObject *parent)
    : QObject(parent)
    , m_isConnected(true)
    , m_updateStatus(QStringLiteral("idle"))
    , m_selfUpdateStatus(QStringLiteral("idle"))
{
}

NetworkManager::~NetworkManager() = default;

QString NetworkManager::networkName() const { return m_networkName; }
bool NetworkManager::isConnected() const { return m_isConnected; }
QString NetworkManager::updateStatus() const { return m_updateStatus; }
QString NetworkManager::updateMessage() const { return m_updateMessage; }
QString NetworkManager::remoteCommit() const { return m_remoteCommit; }
QString NetworkManager::remoteCommitMsg() const { return m_remoteCommitMsg; }
bool NetworkManager::workingTreeDirty() const { return false; }
QString NetworkManager::selfUpdateStatus() const { return m_selfUpdateStatus; }
QString NetworkManager::selfUpdateMessage() const { return m_selfUpdateMessage; }
bool NetworkManager::canSelfUpdate() const { return false; }

void NetworkManager::refreshNetwork() {}
void NetworkManager::checkForUpdates() {}
void NetworkManager::applySelfUpdate() {}
void NetworkManager::restartApp() {}
void NetworkManager::shutdown_device() {}
void NetworkManager::cleanup() {}

void NetworkManager::onConnectivityCheckFinished() {}
void NetworkManager::onNetworkNameCheckFinished() {}
void NetworkManager::onUpdateCheckReplyFinished() {}
void NetworkManager::clearNetworkName() {}
