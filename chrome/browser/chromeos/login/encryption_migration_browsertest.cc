// Copyright 2019 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <string>

#include "base/command_line.h"
#include "base/files/file_path.h"
#include "base/files/scoped_temp_dir.h"
#include "base/optional.h"
#include "base/path_service.h"
#include "base/strings/string_piece.h"
#include "base/test/simple_test_tick_clock.h"
#include "base/threading/thread_restrictions.h"
#include "base/time/time.h"
#include "chrome/browser/chromeos/arc/policy/arc_policy_util.h"
#include "chrome/browser/chromeos/login/login_wizard.h"
#include "chrome/browser/chromeos/login/mixin_based_in_process_browser_test.h"
#include "chrome/browser/chromeos/login/oobe_screen.h"
#include "chrome/browser/chromeos/login/test/js_checker.h"
#include "chrome/browser/chromeos/login/test/login_manager_mixin.h"
#include "chrome/browser/chromeos/login/test/oobe_screen_waiter.h"
#include "chrome/browser/chromeos/login/ui/login_display_host.h"
#include "chrome/browser/chromeos/login/wizard_controller.h"
#include "chrome/browser/ui/webui/chromeos/login/encryption_migration_screen_handler.h"
#include "chrome/browser/ui/webui/chromeos/login/oobe_ui.h"
#include "chromeos/constants/chromeos_switches.h"
#include "chromeos/dbus/constants/dbus_paths.h"
#include "chromeos/dbus/cryptohome/account_identifier_operators.h"
#include "chromeos/dbus/cryptohome/fake_cryptohome_client.h"
#include "chromeos/dbus/cryptohome/rpc.pb.h"
#include "chromeos/dbus/power/fake_power_manager_client.h"
#include "chromeos/dbus/session_manager/fake_session_manager_client.h"
#include "chromeos/login/auth/stub_authenticator_builder.h"
#include "chromeos/login/auth/user_context.h"
#include "components/account_id/account_id.h"
#include "components/policy/core/common/cloud/policy_builder.h"
#include "third_party/cros_system_api/dbus/cryptohome/dbus-constants.h"

namespace chromeos {

namespace {

OobeUI* GetOobeUI() {
  auto* host = LoginDisplayHost::default_host();
  return host ? host->GetOobeUI() : nullptr;
}

}  // namespace

class EncryptionMigrationTest : public MixinBasedInProcessBrowserTest {
 public:
  EncryptionMigrationTest() = default;
  ~EncryptionMigrationTest() override = default;

  void SetUpCommandLine(base::CommandLine* command_line) override {
    MixinBasedInProcessBrowserTest::SetUpCommandLine(command_line);
    // Enable ARC, so dircrypto encryption is forced.
    command_line->AppendSwitchASCII(switches::kArcAvailability,
                                    "officially-supported");
  }

  void SetUpInProcessBrowserTestFixture() override {
    // SessionManagerClient has to be in-memory to support setting sessionless
    // user policy blob.
    chromeos::SessionManagerClient::InitializeFakeInMemory();

    MixinBasedInProcessBrowserTest::SetUpInProcessBrowserTestFixture();
  }

  void SetUpOnMainThread() override {
    MixinBasedInProcessBrowserTest::SetUpOnMainThread();

    FakeCryptohomeClient::Get()->set_run_default_dircrypto_migration(false);

    InitializeTestUserPolicyKey();

    // Initialize OOBE UI, and configure encryption migration screen handler for
    // test.
    ShowLoginWizard(OobeScreen::SCREEN_TEST_NO_WINDOW);
    auto* handler = static_cast<EncryptionMigrationScreenHandler*>(
        GetOobeUI()->GetEncryptionMigrationScreenView());
    handler->SetFreeDiskSpaceFetcherForTesting(base::BindRepeating(
        &EncryptionMigrationTest::GetFreeSpace, base::Unretained(this)));
    handler->SetTickClockForTesting(&tick_clock_);
  }

