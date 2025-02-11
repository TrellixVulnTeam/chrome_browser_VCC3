// Copyright 2019 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/chromeos/login/test/login_manager_mixin.h"

#include <memory>
#include <string>
#include <utility>

#include "base/command_line.h"
#include "chrome/browser/browser_process.h"
#include "chrome/browser/chrome_browser_main.h"
#include "chrome/browser/chrome_browser_main_extra_parts.h"
#include "chrome/browser/chromeos/login/existing_user_controller.h"
#include "chrome/browser/chromeos/login/session/user_session_manager.h"
#include "chrome/browser/chromeos/login/session/user_session_manager_test_api.h"
#include "chrome/browser/chromeos/login/signin_specifics.h"
#include "chrome/browser/chromeos/login/startup_utils.h"
#include "chromeos/constants/chromeos_switches.h"
#include "chromeos/login/auth/key.h"
#include "chromeos/login/auth/stub_authenticator_builder.h"
#include "chromeos/login/auth/user_context.h"
#include "components/prefs/pref_service.h"
#include "components/prefs/scoped_user_pref_update.h"
#include "components/session_manager/core/session_manager.h"
#include "components/session_manager/core/session_manager_observer.h"
#include "components/user_manager/fake_user_manager.h"
#include "components/user_manager/known_user.h"
#include "components/user_manager/scoped_user_manager.h"

namespace chromeos {

namespace {

// Chrome main extra part used for login manager tests to set up initially
// registered users. The main part injects itself into browser startup after
// local state has been set up, but before the user manager instance is created.
// It adds list of "known" users to the local state, so user manager can load
// them during its initialization.
// When users are registered, it marks OOBE as complete.
class TestUserRegistrationMainExtra : public ChromeBrowserMainExtraParts {
 public:
  explicit TestUserRegistrationMainExtra(
      const std::vector<LoginManagerMixin::TestUserInfo>& users)
      : users_(users) {}
  ~TestUserRegistrationMainExtra() override = default;

  // ChromeBrowserMainExtraParts:
  void PostEarlyInitialization() override {
    // SaveKnownUser depends on UserManager to get the local state that has to
    // be updated, and do ephemeral user checks.
    // Given that user manager does not exist yet (by design), create a
    // temporary fake user manager instance.
    {
      auto user_manager = std::make_unique<user_manager::FakeUserManager>();
      user_manager->set_local_state(g_browser_process->local_state());
      user_manager::ScopedUserManager scoper(std::move(user_manager));
      for (const auto& user : users_) {
        ListPrefUpdate users_pref(g_browser_process->local_state(),
                                  "LoggedInUsers");
        users_pref->AppendIfNotPresent(
            std::make_unique<base::Value>(user.account_id.GetUserEmail()));

        DictionaryPrefUpdate user_type_update(g_browser_process->local_state(),
                                              "UserType");
        user_type_update->SetKey(user.account_id.GetAccountIdKey(),
                                 base::Value(static_cast<int>(user.user_type)));

        user_manager::known_user::UpdateId(user.account_id);
      }
    }

    StartupUtils::MarkOobeCompleted();
  }

 private:
  const std::vector<LoginManagerMixin::TestUserInfo> users_;

  DISALLOW_COPY_AND_ASSIGN(TestUserRegistrationMainExtra);
};

// Used to wait for session manager to get into a specific session state.
class SessionStateWaiter : public session_manager::SessionManagerObserver {
 public:
  explicit SessionStateWaiter(session_manager::SessionState target_state)
      : target_state_(target_state) {}
  ~SessionStateWaiter() override = default;

  void Wait() {
    if (session_manager::SessionManager::Get()->session_state() ==
        target_state_) {
      return;
    }
    session_observer_.Add(session_manager::SessionManager::Get());

    base::RunLoop run_loop;
    session_state_callback_ = run_loop.QuitClosure();
    run_loop.Run();

    session_observer_.RemoveAll();
  }

  // session_manager::SessionManagerObserver:
  void OnSessionStateChanged() override {
    if (session_manager::SessionManager::Get()->session_state() ==
            target_state_ &&
        session_state_callback_) {
      std::move(session_state_callback_).Run();
    }
  }

 private:
  session_manager::SessionState target_state_;
  base::OnceClosure session_state_callback_;
  ScopedObserver<session_manager::SessionManager, SessionStateWaiter>
      session_observer_{this};

  DISALLOW_COPY_AND_ASSIGN(SessionStateWaiter);
};

}  // namespace

// static
UserContext LoginManagerMixin::CreateDefaultUserContext(
    const TestUserInfo& user_info) {
  UserContext user_context(user_info.user_type, user_info.account_id);
  user_context.SetKey(Key("password"));
  return user_context;
}

LoginManagerMixin::LoginManagerMixin(
    InProcessBrowserTestMixinHost* host,
    const std::vector<TestUserInfo>& initial_users)
    : InProcessBrowserTestMixin(host), initial_users_(initial_users) {}

LoginManagerMixin::~LoginManagerMixin() = default;

void LoginManagerMixin::SetUpCommandLine(base::CommandLine* command_line) {
  if (skip_flags_setup_)
    return;

  command_line->AppendSwitch(chromeos::switches::kLoginManager);
  command_line->AppendSwitch(chromeos::switches::kForceLoginManagerInTests);
}

void LoginManagerMixin::CreatedBrowserMainParts(
    content::BrowserMainParts* browser_main_parts) {
  // |browser_main_parts| take ownership of TestUserRegistrationMainExtra.
  static_cast<ChromeBrowserMainParts*>(browser_main_parts)
      ->AddParts(new TestUserRegistrationMainExtra(initial_users_));
}

void LoginManagerMixin::SetUpOnMainThread() {
  test::UserSessionManagerTestApi session_manager_test_api(
      UserSessionManager::GetInstance());
  session_manager_test_api.SetShouldLaunchBrowserInTests(false);
  session_manager_test_api.SetShouldObtainTokenHandleInTests(false);
}

void LoginManagerMixin::AttemptLoginUsingAuthenticator(
    const UserContext& user_context,
    std::unique_ptr<StubAuthenticatorBuilder> authenticator_builder) {
  test::UserSessionManagerTestApi(UserSessionManager::GetInstance())
      .InjectAuthenticatorBuilder(std::move(authenticator_builder));
  ExistingUserController::current_controller()->Login(user_context,
                                                      SigninSpecifics());
}

void LoginManagerMixin::WaitForActiveSession() {
  SessionStateWaiter(session_manager::SessionState::ACTIVE).Wait();
}

bool LoginManagerMixin::LoginAndWaitForActiveSession(
    const UserContext& user_context) {
  AttemptLoginUsingAuthenticator(
      user_context, std::make_unique<StubAuthenticatorBuilder>(user_context));
  WaitForActiveSession();

  user_manager::User* active_user =
      user_manager::UserManager::Get()->GetActiveUser();
  return active_user &&
         active_user->GetAccountId() == user_context.GetAccountId();
}

}  // namespace chromeos
