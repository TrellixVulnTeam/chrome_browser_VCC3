// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package org.chromium.chrome.browser.contextualsearch;

import android.support.annotation.Nullable;

import java.net.URL;

/**
 * An interface for network communication between the Contextual Search client and server.
 */
interface ContextualSearchNetworkCommunicator {
    /**
     * Starts a Search Term Resolution request.
     * When the response comes back {@link #handleSearchTermResolutionResponse} will be called.
     * @param selection the current selected text.
     */
    void startSearchTermResolutionRequest(String selection);

    /**
     * Handles a Search Term Resolution response.
     * @param resolvedSearchTerm A {@link ResolvedSearchTerm} that encapsulates the response from
     *        the server.
     */
    void handleSearchTermResolutionResponse(ResolvedSearchTerm resolvedSearchTerm);

    /**
     * @return Whether the device is currently online.
     */
    boolean isOnline();

    /**
     * Stops any navigation in the overlay panel's {@code WebContents}.
     */
    void stopPanelContentsNavigation();

    // --------------------------------------------------------------------------------------------
    // These are non-network actions that need to be stubbed out for testing.
    // --------------------------------------------------------------------------------------------

    /**
     * Gets the URL of the base page.
     * TODO(donnd): move to another interface, or rename this interface:
     * This is needed to stub out for testing, but has nothing to do with networking.
     * @return The URL of the base page (needed for testing purposes).
     */
    @Nullable URL getBasePageUrl();
}
