// Copyright 2018 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#import <EarlGrey/EarlGrey.h>
#import <Foundation/Foundation.h>
#import <XCTest/XCTest.h>

#import "base/strings/sys_string_conversions.h"
#include "ios/chrome/browser/chrome_url_constants.h"
#import "ios/chrome/test/app/chrome_test_util.h"
#import "ios/chrome/test/app/tab_test_util.h"
#import "ios/chrome/test/app/web_view_interaction_test_util.h"
#import "ios/chrome/test/earl_grey/chrome_actions.h"
#import "ios/chrome/test/earl_grey/chrome_earl_grey.h"
#import "ios/chrome/test/earl_grey/chrome_error_util.h"
#import "ios/chrome/test/earl_grey/chrome_matchers.h"
#import "ios/chrome/test/earl_grey/chrome_test_case.h"
#include "ios/web/public/test/element_selector.h"

#if !defined(__has_feature) || !__has_feature(objc_arc)
#error "This file requires ARC support."
#endif

using chrome_test_util::GetCurrentWebState;
using chrome_test_util::TapWebViewElementWithIdInIframe;

namespace {
// Directory containing the |kLogoPagePath| and |kLogoPageImageSourcePath|
// resources.
const char kServerFilesDir[] = "ios/testing/data/http_server_files/";

// Id of the "Start Logging" button.
NSString* const kStartLoggingButtonId = @"start-logging";
// Id of the "Stop Logging" button.
NSString* const kStopLoggingButtonId = @"stop-logging";

// Test page continaing buttons to test console logging.
const char kConsolePage[] = "/console_with_iframe.html";
// Id of the console page button to log a debug message.
NSString* const kDebugMessageButtonId = @"debug";
// Id of the console page button to log an error.
NSString* const kErrorMessageButtonId = @"error";
// Id of the console page button to log an info message.
NSString* const kInfoMessageButtonId = @"info";
// Id of the console page button to log a message.
NSString* const kLogMessageButtonId = @"log";
// Id of the console page button to log a warning.
NSString* const kWarningMessageButtonId = @"warn";

// Label for debug console messages.
const char kDebugMessageLabel[] = "DEBUG";
// Label for error console messages.
const char kErrorMessageLabel[] = "ERROR";
// Label for info console messages.
const char kInfoMessageLabel[] = "INFO";
// Label for log console messages.
const char kLogMessageLabel[] = "LOG";
// Label for warning console messages.
const char kWarningMessageLabel[] = "WARN";

// Text of the message emitted from the |kDebugMessageButtonId| button on
// |kConsolePage|.
const char kDebugMessageText[] = "This is a debug message.";
// Text of the message emitted from the |kErrorMessageButtonId| button on
// |kConsolePage|.
const char kErrorMessageText[] = "This is an error message.";
// Text of the message emitted from the |kInfoMessageButtonId| button on
// |kConsolePage|.
const char kInfoMessageText[] = "This is an informative message.";
// Text of the message emitted from the |kLogMessageButtonId| button on
// |kConsolePage|.
const char kLogMessageText[] = "This log is very round.";
// Text of the message emitted from the |kWarningMessageButtonId| button on
// |kConsolePage|.
const char kWarningMessageText[] = "This is a warning message.";

// Text of the message emitted from the |kDebugMessageButtonId| button within
// the iframe on |kConsolePage|.
const char kIFrameDebugMessageText[] = "This is an iframe debug message.";
// Text of the message emitted from the |kErrorMessageButtonId| button within
// the iframe on |kConsolePage|.
const char kIFrameErrorMessageText[] = "This is an iframe error message.";
// Text of the message emitted from the |kInfoMessageButtonId| button within the
// iframe on |kConsolePage|.
const char kIFrameInfoMessageText[] = "This is an iframe informative message.";
// Text of the message emitted from the |kLogMessageButtonId| button within the
// iframe on |kConsolePage|.
const char kIFrameLogMessageText[] = "This iframe log is very round.";
// Text of the message emitted from the |kWarningMessageButtonId| button within
// the iframe on |kConsolePage|.
const char kIFrameWarningMessageText[] = "This is an iframe warning message.";

ElementSelector* StartLoggingButton() {
  return [ElementSelector
      selectorWithElementID:base::SysNSStringToUTF8(kStartLoggingButtonId)];
}

}  // namespace

// Test case for chrome://inspect WebUI page.
@interface InspectUITestCase : ChromeTestCase
@end

@implementation InspectUITestCase

