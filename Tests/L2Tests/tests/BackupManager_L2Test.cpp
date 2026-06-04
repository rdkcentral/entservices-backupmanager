#include "L2Tests.h"
#include "L2TestsMock.h"

#include <interfaces/IBackup.h>

#include <chrono>
#include <mutex>
#include <thread>

#include <gmock/gmock.h>
#include <gtest/gtest.h>

using namespace WPEFramework;

class BackupManager_L2Test : public L2TestMocks {
protected:
    Exchange::IBackupManager* m_BackupManagerPlugin = nullptr;
    PluginHost::IShell* m_controller_BackupManager = nullptr;
    Core::ProxyType<RPC::InvokeServerType<1, 0, 4>> BackupManager_Engine;
    Core::ProxyType<RPC::CommunicatorClient> BackupManager_Client;

    static constexpr const char* backupManagerCallsign = "org.rdk.BackupManager";
    static constexpr const char* persistentStoreCallsign = "org.rdk.PersistentStore";
    static constexpr const char* userSettingsCallsign = "org.rdk.UserSettings";

public:
    BackupManager_L2Test()
        : L2TestMocks()
    {
        uint32_t status = Core::ERROR_GENERAL;

        status = ActivateService(persistentStoreCallsign);
        EXPECT_EQ(Core::ERROR_NONE, status);

        status = ActivateService(userSettingsCallsign);
        EXPECT_EQ(Core::ERROR_NONE, status);

        status = ActivateService(backupManagerCallsign);
        EXPECT_EQ(Core::ERROR_NONE, status);

        status = CreateBackupManagerInterfaceObject();
        EXPECT_EQ(Core::ERROR_NONE, status);
    }

    ~BackupManager_L2Test() override
    {
        ReleaseBackupManagerInterfaceObject();

        TEST_LOG("Deactivating service %s", backupManagerCallsign);
        const uint32_t status = DeactivateService(backupManagerCallsign);
        TEST_LOG("DeactivateService returned: %d (%s)", status, Core::ErrorToString(status));

        TEST_LOG("Deactivating service %s", userSettingsCallsign);
        const uint32_t status2 = DeactivateService(userSettingsCallsign);
        TEST_LOG("DeactivateService returned: %d (%s)", status2, Core::ErrorToString(status2));

        TEST_LOG("Deactivating service %s", persistentStoreCallsign);
        const uint32_t status3 = DeactivateService(persistentStoreCallsign);
        TEST_LOG("DeactivateService returned: %d (%s)", status3, Core::ErrorToString(status3));
    }

    uint32_t CreateBackupManagerInterfaceObject()
    {
        uint32_t return_value = Core::ERROR_GENERAL;

        TEST_LOG("Creating BackupManager_Engine");
        BackupManager_Engine = Core::ProxyType<RPC::InvokeServerType<1, 0, 4>>::Create();

        BackupManager_Client = Core::ProxyType<RPC::CommunicatorClient>::Create(
            Core::NodeId("/tmp/communicator"),
            Core::ProxyType<Core::IIPCServer>(BackupManager_Engine));

        TEST_LOG("Creating BackupManager_Engine Announcements");
#if ((THUNDER_VERSION == 2) || ((THUNDER_VERSION == 4) && (THUNDER_VERSION_MINOR == 2)))
        BackupManager_Engine->Announcements(BackupManager_Client->Announcement());
#endif

        if (!BackupManager_Client.IsValid()) {
            TEST_LOG("Invalid BackupManager_Client");
        } else {
            m_controller_BackupManager =
                BackupManager_Client->Open<PluginHost::IShell>(backupManagerCallsign, ~0, 3000);

            if (m_controller_BackupManager != nullptr) {
                m_BackupManagerPlugin =
                    m_controller_BackupManager->QueryInterface<Exchange::IBackupManager>();

                if (m_BackupManagerPlugin != nullptr) {
                    return_value = Core::ERROR_NONE;
                    TEST_LOG("Successfully created BackupManager Plugin Interface");
                } else {
                    TEST_LOG("Failed to get BackupManager Plugin Interface");
                }
            } else {
                TEST_LOG("Failed to get BackupManager controller");
            }
        }

        return return_value;
    }

