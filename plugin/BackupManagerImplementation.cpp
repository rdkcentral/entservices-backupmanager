/*
* If not stated otherwise in this file or this component's LICENSE file the
* following copyright and licenses apply:
*
* Copyright 2026 RDK Management
*
* Licensed under the Apache License, Version 2.0 (the "License");
* you may not use this file except in compliance with the License.
* You may obtain a copy of the License at
*
* http://www.apache.org/licenses/LICENSE-2.0
*
* Unless required by applicable law or agreed to in writing, software
* distributed under the License is distributed on an "AS IS" BASIS,
* WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
* See the License for the specific language governing permissions and
* limitations under the License.
*/

#include "BackupManagerImplementation.h"

#include <unistd.h>
#include <sys/stat.h>
#include <limits.h>
#include <stdlib.h>
#include <cstring>

#define DEFAULT_BACKUP_PATH "/opt/secure/persistent/settings_backup/"
#define DEFAULT_BACKUP_VARIANT "generic"

namespace WPEFramework {
namespace Plugin {

    // Validate backup path to prevent path traversal (RDKEMW-24515)
    bool isValidBackupPath(const std::string& path)
    {
        if (path.empty())
        {
            return false;
        }

        // Reject path traversal sequences
        if (path.find("..") != std::string::npos)
        {
            return false;
        }

        // Canonicalize the path to resolve symlinks
        char resolvedPath[PATH_MAX];
        if (realpath(path.c_str(), resolvedPath) == nullptr)
        {
            // Path doesn't exist - this is acceptable for paths that will be created
            // but we should still validate the format
            return true;
        }

        // Check if the resolved path is still within safe bounds
        std::string resolved(resolvedPath);
        if (resolved.find("..") != std::string::npos)
        {
            return false;
        }

        // Allow-listed safe prefixes for backup paths
        const std::vector<std::string> safePrefixes = {
            "/opt/",
            "/tmp/",
            "/var/tmp/"
        };

        // If the path is absolute, check it's within safe prefixes
        if (path[0] == '/')
        {
            bool isSafePrefix = false;
            for (const auto& prefix : safePrefixes)
            {
                if (path.compare(0, prefix.length(), prefix) == 0)
                {
                    isSafePrefix = true;
                    break;
                }
            }
            if (!isSafePrefix)
            {
                return false;
            }
        }

        return true;
    }

    SERVICE_REGISTRATION(BackupManagerImplementation, 1, 0);
    
    BackupManagerImplementation::BackupManagerImplementation()
    : _adminLock()
    , _service(nullptr)
    , _monitor(this)
    {
         LOGINFO("Create BackupManagerImplementation Instance");
    }