- (void)setUp {
  [super setUp];
  self.testServer->ServeFilesFromSourceDirectory(
      base::FilePath(kServerFilesDir));
  GREYAssertTrue(self.testServer->Start(), @"Server did not start.");
}

// Tests that chrome://inspect allows the user to enable and disable logging.
- (void)testStartStopLogging {
  CHROME_EG_ASSERT_NO_ERROR([ChromeEarlGrey loadURL:GURL(kChromeUIInspectURL)]);

  CHROME_EG_ASSERT_NO_ERROR(
      [ChromeEarlGrey waitForWebViewContainingElement:StartLoggingButton()]);

  CHROME_EG_ASSERT_NO_ERROR(
      [ChromeEarlGrey tapWebViewElementWithID:kStartLoggingButtonId]);

  ElementSelector* stopLoggingButton = [ElementSelector
      selectorWithElementID:base::SysNSStringToUTF8(kStopLoggingButtonId)];
  CHROME_EG_ASSERT_NO_ERROR(
      [ChromeEarlGrey waitForWebViewContainingElement:stopLoggingButton]);

  CHROME_EG_ASSERT_NO_ERROR(
      [ChromeEarlGrey tapWebViewElementWithID:kStopLoggingButtonId]);

  CHROME_EG_ASSERT_NO_ERROR(
      [ChromeEarlGrey waitForWebViewContainingElement:StartLoggingButton()]);
}

// Tests that log messages from a page's main frame are displayed.
- (void)testMainFrameLogging {
  CHROME_EG_ASSERT_NO_ERROR([ChromeEarlGrey loadURL:GURL(kChromeUIInspectURL)]);

  // Start logging.
  CHROME_EG_ASSERT_NO_ERROR(
      [ChromeEarlGrey waitForWebViewContainingElement:StartLoggingButton()]);
  CHROME_EG_ASSERT_NO_ERROR(
      [ChromeEarlGrey tapWebViewElementWithID:kStartLoggingButtonId]);

  // Open console test page.
  CHROME_EG_ASSERT_NO_ERROR([ChromeEarlGrey openNewTab]);
  const GURL consoleTestsURL = self.testServer->GetURL(kConsolePage);
  CHROME_EG_ASSERT_NO_ERROR([ChromeEarlGrey loadURL:consoleTestsURL]);
  std::string debugButtonID = base::SysNSStringToUTF8(kDebugMessageButtonId);
  CHROME_EG_ASSERT_NO_ERROR([ChromeEarlGrey
      waitForWebViewContainingElement:
          [ElementSelector selectorWithElementID:debugButtonID]]);

  // Log messages.
  CHROME_EG_ASSERT_NO_ERROR(
      [ChromeEarlGrey tapWebViewElementWithID:kDebugMessageButtonId]);
  CHROME_EG_ASSERT_NO_ERROR(
      [ChromeEarlGrey tapWebViewElementWithID:kErrorMessageButtonId]);
  CHROME_EG_ASSERT_NO_ERROR(
      [ChromeEarlGrey tapWebViewElementWithID:kInfoMessageButtonId]);
  CHROME_EG_ASSERT_NO_ERROR(
      [ChromeEarlGrey tapWebViewElementWithID:kLogMessageButtonId]);
  CHROME_EG_ASSERT_NO_ERROR(
      [ChromeEarlGrey tapWebViewElementWithID:kWarningMessageButtonId]);

  chrome_test_util::SelectTabAtIndexInCurrentMode(0);
  // Validate messages and labels are displayed.
  CHROME_EG_ASSERT_NO_ERROR(
      [ChromeEarlGrey waitForWebViewContainingText:kDebugMessageLabel]);
  CHROME_EG_ASSERT_NO_ERROR(
      [ChromeEarlGrey waitForWebViewContainingText:kDebugMessageText]);
  CHROME_EG_ASSERT_NO_ERROR(
      [ChromeEarlGrey waitForWebViewContainingText:kErrorMessageLabel]);
  CHROME_EG_ASSERT_NO_ERROR(
      [ChromeEarlGrey waitForWebViewContainingText:kErrorMessageText]);
  CHROME_EG_ASSERT_NO_ERROR(
      [ChromeEarlGrey waitForWebViewContainingText:kInfoMessageLabel]);
  CHROME_EG_ASSERT_NO_ERROR(
      [ChromeEarlGrey waitForWebViewContainingText:kInfoMessageText]);
  CHROME_EG_ASSERT_NO_ERROR(
      [ChromeEarlGrey waitForWebViewContainingText:kLogMessageLabel]);
  CHROME_EG_ASSERT_NO_ERROR(
      [ChromeEarlGrey waitForWebViewContainingText:kLogMessageText]);
  CHROME_EG_ASSERT_NO_ERROR(
      [ChromeEarlGrey waitForWebViewContainingText:kWarningMessageLabel]);
  CHROME_EG_ASSERT_NO_ERROR(
      [ChromeEarlGrey waitForWebViewContainingText:kWarningMessageText]);
}

