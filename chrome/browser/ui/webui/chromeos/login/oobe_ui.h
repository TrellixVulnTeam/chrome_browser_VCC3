// Copyright (c) 2011 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_UI_WEBUI_CHROMEOS_LOGIN_OOBE_UI_H_
#define CHROME_BROWSER_UI_WEBUI_CHROMEOS_LOGIN_OOBE_UI_H_

#include <map>
#include <memory>
#include <string>
#include <vector>

#include "base/compiler_specific.h"
#include "base/macros.h"
#include "base/memory/ref_counted.h"
#include "base/observer_list.h"
#include "chrome/browser/chromeos/login/oobe_screen.h"
#include "chrome/browser/ui/webui/chromeos/login/base_screen_handler.h"
#include "chrome/browser/ui/webui/chromeos/login/core_oobe_handler.h"
#include "chromeos/services/multidevice_setup/public/mojom/multidevice_setup.mojom.h"
#include "ui/webui/mojo_web_ui_controller.h"

namespace base {
class DictionaryValue;
}  // namespace base

namespace service_manager {
class Connector;
}  // namespace service_manager

namespace chromeos {
class AppDownloadingScreenView;
class AppLaunchSplashScreenView;
class ArcKioskSplashScreenView;
class ArcTermsOfServiceScreenView;
class AssistantOptInFlowScreenView;
class AutoEnrollmentCheckScreenView;
class BaseScreenHandler;
class CoreOobeView;
class DemoPreferencesScreenView;
class DemoSetupScreenView;
class DeviceDisabledScreenView;
class EnableDebuggingScreenView;
class EncryptionMigrationScreenView;
class EnrollmentScreenView;
class EulaView;
class ErrorScreen;
class DiscoverScreenView;
class FingerprintSetupScreenView;
class HIDDetectionView;
class KioskAutolaunchScreenView;
class KioskEnableScreenView;
class LoginScreenContext;
class MarketingOptInScreenView;
class MultiDeviceSetupScreenView;
class NativeWindowDelegate;
class NetworkScreenView;
class NetworkStateInformer;
class OobeDisplayChooser;
class RecommendAppsScreenView;
class ResetView;
class SigninScreenHandler;
class SigninScreenHandlerDelegate;
class SyncConsentScreenView;
class TermsOfServiceScreenView;
class UpdateView;
class UpdateRequiredView;
class SupervisionTransitionScreenView;
class WelcomeView;
class WrongHWIDScreenView;

// A custom WebUI that defines datasource for out-of-box-experience (OOBE) UI:
// - welcome screen (setup language/keyboard/network).
// - eula screen (CrOS (+ OEM) EULA content/TPM password/crash reporting).
// - update screen.
class OobeUI : public ui::MojoWebUIController {
 public:
  // List of known types of OobeUI. Type added as path in chrome://oobe url, for
  // example chrome://oobe/user-adding.
  static const char kAppLaunchSplashDisplay[];
  static const char kArcKioskSplashDisplay[];
  static const char kDiscoverDisplay[];
  static const char kGaiaSigninDisplay[];
  static const char kLockDisplay[];
  static const char kLoginDisplay[];
  static const char kOobeDisplay[];
  static const char kUserAddingDisplay[];

  class Observer {
   public:
    Observer() {}
    virtual void OnCurrentScreenChanged(OobeScreen current_screen,
                                        OobeScreen new_screen) = 0;

    virtual void OnDestroyingOobeUI() = 0;

   protected:
    virtual ~Observer() {}
    DISALLOW_COPY(Observer);
  };

  OobeUI(content::WebUI* web_ui, const GURL& url);
  ~OobeUI() override;

  CoreOobeView* GetCoreOobeView();
  WelcomeView* GetWelcomeView();
  EulaView* GetEulaView();
  UpdateView* GetUpdateView();
  EnableDebuggingScreenView* GetEnableDebuggingScreenView();
  EnrollmentScreenView* GetEnrollmentScreenView();
  ResetView* GetResetView();
  DemoSetupScreenView* GetDemoSetupScreenView();
  DemoPreferencesScreenView* GetDemoPreferencesScreenView();
  FingerprintSetupScreenView* GetFingerprintSetupScreenView();
  KioskAutolaunchScreenView* GetKioskAutolaunchScreenView();
  KioskEnableScreenView* GetKioskEnableScreenView();
  TermsOfServiceScreenView* GetTermsOfServiceScreenView();
  SyncConsentScreenView* GetSyncConsentScreenView();
  ArcTermsOfServiceScreenView* GetArcTermsOfServiceScreenView();
  RecommendAppsScreenView* GetRecommendAppsScreenView();
  AppDownloadingScreenView* GetAppDownloadingScreenView();
  ErrorScreen* GetErrorScreen();
  WrongHWIDScreenView* GetWrongHWIDScreenView();
  AutoEnrollmentCheckScreenView* GetAutoEnrollmentCheckScreenView();
  AppLaunchSplashScreenView* GetAppLaunchSplashScreenView();
  ArcKioskSplashScreenView* GetArcKioskSplashScreenView();
  HIDDetectionView* GetHIDDetectionView();
  DeviceDisabledScreenView* GetDeviceDisabledScreenView();
  EncryptionMigrationScreenView* GetEncryptionMigrationScreenView();
  SupervisionTransitionScreenView* GetSupervisionTransitionScreenView();
  UpdateRequiredView* GetUpdateRequiredScreenView();
  AssistantOptInFlowScreenView* GetAssistantOptInFlowScreenView();
  MultiDeviceSetupScreenView* GetMultiDeviceSetupScreenView();
  DiscoverScreenView* GetDiscoverScreenView();
  NetworkScreenView* GetNetworkScreenView();
  MarketingOptInScreenView* GetMarketingOptInScreenView();

