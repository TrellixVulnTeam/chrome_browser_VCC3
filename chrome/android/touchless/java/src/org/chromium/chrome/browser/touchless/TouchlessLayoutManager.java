// Copyright 2019 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package org.chromium.chrome.browser.touchless;

import android.content.Context;
import android.support.v7.widget.LinearLayoutManager;
import android.support.v7.widget.RecyclerView;
import android.view.View;

import org.chromium.base.Callback;

/**
 * Normally views can simply call {@link View#requestFocus()}. However, this turns out to be
 * problematic during the measure/layout of the {@link RecyclerView}. This class tracks the layout
 * state of the RecyclerView and decides the best way to focus the given View.
 */
public class TouchlessLayoutManager extends LinearLayoutManager {
    private View mViewToFocus;
    private boolean mMeasureLayoutInProgress;

    public TouchlessLayoutManager(Context context) {
        super(context);
    }

    @Override
    public void onMeasure(RecyclerView.Recycler recycler, RecyclerView.State state, int widthSpec,
            int heightSpec) {
        mMeasureLayoutInProgress = true;
        super.onMeasure(recycler, state, widthSpec, heightSpec);
    }

    @Override
    public void onLayoutCompleted(RecyclerView.State state) {
        super.onLayoutCompleted(state);
        if (mViewToFocus != null) {
            mViewToFocus.requestFocus();
            mViewToFocus = null;
        }
        mMeasureLayoutInProgress = false;
    }

    /**
     * Focuses the view, either right now if we're not in the middle of layout/measure, or
     * otherwise at the end of layout.
     * @param view The view to focus.
     */
    public void setViewToFocus(View view) {
        if (mMeasureLayoutInProgress) {
            mViewToFocus = view;
        } else {
            view.requestFocus();
        }
    }

    public Callback<View> createCallbackToSetViewToFocus() {
        return (View view) -> setViewToFocus(view);
    }

    @Override
    public void scrollToPositionWithOffset(int index, int offset) {
        super.scrollToPositionWithOffset(index, offset);
    }
}