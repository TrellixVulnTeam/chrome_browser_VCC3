// Copyright 2019 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package org.chromium.chrome.browser.autofill_assistant.infobox;

import android.content.Context;
import android.graphics.drawable.BitmapDrawable;
import android.graphics.drawable.Drawable;
import android.support.v7.content.res.AppCompatResources;
import android.view.View;
import android.widget.TextView;

import org.chromium.chrome.autofill_assistant.R;
import org.chromium.chrome.browser.image_fetcher.ImageFetcher;
import org.chromium.chrome.browser.image_fetcher.ImageFetcherConfig;
import org.chromium.chrome.browser.image_fetcher.ImageFetcherFactory;
import org.chromium.ui.modelutil.PropertyKey;
import org.chromium.ui.modelutil.PropertyModelChangeProcessor;

/**
 * This class is responsible for pushing updates to the Autofill Assistant info box view. These
 * updates are pulled from the {@link AssistantInfoBoxModel} when a notification of an update is
 * received.
 */
class AssistantInfoBoxViewBinder
        implements PropertyModelChangeProcessor.ViewBinder<AssistantInfoBoxModel,
                AssistantInfoBoxViewBinder.ViewHolder, PropertyKey> {
    /**
     * A wrapper class that holds the different views of the info box.
     */
    static class ViewHolder {
        final TextView mExplanationView;

        public ViewHolder(Context context, View infoBoxView) {
            mExplanationView = infoBoxView.findViewById(R.id.info_box_explanation);
        }
    }

    private final Context mContext;
    private ImageFetcher mImageFetcher;

    AssistantInfoBoxViewBinder(Context context) {
        mContext = context;
        mImageFetcher = ImageFetcherFactory.createImageFetcher(ImageFetcherConfig.DISK_CACHE_ONLY);
    }

    /**
     * Explicitly clean up.
     */
    public void destroy() {
        mImageFetcher.destroy();
        mImageFetcher = null;
    }

    @Override
    public void bind(AssistantInfoBoxModel model, ViewHolder view, PropertyKey propertyKey) {
        if (AssistantInfoBoxModel.INFO_BOX == propertyKey) {
            AssistantInfoBox infoBox = model.get(AssistantInfoBoxModel.INFO_BOX);
            if (infoBox == null) {
                // Handled by the AssistantInfoBoxCoordinator.
                return;
            }

            setInfoBox(infoBox, view);
        } else {
            assert false : "Unhandled property detected in AssistantInfoBoxViewBinder!";
        }
    }

    private void setInfoBox(AssistantInfoBox infoBox, ViewHolder viewHolder) {
        viewHolder.mExplanationView.setText(infoBox.getExplanation());
        if (infoBox.getImagePath().isEmpty()) {
            viewHolder.mExplanationView.setCompoundDrawablesWithIntrinsicBounds(null,
                    AppCompatResources.getDrawable(mContext, R.drawable.ic_tick_outline_48dp), null,
                    null);
        } else {
            mImageFetcher.fetchImage(infoBox.getImagePath(),
                    ImageFetcher.ASSISTANT_INFO_BOX_UMA_CLIENT_NAME, image -> {
                        if (image != null) {
                            Drawable d = new BitmapDrawable(mContext.getResources(), image);
                            viewHolder.mExplanationView.setCompoundDrawablesWithIntrinsicBounds(
                                    null, d, null, null);
                        }
                    });
        }
    }
}