    void ReleaseBackupManagerInterfaceObject()
    {
        if (m_BackupManagerPlugin != nullptr) {
            m_BackupManagerPlugin->Release();
            m_BackupManagerPlugin = nullptr;
        }

        if (m_controller_BackupManager != nullptr) {
            m_controller_BackupManager->Release();
            m_controller_BackupManager = nullptr;
        }
    }

    Exchange::BackupContext DefaultContext() const
    {
        Exchange::BackupContext context;
        context.scenario = Exchange::HOSPITALITY_RESET;
        context.variant = "generic";
        context.persistentPath = "/opt/secure/persistent/settings_backup/";
        
        return context;
    }
};

TEST_F(BackupManager_L2Test, BackupSettings_Success)
{
    if (!m_BackupManagerPlugin)
    {
        TEST_LOG("m_BackupManagerPlugin is NULL");
        return;
    }

    JsonObject result;
    
    JsonObject persistentStoreParams;
    persistentStoreParams["namespace"] = "UserSettings";
    persistentStoreParams["key"] = "contentPin";
    persistentStoreParams["scope"] = "device";
    EXPECT_EQ(InvokeServiceMethod("org.rdk.PersistentStore", "deleteKey", persistentStoreParams, result), Core::ERROR_NONE);

    persistentStoreParams["key"] = "privacyMode";
    EXPECT_EQ(InvokeServiceMethod("org.rdk.PersistentStore", "deleteKey", persistentStoreParams, result), Core::ERROR_NONE);

    JsonObject usersettingsParams;
    usersettingsParams["preferredLanguages"] = "eng";
    
    EXPECT_EQ(InvokeServiceMethod("org.rdk.UserSettings", "setPreferredAudioLanguages", usersettingsParams, result), Core::ERROR_NONE);

    Exchange::BackupContext context = DefaultContext();
    EXPECT_EQ(Core::ERROR_NONE, m_BackupManagerPlugin->BackupSettings(context));
    EXPECT_TRUE(Core::Directory(context.persistentPath.c_str()).Exists());
}

TEST_F(BackupManager_L2Test, RestoreSettings_CustomPathFailure)
{
    if (!m_BackupManagerPlugin)
    {
        TEST_LOG("m_BackupManagerPlugin is NULL");
        return;
    }

    Exchange::BackupContext context = DefaultContext();
    // This custom path does not exist and should cause RestoreSettings to fail with ERROR_INVALID_PARAMETER
    context.persistentPath = "/tmp/persistent/settings_backup_cuustom/";
    EXPECT_EQ(Core::ERROR_INVALID_PARAMETER, m_BackupManagerPlugin->BackupSettings(context));
}

TEST_F(BackupManager_L2Test, BackupSettings_CustomPathSuccess)
{
    if (!m_BackupManagerPlugin)
    {
        TEST_LOG("m_BackupManagerPlugin is NULL");
        return;
    }

    JsonObject usersettingsParams;
    JsonObject result;

    usersettingsParams["preferredLanguages"] = "es";
    EXPECT_EQ(InvokeServiceMethod("org.rdk.UserSettings", "setPreferredAudioLanguages", usersettingsParams, result), Core::ERROR_NONE);

    usersettingsParams["contentPin"] = "1234";
    EXPECT_EQ(InvokeServiceMethod("org.rdk.UserSettings", "setContentPin", usersettingsParams, result), Core::ERROR_NONE);

    Exchange::BackupContext context = DefaultContext();
    context.persistentPath = "/tmp/persistent/settings_backup_custom/";
    mkdir(context.persistentPath.c_str(), 0777);
    EXPECT_EQ(Core::ERROR_NONE, m_BackupManagerPlugin->BackupSettings(context));

    usersettingsParams["contentPin"] = "5678";
    EXPECT_EQ(InvokeServiceMethod("org.rdk.UserSettings", "setContentPin", usersettingsParams, result), Core::ERROR_NONE);

    usersettingsParams["privacyMode"] = "DO_NOT_SHARE";
    EXPECT_EQ(InvokeServiceMethod("org.rdk.UserSettings", "setPrivacyMode", usersettingsParams, result), Core::ERROR_NONE);
    
    context.variant = "variant2";
    EXPECT_EQ(Core::ERROR_NONE, m_BackupManagerPlugin->BackupSettings(context));
}

