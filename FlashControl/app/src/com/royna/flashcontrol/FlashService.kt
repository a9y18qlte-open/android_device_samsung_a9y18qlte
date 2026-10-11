package com.royna.flashcontrol

import android.app.Service
import android.content.Context
import android.content.Intent
import android.hardware.camera2.CameraManager
import android.os.Handler
import android.os.IBinder
import android.os.Looper
import android.os.ServiceManager
import android.util.Log
import androidx.preference.PreferenceManager
import vendor.samsung_ext.hardware.camera.flashlight.IFlashlight

class FlashService : Service() {
  private val mService: IFlashlight? =
    IFlashlight.Stub.asInterface(
      ServiceManager.waitForDeclaredService(
        "vendor.samsung_ext.hardware.camera.flashlight.IFlashlight/default"
      )
    )
  private val mMainHandler = Handler(Looper.getMainLooper())
  private lateinit var mContext: Context
  private var mRegistered = false

  // Torch state as reported by the camera service, for the rear camera.
  @Volatile private var mTorchOn = false

  private fun getBrightness(mCtx: Context) =
    PreferenceManager.getDefaultSharedPreferences(mCtx)
      .getInt(FlashFragment.PREF_FLASH_INTENSITY, 1)

  private val mApplyBrightness = Runnable {
    // The torch may have been turned off in the meantime: writing a brightness
    // level to the flash node would switch it back on.
    if (!mTorchOn) return@Runnable
    val brightness = getBrightness(mContext)
    Log.d("FlashControlSVC", "Setting $brightness")
    mService?.setBrightness(brightness)
  }

  private val mTorchCallback =
    object : CameraManager.TorchCallback() {
      override fun onTorchModeChanged(cameraId: String, enabled: Boolean) {
        if (cameraId != REAR_CAMERA_ID) return
        mTorchOn = enabled
        mMainHandler.removeCallbacks(mApplyBrightness)
        if (enabled) mMainHandler.postDelayed(mApplyBrightness, 25)
      }

      override fun onTorchModeUnavailable(cameraId: String) {
        if (cameraId != REAR_CAMERA_ID) return
        mTorchOn = false
        mMainHandler.removeCallbacks(mApplyBrightness)
      }
    }

  override fun onStartCommand(i: Intent?, flags: Int, startid: Int): Int {
    mContext = this
    if (!mRegistered) {
      getSystemService(CameraManager::class.java).registerTorchCallback(mTorchCallback, mMainHandler)
      mRegistered = true
    }
    return START_STICKY
  }

  override fun onDestroy() {
    if (mRegistered) {
      getSystemService(CameraManager::class.java).unregisterTorchCallback(mTorchCallback)
      mRegistered = false
    }
    mMainHandler.removeCallbacks(mApplyBrightness)
    super.onDestroy()
  }

  override fun onBind(i: Intent): IBinder? = null

  companion object {
    private const val REAR_CAMERA_ID = "0"
  }
}
