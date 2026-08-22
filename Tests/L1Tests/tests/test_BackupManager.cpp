/**
* If not stated otherwise in this file or this component's LICENSE
* file the following copyright and licenses apply:
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
**/


#include <gmock/gmock.h>
#include <gtest/gtest.h>
#include <atomic>
#include <limits>

#include "COMLinkMock.h"
#include "BackupManager.h"
#include "BackupManagerImplementation.h"
#include "ServiceMock.h"
#include "ThunderPortability.h"
#include "WrapsMock.h"

using ::testing::_;
using ::testing::Invoke;
using ::testing::NiceMock;
using namespace WPEFramework;

class BackupProviderMock : public Exchange::IBackupProvider {
public:
    BackupProviderMock() = default;
    virtual ~BackupProviderMock() override = default;

    MOCK_METHOD(uint32_t, Backup, (const Exchange::BackupContext& context), (override));
    MOCK_METHOD(uint32_t, Restore, (const Exchange::BackupContext& context), (override));
    MOCK_METHOD(uint32_t, Delete, (const Exchange::BackupContext& context), (override));

    MOCK_METHOD(uint32_t, AddRef, (), (const, override));
    MOCK_METHOD(uint32_t, Release, (), (const, override));
    MOCK_METHOD(void*, QueryInterface, (const uint32_t interfaceId), (override));
};

class BackupManagerTest : public ::testing::Test {
protected:
    Core::ProxyType<Plugin::BackupManager> plugin;
    Core::JSONRPC::Handler& handler;
    Core::JSONRPC::Context connection;
    Core::JSONRPC::Message message;
    NiceMock<ServiceMock> service;
    NiceMock<COMLinkMock> comLinkMock;
    NiceMock<BackupProviderMock> backupProviderMock;

    string response;
    WrapsImplMock* p_wrapsImplMock = nullptr;
    ServiceMock* p_serviceMock = nullptr;

    BackupManagerTest()
        : plugin(Core::ProxyType<Plugin::BackupManager>::Create())
        , handler(*plugin)
        , connection(1, 0, "")
    {
        p_serviceMock = new NiceMock<ServiceMock>;

        p_wrapsImplMock = new NiceMock<WrapsImplMock>;
        Wraps::setImpl(p_wrapsImplMock);

        ON_CALL(backupProviderMock, QueryInterface(_))
            .WillByDefault(Return(&backupProviderMock));

        plugin->Initialize(&service);
    }

    ~BackupManagerTest() override
    {
        plugin->Deinitialize(&service);

        if (p_serviceMock != nullptr) {
            delete p_serviceMock;
            p_serviceMock = nullptr;
        }

        Wraps::setImpl(nullptr);
        if (p_wrapsImplMock != nullptr) {
            delete p_wrapsImplMock;
            p_wrapsImplMock = nullptr;
        }
    }
};

class BackupManagerTestProvider : public BackupManagerTest {
public:
    BackupManagerTestProvider() : BackupManagerTest() {
        ON_CALL(service, QueryInterface(Exchange::IBackupProvider::ID))
            .WillByDefault(::testing::Return(static_cast<void*>(&backupProviderMock)));

        Exchange::IBackupManager* backupManagerInterface = static_cast<Exchange::IBackupManager*>(plugin->QueryInterface(Exchange::IBackupManager::ID));

        if (backupManagerInterface != nullptr) {
            static_cast<Plugin::BackupManagerImplementation*>(backupManagerInterface)->PluginActivated("TestBackupProvider", &service);
        }
    }

    ~BackupManagerTestProvider() override {
        Exchange::IBackupManager* backupManagerInterface = static_cast<Exchange::IBackupManager*>(plugin->QueryInterface(Exchange::IBackupManager::ID));

        if (backupManagerInterface != nullptr) {
            static_cast<Plugin::BackupManagerImplementation*>(backupManagerInterface)->PluginDeactivated("TestBackupProvider", &service);
        }
    }
};

TEST_F(BackupManagerTestProvider, BackupSettingsSuccess)
{
    EXPECT_CALL(backupProviderMock, Backup(_))
        .WillOnce(testing::Return(Core::ERROR_NONE));

    EXPECT_EQ(Core::ERROR_NONE, handler.Invoke(connection, _T("backupSettings"), _T("{\"context\":{\"scenario\":\"HOSPITALITY_RESET\"}}"), response));
}

TEST_F(BackupManagerTestProvider, BackupSettingsFailure)
{
    EXPECT_CALL(backupProviderMock, Backup(_))
        .WillOnce(testing::Return(Core::ERROR_GENERAL));

    EXPECT_EQ(Core::ERROR_GENERAL, handler.Invoke(connection, _T("backupSettings"), _T("{\"context\":{\"scenario\":\"HOSPITALITY_RESET\"}}"), response));
}