 protected:
  void SetUpStubAuthenticatorAndAttemptLogin(bool has_incomplete_migration) {
    const UserContext user_context =
        LoginManagerMixin::CreateDefaultUserContext(test_user_);

    auto authenticator_builder =
        std::make_unique<StubAuthenticatorBuilder>(user_context);
    authenticator_builder->SetUpOldEncryption(has_incomplete_migration);

    login_manager_.AttemptLoginUsingAuthenticator(
        user_context, std::move(authenticator_builder));
  }

  void WaitForActiveSession() { login_manager_.WaitForActiveSession(); }

  cryptohome::AccountIdentifier GetTestCryptohomeId() const {
    return cryptohome::CreateAccountIdentifierFromAccountId(
        test_user_.account_id);
  }

  void SetUpEncryptionMigrationActionPolicy(
      arc::policy_util::EcryptfsMigrationAction action) {
    policy_builder_.payload().mutable_ecryptfsmigrationstrategy()->set_value(
        static_cast<int>(action));
    policy_builder_.policy_data().set_username(
        test_user_.account_id.GetUserEmail());
    policy_builder_.policy_data().set_gaia_id(
        test_user_.account_id.GetGaiaId());
    policy_builder_.Build();

    // The pre-login policy fetcher will first check the cached policy, and then
    // the policy server. If not set up, the server request will fail by
    // default, and the policy fetcher will fall back to the cached policy -
    // this is good enough for these tests, so not bothering setting up the
    // policy test server.
    FakeSessionManagerClient::Get()->set_user_policy_without_session(
        GetTestCryptohomeId(), policy_builder_.GetBlob());
  }

  // Verifies that an element within "encryption-migration-element" DOM is
  // currently visible.
  void VerifyUiElementVisible(const std::string& element_id) {
    std::initializer_list<base::StringPiece> element = {
        "encryption-migration-element", element_id};
    ASSERT_TRUE(test::OobeJS().GetBool(test::GetOobeElementPath(element)));
    test::OobeJS().ExpectPathDisplayed(true, element);
  }

  // Verifies that an element within "encryption-migration-element" DOM is
  // currently not visible.
  void VerifyUiElementNotVisible(const std::string& element_id) {
    std::initializer_list<base::StringPiece> element = {
        "encryption-migration-element", element_id};
    // The element not being yet created is sufficient to verify it's not
    // visible
    if (!test::OobeJS().GetBool(test::GetOobeElementPath(element))) {
      return;
    }
    test::OobeJS().ExpectPathDisplayed(false, element);
  }

  // Waits until a DOM element within "encryption-migration-element" is
  // created and injected into the "encryption-migration-element".
  void WaitForElementCreation(const std::string& element_id) {
    std::initializer_list<base::StringPiece> element = {
        "encryption-migration-element", element_id};
    test::OobeJS().CreateWaiter(test::GetOobeElementPath(element))->Wait();
  }

  // Runs a successful full migration flow, and tests that UI is updated as
  // expected.
  void RunFullMigrationFlowTest() {
    WaitForElementCreation("migrating-dialog");
    VerifyUiElementVisible("migrating-dialog");

    VerifyUiElementNotVisible("ready-dialog");
    VerifyUiElementNotVisible("error-dialog");
    VerifyUiElementNotVisible("insufficient-space-dialog");
    VerifyUiElementNotVisible("minimal-migration-dialog");

    EXPECT_EQ(
        GetTestCryptohomeId(),
        FakeCryptohomeClient::Get()->get_id_for_disk_migrated_to_dircrypto());
    EXPECT_FALSE(FakeCryptohomeClient::Get()->minimal_migration());

    EXPECT_EQ(0, FakePowerManagerClient::Get()->num_request_restart_calls());

    // Simulate successful migration - restart should be requested immediately
    // after success is reported.
    FakeCryptohomeClient::Get()->NotifyDircryptoMigrationProgress(
        cryptohome::DIRCRYPTO_MIGRATION_INITIALIZING, 0 /*current*/,
        5 /*total*/);
    EXPECT_EQ(0, FakePowerManagerClient::Get()->num_request_restart_calls());

    std::initializer_list<base::StringPiece> migration_progress = {
        "encryption-migration-element", "migration-progress"};
    test::OobeJS().ExpectTrue(test::GetOobeElementPath(migration_progress) +
                              ".indeterminate");

    FakeCryptohomeClient::Get()->NotifyDircryptoMigrationProgress(
        cryptohome::DIRCRYPTO_MIGRATION_IN_PROGRESS, 3 /*current*/,
        5 /*total*/);
    EXPECT_EQ(0, FakePowerManagerClient::Get()->num_request_restart_calls());

    test::OobeJS().ExpectFalse(test::GetOobeElementPath(migration_progress) +
                               ".indeterminate");

    test::OobeJS().ExpectEQ(
        test::GetOobeElementPath(migration_progress) + ".value * 100", 60);
    test::OobeJS().ExpectEQ(
        test::GetOobeElementPath(migration_progress) + ".max", 1);

    FakeCryptohomeClient::Get()->NotifyDircryptoMigrationProgress(
        cryptohome::DIRCRYPTO_MIGRATION_SUCCESS, 5 /*current*/, 5 /*total*/);

    EXPECT_EQ(1, FakePowerManagerClient::Get()->num_request_restart_calls());
  }

