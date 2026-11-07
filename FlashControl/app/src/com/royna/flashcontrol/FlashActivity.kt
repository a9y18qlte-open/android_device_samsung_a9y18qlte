/*
 * SPDX-FileCopyrightText: The LineageOS Project
 * SPDX-License-Identifier: Apache-2.0
 */

package com.royna.flashcontrol

import android.os.Bundle
import com.android.settingslib.collapsingtoolbar.CollapsingToolbarBaseActivity

class FlashActivity : CollapsingToolbarBaseActivity() {
  private var mInstance: FlashFragment? = null

  override fun onCreate(savedInstanceState: Bundle?) {
    super.onCreate(savedInstanceState)
    if (mInstance == null) mInstance = FlashFragment()
    supportFragmentManager
      .beginTransaction()
      .replace(com.android.settingslib.collapsingtoolbar.R.id.content_frame, mInstance!!)
      .commit()
  }
}
