// Copyright 2018 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package org.chromium.chrome.browser.keyboard_accessory.bar_component;

import static android.support.test.espresso.Espresso.onView;
import static android.support.test.espresso.action.ViewActions.click;
import static android.support.test.espresso.matcher.ViewMatchers.isRoot;
import static android.support.test.espresso.matcher.ViewMatchers.withText;

import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertNotEquals;
import static org.junit.Assert.assertNull;
import static org.junit.Assert.assertTrue;

import static org.chromium.chrome.browser.keyboard_accessory.AccessoryAction.AUTOFILL_SUGGESTION;
import static org.chromium.chrome.browser.keyboard_accessory.bar_component.KeyboardAccessoryProperties.BAR_ITEMS;
import static org.chromium.chrome.browser.keyboard_accessory.bar_component.KeyboardAccessoryProperties.KEYBOARD_TOGGLE_VISIBLE;
import static org.chromium.chrome.browser.keyboard_accessory.bar_component.KeyboardAccessoryProperties.SHEET_TITLE;
import static org.chromium.chrome.browser.keyboard_accessory.bar_component.KeyboardAccessoryProperties.TAB_LAYOUT_ITEM;
import static org.chromium.chrome.browser.keyboard_accessory.bar_component.KeyboardAccessoryProperties.VISIBLE;
import static org.chromium.chrome.test.util.ViewUtils.waitForView;

import android.content.pm.ActivityInfo;
import android.graphics.Rect;
import android.support.design.widget.TabLayout;
import android.support.test.filters.MediumTest;
import android.view.View;
import android.view.ViewGroup;
import android.view.ViewStub;

import org.junit.Before;
import org.junit.Rule;
import org.junit.Test;
import org.junit.runner.RunWith;

import org.chromium.base.Callback;
import org.chromium.base.test.util.CommandLineFlags;
import org.chromium.chrome.R;
import org.chromium.chrome.browser.ChromeFeatureList;
import org.chromium.chrome.browser.ChromeSwitches;
import org.chromium.chrome.browser.keyboard_accessory.bar_component.KeyboardAccessoryProperties.AutofillBarItem;
import org.chromium.chrome.browser.keyboard_accessory.bar_component.KeyboardAccessoryProperties.BarItem;
import org.chromium.chrome.browser.keyboard_accessory.bar_component.KeyboardAccessoryProperties.TabLayoutBarItem;
import org.chromium.chrome.browser.keyboard_accessory.data.KeyboardAccessoryData;
import org.chromium.chrome.browser.keyboard_accessory.data.KeyboardAccessoryData.Action;
import org.chromium.chrome.browser.keyboard_accessory.tab_layout_component.KeyboardAccessoryTabLayoutCoordinator;
import org.chromium.chrome.test.ChromeJUnit4ClassRunner;
import org.chromium.chrome.test.ChromeTabbedActivityTestRule;
import org.chromium.chrome.test.util.browser.Features.EnableFeatures;
import org.chromium.components.autofill.AutofillSuggestion;
import org.chromium.content_public.browser.test.util.Criteria;
import org.chromium.content_public.browser.test.util.CriteriaHelper;
import org.chromium.content_public.browser.test.util.JavaScriptUtils;
import org.chromium.content_public.browser.test.util.TestThreadUtils;
import org.chromium.ui.DeferredViewStubInflationProvider;
import org.chromium.ui.DropdownItem;
import org.chromium.ui.ViewProvider;
import org.chromium.ui.modelutil.LazyConstructionPropertyMcp;
import org.chromium.ui.modelutil.PropertyModel;

import java.util.concurrent.ArrayBlockingQueue;
import java.util.concurrent.BlockingQueue;
import java.util.concurrent.atomic.AtomicReference;

/**
 * View tests for the keyboard accessory component.
 *
 */
@RunWith(ChromeJUnit4ClassRunner.class)
@CommandLineFlags.Add({ChromeSwitches.DISABLE_FIRST_RUN_EXPERIENCE})
@EnableFeatures(ChromeFeatureList.AUTOFILL_KEYBOARD_ACCESSORY)
public class KeyboardAccessoryModernViewTest {
    private PropertyModel mModel;
    private BlockingQueue<KeyboardAccessoryModernView> mKeyboardAccessoryView;

    @Rule
    public ChromeTabbedActivityTestRule mActivityTestRule = new ChromeTabbedActivityTestRule();

    @Before
    public void setUp() throws InterruptedException {
        mActivityTestRule.startMainActivityOnBlankPage();
        TestThreadUtils.runOnUiThreadBlocking(() -> {
            mModel =
                    KeyboardAccessoryProperties.defaultModelBuilder()
                            .with(TAB_LAYOUT_ITEM,
                                    new TabLayoutBarItem(new KeyboardAccessoryTabLayoutCoordinator
                                                                 .TabLayoutCallbacks() {
                                                                     @Override
                                                                     public void onTabLayoutBound(
                                                                             TabLayout tabs) {}
                                                                     @Override
                                                                     public void onTabLayoutUnbound(
                                                                             TabLayout tabs) {}
                                                                 }))
                            .build();
            ViewStub viewStub =
                    mActivityTestRule.getActivity().findViewById(R.id.keyboard_accessory_stub);

            mKeyboardAccessoryView = new ArrayBlockingQueue<>(1);
            ViewProvider<KeyboardAccessoryModernView> provider =
                    new DeferredViewStubInflationProvider<>(viewStub);
            LazyConstructionPropertyMcp.create(
                    mModel, VISIBLE, provider, KeyboardAccessoryModernViewBinder::bind);
            provider.whenLoaded(mKeyboardAccessoryView::add);
        });
    }

