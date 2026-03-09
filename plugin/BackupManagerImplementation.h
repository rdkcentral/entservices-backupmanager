
#pragma once

#include "Module.h"

#include <interfaces/IBackup.h>
#include <interfaces/IConfiguration.h>

#include <com/com.h>
#include <core/core.h>

#include "UtilsLogging.h"

using namespace WPEFramework;

namespace WPEFramework {
namespace Plugin {
    class BackupManagerImplementation : public Exchange::IBackupManager, public Exchange::IConfiguration
    {
        private:

        class MonitorObjects : public PluginHost::IPlugin::INotification
        {
        public:
            explicit MonitorObjects(BackupManagerImplementation *parent)
                : _parent(*parent)
            {
                ASSERT(parent != nullptr);
            }

         void Activated (const string& callsign, PluginHost::IShell* service) override
            {
                LOGINFO("Plugin Activated: %s", callsign.c_str());
                _parent.PluginActivated(callsign, service);
            }

            void Deactivated (const string& callsign, PluginHost::IShell* service) override
            {
                LOGINFO("Plugin Deactivated: %s", callsign.c_str());
                _parent.PluginDeactivated(callsign, service);
            }

            void Unavailable(const string&, PluginHost::IShell*) override
            {
            }

            BEGIN_INTERFACE_MAP(MonitorObjects)
            INTERFACE_ENTRY(PluginHost::IPlugin::INotification)
            END_INTERFACE_MAP
               
        private:
            BackupManagerImplementation& _parent;

        };

    public:
        // We do not allow this plugin to be copied !!
        BackupManagerImplementation();
        ~BackupManagerImplementation() override;

        static BackupManagerImplementation* instance(BackupManagerImplementation *backupImpl = nullptr);

        // We do not allow this plugin to be copied !!
        BackupManagerImplementation(const BackupManagerImplementation&) = delete;
        BackupManagerImplementation& operator=(const BackupManagerImplementation&) = delete;

        BEGIN_INTERFACE_MAP(BackupManagerImplementation)
        INTERFACE_ENTRY(Exchange::IBackupManager)
        INTERFACE_ENTRY(Exchange::IConfiguration)
        END_INTERFACE_MAP

    public:
        uint32_t Configure(PluginHost::IShell* service) override;

        Core::hresult BackupSettings(const Exchange::BackupContext& context) override;
        Core::hresult RestoreSettings(const Exchange::BackupContext& context) override;
        Core::hresult DeleteBackup(const Exchange::BackupContext& context) override;

        void PluginActivated(const string& callsign, PluginHost::IShell* service);
        void PluginDeactivated(const string& callsign, PluginHost::IShell* service);
        
    private:
        mutable Core::CriticalSection _adminLock;
        PluginHost::IShell* _service;
        Core::Sink<MonitorObjects> _monitor;
        using BackupProviderContainer = std::unordered_map<string, Exchange::IBackupProvider*>;
        BackupProviderContainer _backupProviders;
        
    public:
        static BackupManagerImplementation* _instance;
    };
} // namespace Plugin
} // namespace WPEFramework