  // Updates the battery percent info reported by the power manager client.
  void SetBatteryPercent(int battery_percent) {
    base::Optional<power_manager::PowerSupplyProperties> properties =
        FakePowerManagerClient::Get()->GetLastStatus();
    ASSERT_TRUE(properties.has_value());
    properties->set_battery_percent(battery_percent);
    FakePowerManagerClient::Get()->UpdatePowerProperties(properties.value());
  }

  void set_free_space(int64_t free_space) { free_space_ = free_space; }

  void AdvanceTime(base::TimeDelta delta) { tick_clock_.Advance(delta); }

 private:
  int64_t GetFreeSpace() const { return free_space_; }

  // Creates and stores the test user's policy key blob.
  void InitializeTestUserPolicyKey() {
    base::FilePath policy_key_dir;
    ASSERT_TRUE(base::PathService::Get(
        chromeos::dbus_paths::DIR_USER_POLICY_KEYS, &policy_key_dir));
    const std::string sanitized_username =
        chromeos::CryptohomeClient::GetStubSanitizedUsername(
            GetTestCryptohomeId());
    base::FilePath user_policy_key_path =
        policy_key_dir.AppendASCII(sanitized_username)
            .AppendASCII("policy.pub");

    std::string public_key = policy_builder_.GetPublicSigningKeyAsString();
    {
      base::ScopedAllowBlockingForTesting allow_blocking;
      ASSERT_TRUE(base::CreateDirectory(user_policy_key_path.DirName()));
      ASSERT_EQ(static_cast<int>(public_key.size()),
                base::WriteFile(user_policy_key_path, public_key.data(),
                                public_key.size()));
    }
  }

  // Encryption migration requires at least 50 MB - set the default reported
  // free space to an arbitrary amount above that limit.
  int64_t free_space_ = 200 * 1024 * 1024;

  base::SimpleTestTickClock tick_clock_;

  policy::UserPolicyBuilder policy_builder_;