    @Test
    @MediumTest
    public void testAccessoryVisibilityChangedByModel() throws InterruptedException {
        // Initially, there shouldn't be a view yet.
        assertNull(mKeyboardAccessoryView.poll());

        // After setting the visibility to true, the view should exist and be visible.
        TestThreadUtils.runOnUiThreadBlocking(() -> { mModel.set(VISIBLE, true); });
        KeyboardAccessoryModernView view = mKeyboardAccessoryView.take();
        assertEquals(view.getVisibility(), View.VISIBLE);

        // After hiding the view, the view should still exist but be invisible.
        TestThreadUtils.runOnUiThreadBlocking(() -> { mModel.set(VISIBLE, false); });
        assertNotEquals(view.getVisibility(), View.VISIBLE);
    }

    @Test
    @MediumTest
    public void testAddsClickableAutofillSuggestions() {
        AtomicReference<Boolean> clickRecorded = new AtomicReference<>();
        TestThreadUtils.runOnUiThreadBlocking(() -> {
            mModel.set(VISIBLE, true);
            mModel.get(BAR_ITEMS).set(
                    createAutofillChipAndTab("Johnathan", result -> clickRecorded.set(true)));
        });

        onView(isRoot()).check((root, e) -> waitForView((ViewGroup) root, withText("Johnathan")));
        onView(withText("Johnathan")).perform(click());

        assertTrue(clickRecorded.get());
    }

    @Test
    @MediumTest
    public void testUpdatesKeyPaddingAfterRotation() throws InterruptedException {
        TestThreadUtils.runOnUiThreadBlocking(() -> {
            mModel.set(VISIBLE, true);
            mModel.set(KEYBOARD_TOGGLE_VISIBLE, false);
            mModel.set(SHEET_TITLE, "Sheet title");
            mModel.get(BAR_ITEMS).set(createAutofillChipAndTab("John", null));
        });
        KeyboardAccessoryModernView view = mKeyboardAccessoryView.take();
        CriteriaHelper.pollUiThread(view.mBarItemsView::isShown);
        CriteriaHelper.pollUiThread(viewsAreRightAligned(view, view.mBarItemsView.getChildAt(1)));

        TestThreadUtils.runOnUiThreadBlocking(() -> mModel.set(KEYBOARD_TOGGLE_VISIBLE, true));
        CriteriaHelper.pollUiThread(() -> !view.mBarItemsView.isShown());

        rotateActivityToLandscape();
        TestThreadUtils.runOnUiThreadBlocking(() -> mModel.set(KEYBOARD_TOGGLE_VISIBLE, false));

        CriteriaHelper.pollUiThread(view.mBarItemsView::isShown);
        CriteriaHelper.pollUiThread(viewsAreRightAligned(view, view.mBarItemsView.getChildAt(1)));
    }

    private void rotateActivityToLandscape() {
        mActivityTestRule.getActivity().setRequestedOrientation(
                ActivityInfo.SCREEN_ORIENTATION_LANDSCAPE);
        CriteriaHelper.pollInstrumentationThread(Criteria.equals("\"landscape\"", () -> {
            return JavaScriptUtils.executeJavaScriptAndWaitForResult(
                    mActivityTestRule.getWebContents(), "screen.orientation.type.split('-')[0]");
        }));
    }

    private Criteria viewsAreRightAligned(View staticView, View changingView) {
        Rect accessoryViewRect = new Rect();
        staticView.getGlobalVisibleRect(accessoryViewRect);
        return Criteria.equals(accessoryViewRect.right, () -> {
            Rect keyItemRect = new Rect();
            changingView.getGlobalVisibleRect(keyItemRect);
            return keyItemRect.right;
        });
    }

    private BarItem[] createAutofillChipAndTab(String label, Callback<Action> chipCallback) {
        return new BarItem[] {
                new AutofillBarItem(new AutofillSuggestion(label, "Smith", DropdownItem.NO_ICON,
                                            false, 0, false, false, false),
                        new KeyboardAccessoryData.Action(
                                "Unused", AUTOFILL_SUGGESTION, chipCallback)),
                new TabLayoutBarItem(
                        new KeyboardAccessoryTabLayoutCoordinator.TabLayoutCallbacks() {
                            @Override
                            public void onTabLayoutBound(TabLayout tabs) {
                                if (tabs.getTabCount() > 0) return;
                                tabs.addTab(tabs.newTab()
                                                    .setIcon(R.drawable.ic_vpn_key_grey)
                                                    .setContentDescription("Key Icon"));
                            }

                            @Override
                            public void onTabLayoutUnbound(TabLayout tabs) {}
                        })};
    }
}