TEST_F(BackupManagerTest, BackupSettingsNoProviders)
{
    EXPECT_EQ(Core::ERROR_NONE, handler.Invoke(connection, _T("backupSettings"), _T("{\"context\":{\"scenario\":\"HOSPITALITY_RESET\"}}"), response));
}

TEST_F(BackupManagerTest, BackupSettingsInvalidParams)
{
    EXPECT_EQ(Core::ERROR_INVALID_PARAMETER, handler.Invoke(connection, _T("backupSettings"), _T("{\"context\":{\"scenario\":\"HOSPITALITY_RESET\", \"persistentPath\":\"/invalid_path\"}}"), response));
}

TEST_F(BackupManagerTestProvider, RestoreSettingsSuccess)
{
    EXPECT_CALL(backupProviderMock, Restore(_))
        .WillOnce(testing::Return(Core::ERROR_NONE));

    EXPECT_EQ(Core::ERROR_NONE, handler.Invoke(connection, _T("restoreSettings"), _T("{\"context\":{\"scenario\":\"HOSPITALITY_RESET\"}}"), response));
}

TEST_F(BackupManagerTestProvider, RestoreSettingsFailure)
{
    EXPECT_CALL(backupProviderMock, Restore(_))
        .WillOnce(testing::Return(Core::ERROR_GENERAL));

    EXPECT_EQ(Core::ERROR_GENERAL, handler.Invoke(connection, _T("restoreSettings"), _T("{\"context\":{\"scenario\":\"HOSPITALITY_RESET\"}}"), response));
}

TEST_F(BackupManagerTest, RestoreSettingsNoProviders)
{
    EXPECT_EQ(Core::ERROR_NONE, handler.Invoke(connection, _T("restoreSettings"), _T("{\"context\":{\"scenario\":\"HOSPITALITY_RESET\"}}"), response));
}

TEST_F(BackupManagerTestProvider, DeleteBackupSuccess)
{
    EXPECT_CALL(backupProviderMock, Delete(_))
        .WillOnce(testing::Return(Core::ERROR_NONE));

    EXPECT_EQ(Core::ERROR_NONE, handler.Invoke(connection, _T("deleteBackup"), _T("{\"context\":{\"scenario\":\"HOSPITALITY_RESET\"}}"), response));
}

TEST_F(BackupManagerTestProvider, DeleteBackupFailure)
{
    EXPECT_CALL(backupProviderMock, Delete(_))
        .WillOnce(testing::Return(Core::ERROR_GENERAL));

    EXPECT_EQ(Core::ERROR_GENERAL, handler.Invoke(connection, _T("deleteBackup"), _T("{\"context\":{\"scenario\":\"HOSPITALITY_RESET\"}}"), response));
}

TEST_F(BackupManagerTest, DeleteBackupNoProviders)
{
    EXPECT_EQ(Core::ERROR_NONE, handler.Invoke(connection, _T("deleteBackup"), _T("{\"context\":{\"scenario\":\"HOSPITALITY_RESET\"}}"), response));
}

// Path traversal security tests
TEST_F(BackupManagerTest, BackupSettingsPathTraversalAttack)
{
    // Test path traversal attempt with ../ sequences
    EXPECT_EQ(Core::ERROR_INVALID_PARAMETER, handler.Invoke(connection, _T("backupSettings"), _T("{\"context\":{\"scenario\":\"HOSPITALITY_RESET\", \"persistentPath\":\"/opt/secure/persistent/settings_backup/../../../etc/\"}}"), response));
}

TEST_F(BackupManagerTest, BackupSettingsAbsolutePathOutsideAllowed)
{
    // Test absolute path outside the allowed directory
    EXPECT_EQ(Core::ERROR_INVALID_PARAMETER, handler.Invoke(connection, _T("backupSettings"), _T("{\"context\":{\"scenario\":\"HOSPITALITY_RESET\", \"persistentPath\":\"/tmp/pwned_backup/\"}}"), response));
}

TEST_F(BackupManagerTest, RestoreSettingsPathTraversalAttack)
{
    // Test path traversal attempt with ../ sequences
    EXPECT_EQ(Core::ERROR_INVALID_PARAMETER, handler.Invoke(connection, _T("restoreSettings"), _T("{\"context\":{\"scenario\":\"HOSPITALITY_RESET\", \"persistentPath\":\"/opt/secure/persistent/settings_backup/../../../etc/\"}}"), response));
}

TEST_F(BackupManagerTest, DeleteBackupPathTraversalAttack)
{
    // Test path traversal attempt with ../ sequences
    EXPECT_EQ(Core::ERROR_INVALID_PARAMETER, handler.Invoke(connection, _T("deleteBackup"), _T("{\"context\":{\"scenario\":\"HOSPITALITY_RESET\", \"persistentPath\":\"/opt/secure/persistent/settings_backup/../../../etc/\"}}"), response));
}
