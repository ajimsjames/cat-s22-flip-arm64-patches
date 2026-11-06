package com.cat.parts;

import android.content.BroadcastReceiver;
import android.content.Context;
import android.content.Intent;
import android.content.SharedPreferences;
import android.hardware.camera2.CameraManager;
import android.preference.PreferenceManager;

public class BootReceiver extends BroadcastReceiver {
    @Override
    public void onReceive(Context context, Intent intent) {
        if (intent != null && "com.cat.parts.TOGGLE_TORCH".equals(intent.getAction())) {
            try {
                Context deCtx = context.createDeviceProtectedStorageContext();
                SharedPreferences sp = PreferenceManager.getDefaultSharedPreferences(deCtx);
                boolean nextState = !sp.getBoolean("torch_active", false);
                CameraManager cm = (CameraManager) context.getSystemService(Context.CAMERA_SERVICE);
                if (cm != null) {
                    String[] ids = cm.getCameraIdList();
                    if (ids != null && ids.length > 0) {
                        cm.setTorchMode(ids[0], nextState);
                        sp.edit().putBoolean("torch_active", nextState).commit();
                    }
                }
            } catch (Exception ignored) {
            }
            return;
        }
        if (intent != null && "com.cat.parts.TOGGLE_OUTER".equals(intent.getAction())) {
            try {
                Context deCtx = context.createDeviceProtectedStorageContext();
                SharedPreferences sp = PreferenceManager.getDefaultSharedPreferences(deCtx);
                boolean nextOuter = !sp.getBoolean("outer_enabled", true);
                sp.edit().putBoolean("outer_enabled", nextOuter).commit();
                CatPartsActivity.syncConfigToDaemon(context);
            } catch (Exception ignored) {
            }
            return;
        }
        CatPartsActivity.syncConfigToDaemon(context);
    }
}