    BackupManagerImplementation::~BackupManagerImplementation()
    {
        LOGINFO("Call BackupManagerImplementation destructor\n");

        if (_service != nullptr) {
            _service->Unregister(&_monitor);
            _service->Release();
            _service = nullptr;
        }

        Core::SafeSyncType<Core::CriticalSection> lock(_adminLock);

        BackupProviderContainer::const_iterator it = _backupProviders.cbegin();
        while (it != _backupProviders.cend())
        {
            if ( it->second != nullptr)
            {
                it->second->Release();
            }
            ++it;
        }

         _backupProviders.clear();
        
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

    void BackupManagerImplementation::MakeContext(const Exchange::BackupContext &contextIn, Exchange::BackupContext &contextOut) const
    {
        contextOut.scenario = contextIn.scenario;
        
        // Validate persistentPath to prevent path traversal (RDKEMW-24515)
        if (!contextIn.persistentPath.empty() && !isValidBackupPath(contextIn.persistentPath))
        {
            LOGERR("Invalid persistentPath (traversal or unsafe): %s", contextIn.persistentPath.c_str());
            contextOut.persistentPath = DEFAULT_BACKUP_PATH;
        }
        else
        {
            contextOut.persistentPath = !contextIn.persistentPath.empty() ? contextIn.persistentPath : DEFAULT_BACKUP_PATH;
        }
        
        contextOut.variant = !contextIn.variant.empty() ? contextIn.variant : DEFAULT_BACKUP_VARIANT;
    }

    Core::hresult BackupManagerImplementation::BackupSettings(const Exchange::BackupContext& context)
    {
        LOGINFO("BackupSettings scenario [%d] with persistentPath [%s] and variant [%s]", context.scenario, context.persistentPath.c_str(), context.variant.c_str());

        Exchange::BackupContext providerContext = context;
        MakeContext(context, providerContext);

        if (providerContext.persistentPath == DEFAULT_BACKUP_PATH 
            && access(providerContext.persistentPath.c_str(), W_OK) != 0 
            && mkdir(providerContext.persistentPath.c_str(), 0700) != 0)
        {
            LOGERR("Failed to create directory [%s] for backup data", providerContext.persistentPath.c_str());
            return Core::ERROR_GENERAL;
        }

        if (access(providerContext.persistentPath.c_str(), W_OK) != 0)
        {
            LOGERR("Backup path [%s] is not accessible for writing", providerContext.persistentPath.c_str());
            return Core::ERROR_INVALID_PARAMETER;
        }

        Core::SafeSyncType<Core::CriticalSection> lock(_adminLock);

        uint32_t status = Core::ERROR_NONE;
        BackupProviderContainer::const_iterator it = _backupProviders.cbegin();
        while (it != _backupProviders.cend())
        {
            if (it->second->Backup(providerContext) != Core::ERROR_NONE)
            {
                LOGERR("Backup failed for provider with callsign [%s]", it->first.c_str());
                status = Core::ERROR_GENERAL;
            }

            ++it;
        }
        return status;
    }

    Core::hresult BackupManagerImplementation::RestoreSettings(const Exchange::BackupContext& context)
    {
        LOGINFO("RestoreSettings scenario [%d] with persistentPath [%s] and variant [%s]", context.scenario, context.persistentPath.c_str(), context.variant.c_str());
 
        Exchange::BackupContext providerContext = context;
        MakeContext(context, providerContext);

        Core::SafeSyncType<Core::CriticalSection> lock(_adminLock);

        uint32_t status = Core::ERROR_NONE;
        BackupProviderContainer::const_iterator it = _backupProviders.cbegin();
        while (it != _backupProviders.cend())
        {            
            if (it->second->Restore(providerContext) != Core::ERROR_NONE)
            {
                LOGERR("Restore failed for provider with callsign [%s]", it->first.c_str());
                status = Core::ERROR_GENERAL;
            }   
            ++it;
        }

        return status;
    }

    Core::hresult BackupManagerImplementation::DeleteBackup(const Exchange::BackupContext& context)
    {
        LOGINFO("DeleteBackup scenario [%d] with persistentPath [%s] and variant [%s]", context.scenario, context.persistentPath.c_str(), context.variant.c_str());

        Exchange::BackupContext providerContext = context;
        MakeContext(context, providerContext);

        Core::SafeSyncType<Core::CriticalSection> lock(_adminLock);

        uint32_t status = Core::ERROR_NONE;
        BackupProviderContainer::const_iterator it = _backupProviders.cbegin();
        while (it != _backupProviders.cend())
        {            
            if (it->second->Delete(providerContext) != Core::ERROR_NONE)
            {
                LOGERR("Delete failed for provider with callsign [%s]", it->first.c_str());
                status = Core::ERROR_GENERAL;
                break;
            }
            ++it;
        }

        return status;
    }

    void BackupManagerImplementation::PluginActivated(const string& callsign, PluginHost::IShell* service)
    {
        Core::SafeSyncType<Core::CriticalSection> lock(_adminLock);
        BackupProviderContainer::iterator it = _backupProviders.find(callsign);
        if (it == _backupProviders.end())
        {
            // add to the list of backup providers
            Exchange::IBackupProvider *provider = service->QueryInterface<Exchange::IBackupProvider>();
            if (provider != nullptr)
            {
                _backupProviders[callsign] = provider;
                LOGINFO("Backup provider added for callsign: %s", callsign.c_str());
            }
        }
    }

    void BackupManagerImplementation::PluginDeactivated(const string& callsign, PluginHost::IShell* service)
    {
        Core::SafeSyncType<Core::CriticalSection> lock(_adminLock);
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