TEST_F(BackupManager_L2Test, RestoreSettings_Success)
{
    if (!m_BackupManagerPlugin)
    {
        TEST_LOG("m_BackupManagerPlugin is NULL");
        return;
    }

    Core::JSON::String resultString;

    Exchange::BackupContext context = DefaultContext();    
    context.persistentPath = "/tmp/persistent/settings_backup_custom/";
    context.variant = "variant2";
    EXPECT_EQ(Core::ERROR_NONE, m_BackupManagerPlugin->RestoreSettings(context));   

    EXPECT_EQ(InvokeServiceMethod("org.rdk.UserSettings", "getPreferredAudioLanguages", resultString), Core::ERROR_NONE);
    EXPECT_EQ(resultString.Value(), "es");

    EXPECT_EQ(InvokeServiceMethod("org.rdk.UserSettings", "getContentPin", resultString), Core::ERROR_NONE);
    EXPECT_EQ(resultString.Value(), "5678");

    EXPECT_EQ(InvokeServiceMethod("org.rdk.UserSettings", "getPrivacyMode", resultString), Core::ERROR_NONE);
    EXPECT_EQ(resultString.Value(), "DO_NOT_SHARE");

    context = DefaultContext();
    context.persistentPath = "/tmp/persistent/settings_backup_custom/";
    EXPECT_EQ(Core::ERROR_NONE, m_BackupManagerPlugin->RestoreSettings(context));

    EXPECT_EQ(InvokeServiceMethod("org.rdk.UserSettings", "getPreferredAudioLanguages", resultString), Core::ERROR_NONE);
    EXPECT_EQ(resultString.Value(), "es");

    EXPECT_EQ(InvokeServiceMethod("org.rdk.UserSettings", "getContentPin", resultString), Core::ERROR_NONE);
    EXPECT_EQ(resultString.Value(), "1234");

    EXPECT_EQ(InvokeServiceMethod("org.rdk.UserSettings", "getPrivacyMode", resultString), Core::ERROR_NONE);
    EXPECT_EQ(resultString.Value(), "SHARE");

    context = DefaultContext();
    EXPECT_EQ(Core::ERROR_NONE, m_BackupManagerPlugin->RestoreSettings(context));

    EXPECT_EQ(InvokeServiceMethod("org.rdk.UserSettings", "getPreferredAudioLanguages", resultString), Core::ERROR_NONE);
    EXPECT_EQ(resultString.Value(), "eng");

    EXPECT_EQ(InvokeServiceMethod("org.rdk.UserSettings", "getContentPin", resultString), Core::ERROR_NONE);
    EXPECT_EQ(resultString.Value(), "");

    EXPECT_EQ(InvokeServiceMethod("org.rdk.UserSettings", "getPrivacyMode", resultString), Core::ERROR_NONE);
    EXPECT_EQ(resultString.Value(), "SHARE");
}

TEST_F(BackupManager_L2Test, DeleteBackup_Success)
{
    if (!m_BackupManagerPlugin)
    {
        TEST_LOG("m_BackupManagerPlugin is NULL");
        return;
    }

    Exchange::BackupContext context = DefaultContext();
    
    EXPECT_EQ(Core::ERROR_NONE, m_BackupManagerPlugin->RestoreSettings(context));   
    EXPECT_EQ(Core::ERROR_NONE, m_BackupManagerPlugin->DeleteBackup(context));

    EXPECT_EQ(Core::ERROR_GENERAL, m_BackupManagerPlugin->RestoreSettings(context));   
    EXPECT_EQ(Core::ERROR_GENERAL, m_BackupManagerPlugin->DeleteBackup(context));

    context.persistentPath = "/tmp/persistent/settings_backup_custom/";
    context.variant = "variant2";

    EXPECT_EQ(Core::ERROR_NONE, m_BackupManagerPlugin->RestoreSettings(context));   
    EXPECT_EQ(Core::ERROR_NONE, m_BackupManagerPlugin->DeleteBackup(context));

    EXPECT_EQ(Core::ERROR_GENERAL, m_BackupManagerPlugin->RestoreSettings(context));   
    EXPECT_EQ(Core::ERROR_GENERAL, m_BackupManagerPlugin->DeleteBackup(context));
}

