/*
 * Copyright (C) 2022 The LineageOS Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

package com.royna.flashcontrol

import android.content.SharedPreferences
import android.hardware.camera2.CameraAccessException
import android.hardware.camera2.CameraManager
import android.os.Bundle
import android.os.Handler
import android.os.Looper
import android.os.ServiceManager
import android.os.SystemProperties
import android.util.Log
import android.widget.CompoundButton
import android.widget.CompoundButton.OnCheckedChangeListener
import androidx.preference.Preference
import androidx.preference.PreferenceFragmentCompat
import androidx.preference.PreferenceManager
import com.android.settingslib.widget.MainSwitchPreference
import com.android.settingslib.widget.SelectorWithWidgetPreference
import vendor.samsung_ext.hardware.camera.flashlight.IFlashlight

/*
 * The torch is switched through the camera service, like the Quick Settings
 * flashlight tile, so the tile and this switch always show the same state.
 * The flashlight HAL only sets the brightness; FlashService applies the saved
 * level whenever the torch turns on.
 */
class FlashFragment : PreferenceFragmentCompat(), OnCheckedChangeListener {

  private lateinit var switchBar: MainSwitchPreference
  private val mService: IFlashlight? =
    IFlashlight.Stub.asInterface(
      ServiceManager.waitForDeclaredService(
        "vendor.samsung_ext.hardware.camera.flashlight.IFlashlight/default"
      )
    )
  private lateinit var mSharedPreferences: SharedPreferences
  private lateinit var mCurrentIntensity: Preference
  private lateinit var mCurrentOn: Preference
  private lateinit var mCameraManager: CameraManager
  private val mMainHandler = Handler(Looper.getMainLooper())

  private var mTorchOn = false
  private var mTorchAvailable = true
  // Set while the switch is updated from a torch callback, so that the change
  // is not sent back to the camera service.
  private var mUpdatingSwitch = false

  override fun onCreatePreferences(savedInstanceState: Bundle?, rootKey: String?) {
    addPreferencesFromResource(R.xml.flash_settings)

    mSharedPreferences = PreferenceManager.getDefaultSharedPreferences(requireContext())
    mCameraManager = requireContext().getSystemService(CameraManager::class.java)

    switchBar = findPreference<MainSwitchPreference>(PREF_FLASH_ENABLE)!!
    switchBar.addOnSwitchChangeListener(this)

    val mSavedIntensity = getSavedIntensity()
    for ((key, value) in PREF_FLASH_MODES) {
      val preference = findPreference<SelectorWithWidgetPreference>(key)!!
      preference.isChecked = value == mSavedIntensity
      preference.setOnPreferenceClickListener {
        setIntensity(value)
        true
      }
    }
    mCurrentOn = findPreference<Preference>(PREF_FLASH_CURRENT_ON)!!
    mCurrentIntensity = findPreference<Preference>(PREF_FLASH_CURRENT_INTENSITY)!!
    changeIntensityView(mSavedIntensity)
    updateTorchViews()
  }

  private fun getSavedIntensity() = mSharedPreferences.getInt(PREF_FLASH_INTENSITY, 1)

  private fun changeIntensityView(b: Int) {
    mCurrentIntensity.title =
      String.format(requireContext().getString(R.string.flash_current_intensity), b)
  }

  private fun changeOnOffView(b: Boolean) {
    mCurrentOn.title =
      String.format(
        requireContext().getString(R.string.flash_current_on),
        requireContext().getString(if (b) R.string.on else R.string.off),
      )
  }

  private fun updateTorchViews() {
    mUpdatingSwitch = true
    switchBar.setChecked(mTorchOn)
    mUpdatingSwitch = false
    switchBar.isEnabled = mTorchAvailable
    changeOnOffView(mTorchOn)
  }

  private val mTorchCallback =
    object : CameraManager.TorchCallback() {
      override fun onTorchModeChanged(cameraId: String, enabled: Boolean) {
        if (cameraId != REAR_CAMERA_ID) return
        mTorchOn = enabled
        mTorchAvailable = true
        if (context != null) updateTorchViews()
      }

      override fun onTorchModeUnavailable(cameraId: String) {
        if (cameraId != REAR_CAMERA_ID) return
        // A camera app holds the camera.
        mTorchOn = false
        mTorchAvailable = false
        if (context != null) updateTorchViews()
      }
    }

  override fun onResume() {
    super.onResume()
    mCameraManager.registerTorchCallback(mTorchCallback, mMainHandler)
  }

  override fun onPause() {
    super.onPause()
    mCameraManager.unregisterTorchCallback(mTorchCallback)
  }

  override fun onCheckedChanged(buttonView: CompoundButton, isChecked: Boolean) {
    if (mUpdatingSwitch) return
    try {
      mCameraManager.setTorchMode(REAR_CAMERA_ID, isChecked)
    } catch (e: CameraAccessException) {
      Log.w(TAG, "setTorchMode($isChecked) failed", e)
      updateTorchViews()
    } catch (e: IllegalArgumentException) {
      Log.w(TAG, "setTorchMode($isChecked) failed", e)
      updateTorchViews()
    }
  }

  private fun setIntensity(intensity: Int) {
    if (intensity < 1 || intensity > 5) {
      Log.e(TAG, "Invalid intensity $intensity")
      return
    }
    for ((key, value) in PREF_FLASH_MODES) {
      val preference = findPreference<SelectorWithWidgetPreference>(key)!!
      preference.isChecked = value == intensity
    }
    mSharedPreferences.edit().putInt(PREF_FLASH_INTENSITY, intensity).apply()
    changeIntensityView(intensity)
    // Writing a level switches the torch on, so only apply it while it is on.
    // Otherwise save it for the camera HAL, which turns the torch on at it.
    if (mTorchOn) mService?.setBrightness(intensity)
    else SystemProperties.set(TORCH_LEVEL_PROP, intensity.toString())
  }

  companion object {
    private const val PREF_FLASH_ENABLE = "flash_enable"
    const val PREF_FLASH_INTENSITY = "flash_intensity"
    private const val PREF_FLASH_CURRENT_ON = "flash_current_on"
    private const val PREF_FLASH_CURRENT_INTENSITY = "flash_current_intensity"
    val PREF_FLASH_MODES =
      mapOf(
        "flash_intensity_1" to 1,
        "flash_intensity_2" to 2,
        "flash_intensity_3" to 3,
        "flash_intensity_4" to 4,
        "flash_intensity_5" to 5,
      )
    private const val TAG = "FlashCtrl"
    private const val REAR_CAMERA_ID = "0"
    private const val TORCH_LEVEL_PROP = "persist.ext.flashlight.last_brightness"
  }
}