// Tests that log messages from an iframe are displayed.
- (void)testIframeLogging {
  CHROME_EG_ASSERT_NO_ERROR([ChromeEarlGrey loadURL:GURL(kChromeUIInspectURL)]);

  // Start logging.
  CHROME_EG_ASSERT_NO_ERROR(
      [ChromeEarlGrey waitForWebViewContainingElement:StartLoggingButton()]);
  CHROME_EG_ASSERT_NO_ERROR(
      [ChromeEarlGrey tapWebViewElementWithID:kStartLoggingButtonId]);

  // Open console test page.
  CHROME_EG_ASSERT_NO_ERROR([ChromeEarlGrey openNewTab]);
  const GURL consoleTestsURL = self.testServer->GetURL(kConsolePage);
  CHROME_EG_ASSERT_NO_ERROR([ChromeEarlGrey loadURL:consoleTestsURL]);

  std::string debugButtonID = base::SysNSStringToUTF8(kDebugMessageButtonId);
  CHROME_EG_ASSERT_NO_ERROR([ChromeEarlGrey
      waitForWebViewContainingElement:
          [ElementSelector selectorWithElementID:debugButtonID]]);

  // Log messages.
  GREYAssertTrue(TapWebViewElementWithIdInIframe(debugButtonID),
                 @"Failed to tap debug button.");

  std::string errorButtonID = base::SysNSStringToUTF8(kErrorMessageButtonId);
  GREYAssertTrue(TapWebViewElementWithIdInIframe(errorButtonID),
                 @"Failed to tap error button.");

  std::string infoButtonID = base::SysNSStringToUTF8(kInfoMessageButtonId);
  GREYAssertTrue(TapWebViewElementWithIdInIframe(infoButtonID),
                 @"Failed to tap info button.");

  std::string logButtonID = base::SysNSStringToUTF8(kLogMessageButtonId);
  GREYAssertTrue(TapWebViewElementWithIdInIframe(logButtonID),
                 @"Failed to tap log button.");

  std::string warnButtonID = base::SysNSStringToUTF8(kWarningMessageButtonId);
  GREYAssertTrue(TapWebViewElementWithIdInIframe(warnButtonID),
                 @"Failed to tap warn button.");

  chrome_test_util::SelectTabAtIndexInCurrentMode(0);
  // Validate messages and labels are displayed.
  CHROME_EG_ASSERT_NO_ERROR(
      [ChromeEarlGrey waitForWebViewContainingText:kDebugMessageLabel]);
  CHROME_EG_ASSERT_NO_ERROR(
      [ChromeEarlGrey waitForWebViewContainingText:kIFrameDebugMessageText]);
  CHROME_EG_ASSERT_NO_ERROR(
      [ChromeEarlGrey waitForWebViewContainingText:kErrorMessageLabel]);
  CHROME_EG_ASSERT_NO_ERROR(
      [ChromeEarlGrey waitForWebViewContainingText:kIFrameErrorMessageText]);
  CHROME_EG_ASSERT_NO_ERROR(
      [ChromeEarlGrey waitForWebViewContainingText:kInfoMessageLabel]);
  CHROME_EG_ASSERT_NO_ERROR(
      [ChromeEarlGrey waitForWebViewContainingText:kIFrameInfoMessageText]);
  CHROME_EG_ASSERT_NO_ERROR(
      [ChromeEarlGrey waitForWebViewContainingText:kLogMessageLabel]);
  CHROME_EG_ASSERT_NO_ERROR(
      [ChromeEarlGrey waitForWebViewContainingText:kIFrameLogMessageText]);
  CHROME_EG_ASSERT_NO_ERROR(
      [ChromeEarlGrey waitForWebViewContainingText:kWarningMessageLabel]);
  CHROME_EG_ASSERT_NO_ERROR(
      [ChromeEarlGrey waitForWebViewContainingText:kIFrameWarningMessageText]);
}

