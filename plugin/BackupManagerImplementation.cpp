#include "BackupManagerImplementation.h"

#include "UtilsJsonRpc.h"

#define DEFAULT_BACKUP_PATH "/opt/secure/persistent/settings_backup/"
#define DEFAULT_BACKUP_VARIANT "generic"

namespace WPEFramework {
namespace Plugin {

    SERVICE_REGISTRATION(BackupManagerImplementation, 1, 0);
    BackupManagerImplementation* BackupManagerImplementation::_instance = nullptr;
    
    BackupManagerImplementation::BackupManagerImplementation()
    : _adminLock()
    , _service(nullptr)
    , _monitor(this)
    {
         LOGINFO("Create BackupManagerImplementation Instance");
         BackupManagerImplementation::_instance = this;
    }

    BackupManagerImplementation::~BackupManagerImplementation()
    {
        LOGINFO("Call BackupManagerImplementation destructor\n");

        if (_service != nullptr) {
            _service->Unregister(&_monitor);
            _service->Release();
            _service = nullptr;
        }

        BackupManagerImplementation::_instance = nullptr;
    }

    uint32_t BackupManagerImplementation::Configure(PluginHost::IShell* service)
    {
        LOGINFO("Configuring BackupManagerImplementation");
        uint32_t status = Core::ERROR_NONE;
        ASSERT(service != nullptr);
        _service = service;
        _service->AddRef();

        _service->Register(&_monitor);

        return status;
    }

    Core::hresult BackupManagerImplementation::BackupSettings(const Exchange::BackupContext& context)
    {
        LOGINFO("BackupSettings scenario [%d] with persistentPath [%s] and variant [%s]", context.scenario, context.persistentPath.c_str(), context.variant.c_str());

        Exchange::BackupContext providerContext = context;
        if (providerContext.persistentPath.empty())
        {
            providerContext.persistentPath = DEFAULT_BACKUP_PATH;
            LOGINFO("Using default backup path [%s]", providerContext.persistentPath.c_str());
        }
        if (providerContext.variant.empty())
        {
            providerContext.variant = DEFAULT_BACKUP_VARIANT;
            LOGINFO("Using default backup variant [%s]", providerContext.variant.c_str());
        } 

        if (access(providerContext.persistentPath.c_str(), W_OK) != 0)
        {
            if (mkdir(providerContext.persistentPath.c_str(), 0755) != 0 || access(providerContext.persistentPath.c_str(), W_OK) != 0)
            {
                LOGERR("Failed to create directory [%s] for backup data", providerContext.persistentPath.c_str());
                return Core::ERROR_GENERAL;
            }
        }

        BackupProviderContainer::const_iterator it = _backupProviders.cbegin();
        while (it != _backupProviders.cend())
        {
            it->second->Backup(providerContext);
            ++it;
        }
        return Core::ERROR_NONE;
    }

    Core::hresult BackupManagerImplementation::RestoreSettings(const Exchange::BackupContext& context)
    {
        LOGINFO("RestoreSettings scenario [%d] with persistentPath [%s] and variant [%s]", context.scenario, context.persistentPath.c_str(), context.variant.c_str());
 
        Exchange::BackupContext providerContext = context;
        if (providerContext.persistentPath.empty())
        {
            providerContext.persistentPath = DEFAULT_BACKUP_PATH;
            LOGINFO("Using default backup path [%s]", providerContext.persistentPath.c_str());
        }
        if (providerContext.variant.empty())
        {
            providerContext.variant = DEFAULT_BACKUP_VARIANT;
            LOGINFO("Using default backup variant [%s]", providerContext.variant.c_str());
        }

        BackupProviderContainer::const_iterator it = _backupProviders.cbegin();
        while (it != _backupProviders.cend())
        {            
            it->second->Restore(providerContext);
            ++it;
        }

        return Core::ERROR_NONE;
    }

    Core::hresult BackupManagerImplementation::DeleteBackup(const Exchange::BackupContext& context)
    {
        LOGINFO("DeleteBackup scenario [%d] with persistentPath [%s] and variant [%s]", context.scenario, context.persistentPath.c_str(), context.variant.c_str());

        Exchange::BackupContext providerContext = context;
        if (providerContext.persistentPath.empty())
        {
            providerContext.persistentPath = DEFAULT_BACKUP_PATH;
            LOGINFO("Using default backup path [%s]", providerContext.persistentPath.c_str());
        }
        if (providerContext.variant.empty())
        {
            providerContext.variant = DEFAULT_BACKUP_VARIANT;
            LOGINFO("Using default backup variant [%s]", providerContext.variant.c_str());
        }

        BackupProviderContainer::const_iterator it = _backupProviders.cbegin();
        while (it != _backupProviders.cend())
        {            
            it->second->Delete(providerContext);
            ++it;
        }

        return Core::ERROR_NONE;
    }

    void BackupManagerImplementation::PluginActivated(const string& callsign, PluginHost::IShell* service)
    {
        BackupProviderContainer::iterator it = _backupProviders.find(callsign);
        if (it == _backupProviders.end())
        {
            // add to the list of backup providers
            Exchange::IBackupProvider *provider = service->QueryInterfaceByCallsign<Exchange::IBackupProvider>(callsign.c_str());
            if (provider != nullptr)
            {
                _backupProviders[callsign] = provider;
                LOGINFO("Backup provider added for callsign: %s", callsign.c_str());
            }
        }
    }

    void BackupManagerImplementation::PluginDeactivated(const string& callsign, PluginHost::IShell* service)
    {
        BackupProviderContainer::iterator it = _backupProviders.find(callsign);
        if (it != _backupProviders.end())
        {
            // remove from the list of backup providers
            it->second->Release();
            _backupProviders.erase(it);
            LOGINFO("Backup provider removed for callsign: %s", callsign.c_str());
        }
    }


} // namespace Plugin
} // namespace WPEFramework