  const LoginManagerMixin::TestUserInfo test_user_{
      AccountId::FromUserEmailGaiaId("user@gmail.com", "user")};
  LoginManagerMixin login_manager_{&mixin_host_, {test_user_}};
};

IN_PROC_BROWSER_TEST_F(EncryptionMigrationTest, SkipWithNoPolicySet) {
  SetUpStubAuthenticatorAndAttemptLogin(false /* has_incomplete_migration */);
  OobeScreenWaiter(OobeScreen::SCREEN_ENCRYPTION_MIGRATION).Wait();

  WaitForElementCreation("ready-dialog");
  VerifyUiElementVisible("ready-dialog");

  VerifyUiElementNotVisible("migrating-dialog");
  VerifyUiElementNotVisible("error-dialog");
  VerifyUiElementNotVisible("insufficient-space-dialog");
  VerifyUiElementNotVisible("minimal-migration-dialog");

  VerifyUiElementVisible("skip-button");
  VerifyUiElementVisible("upgrade-button");

  // Click skip - this should start the user session.
  test::OobeJS().TapOnPath({"encryption-migration-element", "skip-button"});

  WaitForActiveSession();

  EXPECT_FALSE(FakeCryptohomeClient::Get()
                   ->get_id_for_disk_migrated_to_dircrypto()
                   .has_account_id());
}

IN_PROC_BROWSER_TEST_F(EncryptionMigrationTest, MigrateWithNoUserPolicySet) {
  SetUpStubAuthenticatorAndAttemptLogin(false /* has_incomplete_migration */);
  OobeScreenWaiter(OobeScreen::SCREEN_ENCRYPTION_MIGRATION).Wait();

  WaitForElementCreation("ready-dialog");
  VerifyUiElementVisible("ready-dialog");

  VerifyUiElementNotVisible("migrating-dialog");
  VerifyUiElementNotVisible("error-dialog");
  VerifyUiElementNotVisible("insufficient-space-dialog");
  VerifyUiElementNotVisible("minimal-migration-dialog");

  VerifyUiElementVisible("skip-button");
  VerifyUiElementVisible("upgrade-button");

  EXPECT_FALSE(FakeCryptohomeClient::Get()
                   ->get_id_for_disk_migrated_to_dircrypto()
                   .has_account_id());

  test::OobeJS().TapOnPath({"encryption-migration-element", "upgrade-button"});

  RunFullMigrationFlowTest();
}

IN_PROC_BROWSER_TEST_F(EncryptionMigrationTest,
                       ResumeMigrationWithNoUserPolicySet) {
  SetUpStubAuthenticatorAndAttemptLogin(true /* has_incomplete_migration */);
  OobeScreenWaiter(OobeScreen::SCREEN_ENCRYPTION_MIGRATION).Wait();

  // Migration is expected to continue immediately.
  RunFullMigrationFlowTest();
}

IN_PROC_BROWSER_TEST_F(EncryptionMigrationTest, MigratePolicy) {
  SetUpEncryptionMigrationActionPolicy(
      arc::policy_util::EcryptfsMigrationAction::kMigrate);

  SetUpStubAuthenticatorAndAttemptLogin(false /* has_incomplete_migration */);
  OobeScreenWaiter(OobeScreen::SCREEN_ENCRYPTION_MIGRATION).Wait();

  // With kMigrate policy, the migration should start immediately.
  RunFullMigrationFlowTest();
}

IN_PROC_BROWSER_TEST_F(EncryptionMigrationTest,
                       ResumeMigrationWithMigratePolicy) {
  SetUpEncryptionMigrationActionPolicy(
      arc::policy_util::EcryptfsMigrationAction::kMigrate);

  SetUpStubAuthenticatorAndAttemptLogin(true /* has_incomplete_migration */);
  OobeScreenWaiter(OobeScreen::SCREEN_ENCRYPTION_MIGRATION).Wait();

  RunFullMigrationFlowTest();
}

// The "ask user" mode should be available to consumer users only - is set as a
// policy value, it should be treated the same as migrate.
IN_PROC_BROWSER_TEST_F(EncryptionMigrationTest, AskUserPolicy) {
  SetUpEncryptionMigrationActionPolicy(
      arc::policy_util::EcryptfsMigrationAction::kAskUser);

  SetUpStubAuthenticatorAndAttemptLogin(false /* has_incomplete_migration */);
  OobeScreenWaiter(OobeScreen::SCREEN_ENCRYPTION_MIGRATION).Wait();

  // Verify that ready dialog is not shown, and that the migration started
  // without ask user for confirmation.
  WaitForElementCreation("migrating-dialog");
  VerifyUiElementVisible("migrating-dialog");
  VerifyUiElementNotVisible("ready-dialog");

  EXPECT_EQ(
      GetTestCryptohomeId(),
      FakeCryptohomeClient::Get()->get_id_for_disk_migrated_to_dircrypto());
  EXPECT_FALSE(FakeCryptohomeClient::Get()->minimal_migration());
}

IN_PROC_BROWSER_TEST_F(EncryptionMigrationTest, MinimalMigration) {
  SetUpEncryptionMigrationActionPolicy(
      arc::policy_util::EcryptfsMigrationAction::kMinimalMigrate);

  SetUpStubAuthenticatorAndAttemptLogin(false /* has_incomplete_migration */);
  OobeScreenWaiter(OobeScreen::SCREEN_ENCRYPTION_MIGRATION).Wait();

  WaitForElementCreation("minimal-migration-dialog");
  VerifyUiElementVisible("minimal-migration-dialog");

  VerifyUiElementNotVisible("ready-dialog");
  VerifyUiElementNotVisible("migrating-dialog");
  VerifyUiElementNotVisible("error-dialog");
  VerifyUiElementNotVisible("insufficient-space-dialog");

  EXPECT_EQ(0, FakePowerManagerClient::Get()->num_request_restart_calls());

  EXPECT_EQ(
      GetTestCryptohomeId(),
      FakeCryptohomeClient::Get()->get_id_for_disk_migrated_to_dircrypto());
  EXPECT_TRUE(FakeCryptohomeClient::Get()->minimal_migration());

  // Simulate migration success - restart should be requested immediately after.
  FakeCryptohomeClient::Get()->NotifyDircryptoMigrationProgress(
      cryptohome::DIRCRYPTO_MIGRATION_SUCCESS, 5 /*current*/, 5 /*total*/);
  EXPECT_EQ(0, FakePowerManagerClient::Get()->num_request_restart_calls());

  WaitForActiveSession();
}

IN_PROC_BROWSER_TEST_F(EncryptionMigrationTest, MinimalMigrationWithTimeout) {
  SetUpEncryptionMigrationActionPolicy(
      arc::policy_util::EcryptfsMigrationAction::kMinimalMigrate);

  SetUpStubAuthenticatorAndAttemptLogin(false /* has_incomplete_migration */);
  OobeScreenWaiter(OobeScreen::SCREEN_ENCRYPTION_MIGRATION).Wait();

  WaitForElementCreation("minimal-migration-dialog");
  VerifyUiElementVisible("minimal-migration-dialog");

  VerifyUiElementNotVisible("ready-dialog");
  VerifyUiElementNotVisible("migrating-dialog");
  VerifyUiElementNotVisible("error-dialog");
  VerifyUiElementNotVisible("insufficient-space-dialog");

  EXPECT_EQ(0, FakePowerManagerClient::Get()->num_request_restart_calls());

  EXPECT_EQ(
      GetTestCryptohomeId(),
      FakeCryptohomeClient::Get()->get_id_for_disk_migrated_to_dircrypto());
  EXPECT_TRUE(FakeCryptohomeClient::Get()->minimal_migration());

  // Simulate time passage during migration - enough for the user to get asked
  // to reauthenticate upon migration completion..
  AdvanceTime(base::TimeDelta::FromMinutes(3));

  FakeCryptohomeClient::Get()->NotifyDircryptoMigrationProgress(
      cryptohome::DIRCRYPTO_MIGRATION_SUCCESS, 5 /*current*/, 5 /*total*/);
  EXPECT_EQ(0, FakePowerManagerClient::Get()->num_request_restart_calls());

  OobeScreenWaiter(OobeScreen::SCREEN_GAIA_SIGNIN).Wait();
}

IN_PROC_BROWSER_TEST_F(EncryptionMigrationTest,
                       PRE_MinimalMigrationPolicyWithIncompleteFullMigration) {
  SetUpEncryptionMigrationActionPolicy(
      arc::policy_util::EcryptfsMigrationAction::kMigrate);

  SetUpStubAuthenticatorAndAttemptLogin(false /* has_incomplete_migration */);
  OobeScreenWaiter(OobeScreen::SCREEN_ENCRYPTION_MIGRATION).Wait();

  WaitForElementCreation("migrating-dialog");
}

// Tests that attempted full migration is continued, even if the migration mode
// changes to minimal in mean time.
IN_PROC_BROWSER_TEST_F(EncryptionMigrationTest,
                       MinimalMigrationPolicyWithIncompleteFullMigration) {
  SetUpEncryptionMigrationActionPolicy(
      arc::policy_util::EcryptfsMigrationAction::kMinimalMigrate);

  SetUpStubAuthenticatorAndAttemptLogin(true /* has_incomplete_migration */);
  OobeScreenWaiter(OobeScreen::SCREEN_ENCRYPTION_MIGRATION).Wait();

  RunFullMigrationFlowTest();
}

IN_PROC_BROWSER_TEST_F(EncryptionMigrationTest, PRE_ResumeMinimalMigration) {
  SetUpEncryptionMigrationActionPolicy(
      arc::policy_util::EcryptfsMigrationAction::kMinimalMigrate);

  SetUpStubAuthenticatorAndAttemptLogin(false /* has_incomplete_migration */);
  OobeScreenWaiter(OobeScreen::SCREEN_ENCRYPTION_MIGRATION).Wait();

  WaitForElementCreation("minimal-migration-dialog");
}

IN_PROC_BROWSER_TEST_F(EncryptionMigrationTest, ResumeMinimalMigration) {
  SetUpEncryptionMigrationActionPolicy(
      arc::policy_util::EcryptfsMigrationAction::kMigrate);

  SetUpStubAuthenticatorAndAttemptLogin(true /* has_incomplete_migration */);
  OobeScreenWaiter(OobeScreen::SCREEN_ENCRYPTION_MIGRATION).Wait();

  WaitForElementCreation("minimal-migration-dialog");
  VerifyUiElementVisible("minimal-migration-dialog");

  EXPECT_EQ(0, FakePowerManagerClient::Get()->num_request_restart_calls());

  EXPECT_EQ(
      GetTestCryptohomeId(),
      FakeCryptohomeClient::Get()->get_id_for_disk_migrated_to_dircrypto());
  EXPECT_TRUE(FakeCryptohomeClient::Get()->minimal_migration());

  // Simulate migration success - restart should be requested immediately after.
  FakeCryptohomeClient::Get()->NotifyDircryptoMigrationProgress(
      cryptohome::DIRCRYPTO_MIGRATION_SUCCESS, 5 /*current*/, 5 /*total*/);
  EXPECT_EQ(0, FakePowerManagerClient::Get()->num_request_restart_calls());

  WaitForActiveSession();
}

IN_PROC_BROWSER_TEST_F(EncryptionMigrationTest, MigrationDisallowedByPolicy) {
  SetUpEncryptionMigrationActionPolicy(
      arc::policy_util::EcryptfsMigrationAction::kDisallowMigration);

  SetUpStubAuthenticatorAndAttemptLogin(false /* has_incomplete_migration */);
  WaitForActiveSession();
  EXPECT_FALSE(FakeCryptohomeClient::Get()
                   ->get_id_for_disk_migrated_to_dircrypto()
                   .has_account_id());
}

IN_PROC_BROWSER_TEST_F(EncryptionMigrationTest, WipeMigrationActionPolicy) {
  SetUpEncryptionMigrationActionPolicy(
      arc::policy_util::EcryptfsMigrationAction::kWipe);

  SetUpStubAuthenticatorAndAttemptLogin(false /* has_incomplete_migration */);

  // Wipe is expected to wipe the cryptohome, and force online login.
  OobeScreenWaiter(OobeScreen::SCREEN_GAIA_SIGNIN).Wait();

  EXPECT_FALSE(FakeCryptohomeClient::Get()
                   ->get_id_for_disk_migrated_to_dircrypto()
                   .has_account_id());
}

IN_PROC_BROWSER_TEST_F(EncryptionMigrationTest,
                       InsufficientSpaceWithNoUserPolicy) {
  set_free_space(5 * 1000 * 1000);

  SetUpStubAuthenticatorAndAttemptLogin(false /* has_incomplete_migration */);
  OobeScreenWaiter(OobeScreen::SCREEN_ENCRYPTION_MIGRATION).Wait();

  WaitForElementCreation("insufficient-space-dialog");
  VerifyUiElementVisible("insufficient-space-dialog");

  VerifyUiElementNotVisible("ready-dialog");
  VerifyUiElementNotVisible("migrating-dialog");
  VerifyUiElementNotVisible("error-dialog");
  VerifyUiElementNotVisible("minimal-migartion-dialog");

  VerifyUiElementNotVisible("insufficient-space-restart-button");
  VerifyUiElementVisible("insufficient-space-skip-button");
  test::OobeJS().TapOnPath(
      {"encryption-migration-element", "insufficient-space-skip-button"});

  WaitForActiveSession();
  EXPECT_FALSE(FakeCryptohomeClient::Get()
                   ->get_id_for_disk_migrated_to_dircrypto()
                   .has_account_id());
}

IN_PROC_BROWSER_TEST_F(EncryptionMigrationTest, MigrateWithInsuficientSpace) {
  set_free_space(5 * 1000 * 1000);
  SetUpEncryptionMigrationActionPolicy(
      arc::policy_util::EcryptfsMigrationAction::kMigrate);

  SetUpStubAuthenticatorAndAttemptLogin(false /* has_incomplete_migration */);
  OobeScreenWaiter(OobeScreen::SCREEN_ENCRYPTION_MIGRATION).Wait();

  WaitForElementCreation("insufficient-space-dialog");
  VerifyUiElementVisible("insufficient-space-dialog");

  VerifyUiElementNotVisible("ready-dialog");
  VerifyUiElementNotVisible("migrating-dialog");
  VerifyUiElementNotVisible("error-dialog");
  VerifyUiElementNotVisible("minimal-migartion-dialog");

  VerifyUiElementVisible("insufficient-space-restart-button");
  VerifyUiElementNotVisible("insufficient-space-skip-button");

  test::OobeJS().TapOnPath(
      {"encryption-migration-element", "insufficient-space-restart-button"});

  EXPECT_EQ(1, FakePowerManagerClient::Get()->num_request_restart_calls());
  EXPECT_FALSE(FakeCryptohomeClient::Get()
                   ->get_id_for_disk_migrated_to_dircrypto()
                   .has_account_id());
}

IN_PROC_BROWSER_TEST_F(EncryptionMigrationTest, InsuficientSpaceOnResume) {
  set_free_space(5 * 1000 * 1000);
  SetUpEncryptionMigrationActionPolicy(
      arc::policy_util::EcryptfsMigrationAction::kMigrate);

  SetUpStubAuthenticatorAndAttemptLogin(true /* has_incomplete_migration */);
  OobeScreenWaiter(OobeScreen::SCREEN_ENCRYPTION_MIGRATION).Wait();

  WaitForElementCreation("insufficient-space-dialog");
  VerifyUiElementVisible("insufficient-space-dialog");

  VerifyUiElementNotVisible("ready-dialog");
  VerifyUiElementNotVisible("migrating-dialog");
  VerifyUiElementNotVisible("error-dialog");
  VerifyUiElementNotVisible("minimal-migartion-dialog");

  VerifyUiElementVisible("insufficient-space-restart-button");
  VerifyUiElementNotVisible("insufficient-space-skip-button");

  test::OobeJS().TapOnPath(
      {"encryption-migration-element", "insufficient-space-restart-button"});

  EXPECT_EQ(1, FakePowerManagerClient::Get()->num_request_restart_calls());
  EXPECT_FALSE(FakeCryptohomeClient::Get()
                   ->get_id_for_disk_migrated_to_dircrypto()
                   .has_account_id());
}

IN_PROC_BROWSER_TEST_F(EncryptionMigrationTest, MigrationFailure) {
  SetUpEncryptionMigrationActionPolicy(
      arc::policy_util::EcryptfsMigrationAction::kMigrate);

  SetUpStubAuthenticatorAndAttemptLogin(false /* has_incomplete_migration */);
  OobeScreenWaiter(OobeScreen::SCREEN_ENCRYPTION_MIGRATION).Wait();

  WaitForElementCreation("migrating-dialog");

  EXPECT_EQ(
      GetTestCryptohomeId(),
      FakeCryptohomeClient::Get()->get_id_for_disk_migrated_to_dircrypto());
  FakeCryptohomeClient::Get()->NotifyDircryptoMigrationProgress(
      cryptohome::DIRCRYPTO_MIGRATION_FAILED, 5 /*current*/, 5 /*total*/);

  EXPECT_EQ(0, FakePowerManagerClient::Get()->num_request_restart_calls());

  WaitForElementCreation("error-dialog");
  VerifyUiElementVisible("error-dialog");

  VerifyUiElementNotVisible("ready-dialog");
  VerifyUiElementNotVisible("migrating-dialog");
  VerifyUiElementNotVisible("insufficient-space-dialog");
  VerifyUiElementNotVisible("minimal-migartion-dialog");

  VerifyUiElementVisible("restart-button");

  test::OobeJS().TapOnPath({"encryption-migration-element", "restart-button"});

  EXPECT_EQ(1, FakePowerManagerClient::Get()->num_request_restart_calls());
}

IN_PROC_BROWSER_TEST_F(EncryptionMigrationTest, LowBattery) {
  SetBatteryPercent(5);
  SetUpEncryptionMigrationActionPolicy(
      arc::policy_util::EcryptfsMigrationAction::kMigrate);

  SetUpStubAuthenticatorAndAttemptLogin(false /* has_incomplete_migration */);
  OobeScreenWaiter(OobeScreen::SCREEN_ENCRYPTION_MIGRATION).Wait();

  WaitForElementCreation("ready-dialog");
  VerifyUiElementVisible("ready-dialog");

  VerifyUiElementNotVisible("migrating-dialog");
  VerifyUiElementNotVisible("insufficient-space-dialog");
  VerifyUiElementNotVisible("error-dialog");
  VerifyUiElementNotVisible("minimal-migartion-dialog");

  VerifyUiElementVisible("skip-button");
  test::OobeJS().ExpectEnabledPath(
      {"encryption-migration-element", "skip-button"});

  VerifyUiElementVisible("upgrade-button");
  test::OobeJS().ExpectDisabledPath(
      {"encryption-migration-element", "upgrade-button"});

  test::OobeJS().TapOnPath({"encryption-migration-element", "skip-button"});

  WaitForActiveSession();
  EXPECT_FALSE(FakeCryptohomeClient::Get()
                   ->get_id_for_disk_migrated_to_dircrypto()
                   .has_account_id());
}

IN_PROC_BROWSER_TEST_F(EncryptionMigrationTest,
                       CannotSkipWithLowBatteryOnMigrationResume) {
  SetBatteryPercent(5);
  SetUpEncryptionMigrationActionPolicy(
      arc::policy_util::EcryptfsMigrationAction::kMigrate);

  SetUpStubAuthenticatorAndAttemptLogin(true /* has_incomplete_migration */);
  OobeScreenWaiter(OobeScreen::SCREEN_ENCRYPTION_MIGRATION).Wait();

  WaitForElementCreation("ready-dialog");
  VerifyUiElementVisible("ready-dialog");

  VerifyUiElementNotVisible("migrating-dialog");
  VerifyUiElementNotVisible("insufficient-space-dialog");
  VerifyUiElementNotVisible("error-dialog");
  VerifyUiElementNotVisible("minimal-migartion-dialog");

  VerifyUiElementNotVisible("skip-button");
  VerifyUiElementNotVisible("upgrade-button");

  EXPECT_FALSE(FakeCryptohomeClient::Get()
                   ->get_id_for_disk_migrated_to_dircrypto()
                   .has_account_id());
}

IN_PROC_BROWSER_TEST_F(EncryptionMigrationTest,
                       StartMigrationWhenEnoughBattery) {
  SetBatteryPercent(5);
  SetUpEncryptionMigrationActionPolicy(
      arc::policy_util::EcryptfsMigrationAction::kMigrate);

  SetUpStubAuthenticatorAndAttemptLogin(false /* has_incomplete_migration */);
  OobeScreenWaiter(OobeScreen::SCREEN_ENCRYPTION_MIGRATION).Wait();

  WaitForElementCreation("ready-dialog");
  VerifyUiElementVisible("ready-dialog");

  VerifyUiElementNotVisible("migrating-dialog");
  VerifyUiElementNotVisible("insufficient-space-dialog");
  VerifyUiElementNotVisible("error-dialog");
  VerifyUiElementNotVisible("minimal-migartion-dialog");

  EXPECT_FALSE(FakeCryptohomeClient::Get()
                   ->get_id_for_disk_migrated_to_dircrypto()
                   .has_account_id());

  SetBatteryPercent(60);

  RunFullMigrationFlowTest();
}

}  // namespace chromeos