  // Collects localized strings from the owned handlers.
  void GetLocalizedStrings(base::DictionaryValue* localized_strings);

  // Initializes the handlers.
  void InitializeHandlers();

  // Called when the screen has changed.
  void CurrentScreenChanged(OobeScreen screen);

  bool IsScreenInitialized(OobeScreen screen);

  bool IsJSReady(const base::Closure& display_is_ready_callback);

  // Shows or hides OOBE UI elements.
  void ShowOobeUI(bool show);

  // Shows the signin screen.
  void ShowSigninScreen(const LoginScreenContext& context,
                        SigninScreenHandlerDelegate* delegate,
                        NativeWindowDelegate* native_window_delegate);

  // Forwards an accelerator to the webui to be handled.
  void ForwardAccelerator(std::string accelerator_name);

  // Resets the delegate set in ShowSigninScreen.
  void ResetSigninScreenHandlerDelegate();

  // Add and remove observers for screen change events.
  void AddObserver(Observer* observer);
  void RemoveObserver(Observer* observer);

  OobeScreen current_screen() const { return current_screen_; }

  OobeScreen previous_screen() const { return previous_screen_; }

  const std::string& display_type() const { return display_type_; }

  SigninScreenHandler* signin_screen_handler() {
    return signin_screen_handler_;
  }

  NetworkStateInformer* network_state_informer_for_test() const {
    return network_state_informer_.get();
  }

  // Re-evaluate OOBE display placement.
  void OnDisplayConfigurationChanged();

  // Notify WebUI of the user count on the views login screen.
  void SetLoginUserCount(int user_count);

  // Find a *View instance provided by a given *Handler type.
  //
  // This is the same as GetHandler() except the return type is limited to the
  // view.
  template <typename THandler>
  typename THandler::TView* GetView() {
    return GetHandler<THandler>();
  }

 private:
  // Find a handler instance.
  template <typename THandler>
  THandler* GetHandler() {
    OobeScreen expected_screen = THandler::kScreenId;
    for (BaseScreenHandler* handler : screen_handlers_) {
      if (expected_screen == handler->oobe_screen())
        return static_cast<THandler*>(handler);
    }

    NOTREACHED() << "Unable to find handler for screen "
                 << GetOobeScreenName(expected_screen);
    return nullptr;
  }

  void AddWebUIHandler(std::unique_ptr<BaseWebUIHandler> handler);
  void AddScreenHandler(std::unique_ptr<BaseScreenHandler> handler);

  // Configures all the relevant screen shandlers and resources for OOBE/Login
  // display type.
  void ConfigureOobeDisplay();

  // Adds Mojo bindings for this WebUIController.
  service_manager::Connector* GetLoggedInUserMojoConnector();
  void BindMultiDeviceSetup(
      multidevice_setup::mojom::MultiDeviceSetupRequest request);
  void BindPrivilegedHostDeviceSetter(
      multidevice_setup::mojom::PrivilegedHostDeviceSetterRequest request);

  // Type of UI.
  std::string display_type_;

  // Reference to NetworkStateInformer that handles changes in network
  // state.
  scoped_refptr<NetworkStateInformer> network_state_informer_;

  // Reference to CoreOobeHandler that handles common requests of Oobe page.
  CoreOobeHandler* core_handler_ = nullptr;

  // Reference to SigninScreenHandler that handles sign-in screen requests and
  // forwards calls from native code to JS side.
  SigninScreenHandler* signin_screen_handler_ = nullptr;

  std::vector<BaseWebUIHandler*> webui_handlers_;       // Non-owning pointers.
  std::vector<BaseWebUIHandler*> webui_only_handlers_;  // Non-owning pointers.
  std::vector<BaseScreenHandler*> screen_handlers_;     // Non-owning pointers.

  std::unique_ptr<ErrorScreen> error_screen_;

  // Id of the current oobe/login screen.
  OobeScreen current_screen_ = OobeScreen::SCREEN_UNKNOWN;

  // Id of the previous oobe/login screen.
  OobeScreen previous_screen_ = OobeScreen::SCREEN_UNKNOWN;

  // Flag that indicates whether JS part is fully loaded and ready to accept
  // calls.
  bool ready_ = false;

  // Callbacks to notify when JS part is fully loaded and ready to accept calls.
  std::vector<base::Closure> ready_callbacks_;

  // List of registered observers.
  base::ObserverList<Observer>::Unchecked observer_list_;

  std::unique_ptr<OobeDisplayChooser> oobe_display_chooser_;

  // Store the deferred JS calls before the screen handler instance is
  // initialized.
  std::unique_ptr<JSCallsContainer> js_calls_container_;

  DISALLOW_COPY_AND_ASSIGN(OobeUI);
};

}  // namespace chromeos

#endif  // CHROME_BROWSER_UI_WEBUI_CHROMEOS_LOGIN_OOBE_UI_H_