// Tests that log messages are correctly displayed from multiple tabs.
- (void)testLoggingFromMultipleTabs {
  CHROME_EG_ASSERT_NO_ERROR([ChromeEarlGrey loadURL:GURL(kChromeUIInspectURL)]);

  // Start logging.
  CHROME_EG_ASSERT_NO_ERROR(
      [ChromeEarlGrey waitForWebViewContainingElement:StartLoggingButton()]);
  CHROME_EG_ASSERT_NO_ERROR(
      [ChromeEarlGrey tapWebViewElementWithID:kStartLoggingButtonId]);

  // Open console test page.
  CHROME_EG_ASSERT_NO_ERROR([ChromeEarlGrey openNewTab]);
  const GURL consoleTestsURL = self.testServer->GetURL(kConsolePage);
  CHROME_EG_ASSERT_NO_ERROR([ChromeEarlGrey loadURL:consoleTestsURL]);
  std::string logButtonID = base::SysNSStringToUTF8(kLogMessageButtonId);
  CHROME_EG_ASSERT_NO_ERROR([ChromeEarlGrey
      waitForWebViewContainingElement:[ElementSelector
                                          selectorWithElementID:logButtonID]]);

  // Log a message and verify it is displayed.
  CHROME_EG_ASSERT_NO_ERROR(
      [ChromeEarlGrey tapWebViewElementWithID:kDebugMessageButtonId]);
  chrome_test_util::SelectTabAtIndexInCurrentMode(0);
  CHROME_EG_ASSERT_NO_ERROR(
      [ChromeEarlGrey waitForWebViewContainingText:kDebugMessageLabel]);
  CHROME_EG_ASSERT_NO_ERROR(
      [ChromeEarlGrey waitForWebViewContainingText:kDebugMessageText]);

  // Open console test page again.
  CHROME_EG_ASSERT_NO_ERROR([ChromeEarlGrey openNewTab]);
  CHROME_EG_ASSERT_NO_ERROR([ChromeEarlGrey loadURL:consoleTestsURL]);
  CHROME_EG_ASSERT_NO_ERROR([ChromeEarlGrey
      waitForWebViewContainingElement:[ElementSelector
                                          selectorWithElementID:logButtonID]]);

  // Log another message and verify it is displayed.
  CHROME_EG_ASSERT_NO_ERROR(
      [ChromeEarlGrey tapWebViewElementWithID:kLogMessageButtonId]);
  chrome_test_util::SelectTabAtIndexInCurrentMode(0);
  CHROME_EG_ASSERT_NO_ERROR(
      [ChromeEarlGrey waitForWebViewContainingText:kLogMessageLabel]);
  CHROME_EG_ASSERT_NO_ERROR(
      [ChromeEarlGrey waitForWebViewContainingText:kLogMessageText]);

  // Ensure the log from the first tab still exists.
  CHROME_EG_ASSERT_NO_ERROR(
      [ChromeEarlGrey waitForWebViewContainingText:kDebugMessageLabel]);
  CHROME_EG_ASSERT_NO_ERROR(
      [ChromeEarlGrey waitForWebViewContainingText:kDebugMessageText]);
}

// Tests that messages are cleared after stopping logging.
- (void)testMessagesClearedOnStopLogging {
  CHROME_EG_ASSERT_NO_ERROR([ChromeEarlGrey loadURL:GURL(kChromeUIInspectURL)]);

  // Start logging.
  CHROME_EG_ASSERT_NO_ERROR(
      [ChromeEarlGrey waitForWebViewContainingElement:StartLoggingButton()]);
  CHROME_EG_ASSERT_NO_ERROR(
      [ChromeEarlGrey tapWebViewElementWithID:kStartLoggingButtonId]);

  // Open console test page.
  CHROME_EG_ASSERT_NO_ERROR([ChromeEarlGrey openNewTab]);
  const GURL consoleTestsURL = self.testServer->GetURL(kConsolePage);
  CHROME_EG_ASSERT_NO_ERROR([ChromeEarlGrey loadURL:consoleTestsURL]);
  std::string logButtonID = base::SysNSStringToUTF8(kLogMessageButtonId);
  CHROME_EG_ASSERT_NO_ERROR([ChromeEarlGrey
      waitForWebViewContainingElement:[ElementSelector
                                          selectorWithElementID:logButtonID]]);

  // Log a message and verify it is displayed.
  CHROME_EG_ASSERT_NO_ERROR(
      [ChromeEarlGrey tapWebViewElementWithID:kDebugMessageButtonId]);
  chrome_test_util::SelectTabAtIndexInCurrentMode(0);
  CHROME_EG_ASSERT_NO_ERROR(
      [ChromeEarlGrey waitForWebViewContainingText:kDebugMessageLabel]);
  CHROME_EG_ASSERT_NO_ERROR(
      [ChromeEarlGrey waitForWebViewContainingText:kDebugMessageText]);

  // Stop logging.
  CHROME_EG_ASSERT_NO_ERROR(
      [ChromeEarlGrey tapWebViewElementWithID:kStopLoggingButtonId]);
  // Ensure message was cleared.
  CHROME_EG_ASSERT_NO_ERROR(
      [ChromeEarlGrey waitForWebViewNotContainingText:kDebugMessageLabel]);
  CHROME_EG_ASSERT_NO_ERROR(
      [ChromeEarlGrey waitForWebViewNotContainingText:kDebugMessageText]);
}

