#pragma once

#include "Module.h"

#include <interfaces/IBackup.h>
#include <interfaces/json/JBackupManager.h>
#include <interfaces/json/JsonData_BackupManager.h>

#include <interfaces/IConfiguration.h>

#include <com/com.h>
#include <core/core.h>

#include "UtilsLogging.h"
#include "tracing/Logging.h"

namespace WPEFramework {
namespace Plugin {
    
class BackupManager : public PluginHost::IPlugin, public PluginHost::JSONRPC {
    private:
        class Notification : public RPC::IRemoteConnection::INotification
        {
        private:
            Notification() = delete;
            Notification(const Notification &) = delete;
            Notification &operator=(const Notification &) = delete;

        public:
            explicit Notification(BackupManager *parent)
                : _parent(*parent)
            {
                ASSERT(parent != nullptr);
            }

            virtual ~Notification()
            {
            }

            BEGIN_INTERFACE_MAP(Notification)
            INTERFACE_ENTRY(RPC::IRemoteConnection::INotification)
            END_INTERFACE_MAP

            void Activated(RPC::IRemoteConnection *) override
            {
                LOGINFO("Backup Notification Activated");
            }

            void Deactivated(RPC::IRemoteConnection *connection) override
            {
                LOGINFO("Backup Notification Deactivated");
                _parent.Deactivated(connection);
            }

        private:
            BackupManager &_parent;
        };

    public:
        BackupManager(const BackupManager&) = delete;
        BackupManager& operator=(const BackupManager&) = delete;

        BackupManager();
        ~BackupManager() override;

        BEGIN_INTERFACE_MAP(BackupManager)
            INTERFACE_ENTRY(PluginHost::IPlugin)
            INTERFACE_ENTRY(PluginHost::IDispatcher)
            INTERFACE_AGGREGATE(Exchange::IBackupManager, _backup)
        END_INTERFACE_MAP

        //  IPlugin methods
        // -------------------------------------------------------------------------------------------------------
        const string Initialize(PluginHost::IShell *service) override;
        void Deinitialize(PluginHost::IShell *service) override;
        string Information() const override;

    private:

        void Deactivated(RPC::IRemoteConnection* connection);
    
        PluginHost::IShell* _service;
        uint32_t _connectionId;
        Exchange::IBackupManager* _backup;
        Exchange::IConfiguration* _configure;
        Core::Sink<Notification> _backupNotification;
};

} // namespace Plugin
} // namespace WPEFramework
