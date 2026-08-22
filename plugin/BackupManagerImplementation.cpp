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
#include <string.h>

#define DEFAULT_BACKUP_PATH "/opt/secure/persistent/settings_backup/"
#define DEFAULT_BACKUP_VARIANT "generic"

namespace WPEFramework {
namespace Plugin {

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
        if (!contextIn.persistentPath.empty()) {
            string validatedPath = ValidateAndCanonicalizePath(contextIn.persistentPath);
            if (validatedPath.empty()) {
                // Validation failed, keep the path empty to signal error
                contextOut.persistentPath.clear();
            } else {
                contextOut.persistentPath = validatedPath;
            }
        } else {
            contextOut.persistentPath = DEFAULT_BACKUP_PATH;
        }
        contextOut.variant = !contextIn.variant.empty() ? contextIn.variant : DEFAULT_BACKUP_VARIANT;
    }

    string BackupManagerImplementation::ValidateAndCanonicalizePath(const string& inputPath) const
    {
        char resolvedPath[PATH_MAX];
        char* result = nullptr;

        // First, try to resolve the path directly (for existing paths)
        result = realpath(inputPath.c_str(), resolvedPath);

        if (result != nullptr) {
            // Path exists and was successfully resolved
            string canonicalPath(resolvedPath);

            // Verify the canonical path is within the allowed base directory
            if (canonicalPath.find(DEFAULT_BACKUP_PATH) == 0) {
                return canonicalPath;
            } else {
                LOGERR("Path validation failed: [%s] is outside the allowed base directory [%s]", 
                       canonicalPath.c_str(), DEFAULT_BACKUP_PATH);
                return ""; // Return empty string to indicate failure
            }
        }

        // Path doesn't exist, so we need to validate it by checking the parent directory
        // Extract the parent directory and filename
        string parentDir;
        string fileName;

        size_t lastSlash = inputPath.find_last_of('/');
        if (lastSlash == string::npos) {
            // No directory component, use current directory as parent
            parentDir = ".";
            fileName = inputPath;
        } else if (lastSlash == 0) {
            // Path starts with /
            parentDir = "/";
            fileName = inputPath.substr(1);
        } else {
            parentDir = inputPath.substr(0, lastSlash);
            fileName = inputPath.substr(lastSlash + 1);
        }

        // Resolve the parent directory
        char resolvedParent[PATH_MAX];
        result = realpath(parentDir.c_str(), resolvedParent);

        if (result == nullptr) {
            LOGERR("Path validation failed: parent directory [%s] does not exist or is inaccessible", 
                   parentDir.c_str());
            return ""; // Return empty string to indicate failure
        }

        string canonicalParent(resolvedParent);

        // Verify the parent directory is within the allowed base directory
        if (canonicalParent.find(DEFAULT_BACKUP_PATH) != 0) {
            LOGERR("Path validation failed: parent directory [%s] is outside the allowed base directory [%s]", 
                   canonicalParent.c_str(), DEFAULT_BACKUP_PATH);
            return ""; // Return empty string to indicate failure
        }

        // Construct the full canonical path
        string fullPath = canonicalParent;
        if (!fullPath.empty() && fullPath.back() != '/') {
            fullPath += '/';
        }
        fullPath += fileName;

        return fullPath;
    }

    Core::hresult BackupManagerImplementation::BackupSettings(const Exchange::BackupContext& context)
    {
        LOGINFO("BackupSettings scenario [%d] with persistentPath [%s] and variant [%s]", context.scenario, context.persistentPath.c_str(), context.variant.c_str());

        Exchange::BackupContext providerContext = context;
        MakeContext(context, providerContext);

        // Check if path validation failed
        if (!context.persistentPath.empty() && providerContext.persistentPath.empty()) {
            LOGERR("Path validation failed for [%s]", context.persistentPath.c_str());
            return Core::ERROR_INVALID_PARAMETER;
        }

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

        // Check if path validation failed
        if (!context.persistentPath.empty() && providerContext.persistentPath.empty()) {
            LOGERR("Path validation failed for [%s]", context.persistentPath.c_str());
            return Core::ERROR_INVALID_PARAMETER;
        }

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

        // Check if path validation failed
        if (!context.persistentPath.empty() && providerContext.persistentPath.empty()) {
            LOGERR("Path validation failed for [%s]", context.persistentPath.c_str());
            return Core::ERROR_INVALID_PARAMETER;
        }

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