// Tests that messages are cleared after a page reload.
- (void)testMessagesClearedOnReload {
  CHROME_EG_ASSERT_NO_ERROR([ChromeEarlGrey loadURL:GURL(kChromeUIInspectURL)]);

  // Start logging.
  CHROME_EG_ASSERT_NO_ERROR(
      [ChromeEarlGrey waitForWebViewContainingElement:StartLoggingButton()]);
  CHROME_EG_ASSERT_NO_ERROR(
      [ChromeEarlGrey tapWebViewElementWithID:kStartLoggingButtonId]);

  // Open console test page.
  CHROME_EG_ASSERT_NO_ERROR([ChromeEarlGrey openNewTab]);
  const GURL consoleTestsURL = self.testServer->GetURL(kConsolePage);
  CHROME_EG_ASSERT_NO_ERROR([ChromeEarlGrey loadURL:consoleTestsURL]);
  std::string logButtonID = base::SysNSStringToUTF8(kLogMessageButtonId);
  CHROME_EG_ASSERT_NO_ERROR([ChromeEarlGrey
      waitForWebViewContainingElement:[ElementSelector
                                          selectorWithElementID:logButtonID]]);

  // Log a message and verify it is displayed.
  CHROME_EG_ASSERT_NO_ERROR(
      [ChromeEarlGrey tapWebViewElementWithID:kDebugMessageButtonId]);
  chrome_test_util::SelectTabAtIndexInCurrentMode(0);
  CHROME_EG_ASSERT_NO_ERROR(
      [ChromeEarlGrey waitForWebViewContainingText:kDebugMessageLabel]);
  CHROME_EG_ASSERT_NO_ERROR(
      [ChromeEarlGrey waitForWebViewContainingText:kDebugMessageText]);

  // Reload page.
  CHROME_EG_ASSERT_NO_ERROR([ChromeEarlGrey reload]);
  // Ensure message was cleared.
  CHROME_EG_ASSERT_NO_ERROR(
      [ChromeEarlGrey waitForWebViewNotContainingText:kDebugMessageLabel]);
  CHROME_EG_ASSERT_NO_ERROR(
      [ChromeEarlGrey waitForWebViewNotContainingText:kDebugMessageText]);
}

// Tests that messages are cleared for a tab which is closed.
- (void)testMessagesClearedOnTabClosure {
  CHROME_EG_ASSERT_NO_ERROR([ChromeEarlGrey loadURL:GURL(kChromeUIInspectURL)]);

  // Start logging.
  CHROME_EG_ASSERT_NO_ERROR(
      [ChromeEarlGrey waitForWebViewContainingElement:StartLoggingButton()]);
  CHROME_EG_ASSERT_NO_ERROR(
      [ChromeEarlGrey tapWebViewElementWithID:kStartLoggingButtonId]);

  // Open console test page.
  CHROME_EG_ASSERT_NO_ERROR([ChromeEarlGrey openNewTab]);
  const GURL consoleTestsURL = self.testServer->GetURL(kConsolePage);
  CHROME_EG_ASSERT_NO_ERROR([ChromeEarlGrey loadURL:consoleTestsURL]);
  std::string debugButtonID = base::SysNSStringToUTF8(kDebugMessageButtonId);
  CHROME_EG_ASSERT_NO_ERROR([ChromeEarlGrey
      waitForWebViewContainingElement:
          [ElementSelector selectorWithElementID:debugButtonID]]);

  CHROME_EG_ASSERT_NO_ERROR(
      [ChromeEarlGrey tapWebViewElementWithID:kDebugMessageButtonId]);
  [ChromeEarlGrey closeCurrentTab];

  // Validate message and label are not displayed.
  CHROME_EG_ASSERT_NO_ERROR(
      [ChromeEarlGrey waitForWebViewNotContainingText:kDebugMessageLabel]);
  CHROME_EG_ASSERT_NO_ERROR(
      [ChromeEarlGrey waitForWebViewNotContainingText:kDebugMessageText]);
}

@end
