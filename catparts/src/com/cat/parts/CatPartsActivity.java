package com.cat.parts;

import android.app.ActionBar;
import android.app.Activity;
import android.content.Context;
import android.content.SharedPreferences;
import android.os.Bundle;
import android.preference.PreferenceFragment;
import android.preference.PreferenceManager;
import android.preference.SwitchPreference;
import android.view.MenuItem;
import android.view.View;
import android.widget.ListView;
import java.io.FileWriter;
import java.lang.reflect.Method;

public class CatPartsActivity extends Activity {

    private static void writeSysfs(String path, String val) {
        try {
            FileWriter fw = new FileWriter(path, false);
            fw.write(val);
            fw.flush();
            fw.close();
        } catch (Exception ignored) {
        }
    }

    private static void setProp(String key, String val) {
        try {
            Class<?> clazz = Class.forName("android.os.SystemProperties");
            Method method = clazz.getMethod("set", String.class, String.class);
            method.invoke(null, key, val);
        } catch (Exception ignored) {
        }
    }

    public static void syncConfigToDaemon(Context ctx) {
        try {
            Context deCtx = ctx.createDeviceProtectedStorageContext();
            SharedPreferences sp = PreferenceManager.getDefaultSharedPreferences(deCtx);
            String pttShort = sp.getString("ptt_short", "torch");
            String pttLong = sp.getString("ptt_long", "outer_toggle");
            String pttIncall = sp.getString("ptt_incall", "answer_spk");
            boolean outerEnabled = sp.getBoolean("outer_enabled", true);
            String outerTheme = sp.getString("outer_theme", "pipboy");
            String outerCallTheme = sp.getString("outer_call_theme", "pipboy");
            String outerFooter = sp.getString("outer_footer", "battery");
            boolean keylightEnabled = sp.getBoolean("keylight_enabled", true);
            String keylightTimeout = sp.getString("keylight_timeout", "0");
            String mouseSpeed = sp.getString("mouse_speed", "16");

            /* VoLTE / IMS & Telephony Settings */
            boolean imsRequestNetwork = sp.getBoolean("ims_request_network", true);
            boolean imsForceVolte = sp.getBoolean("ims_force_volte", true);
            boolean imsAsyncIncoming = sp.getBoolean("ims_async_incoming_calls", true);

            int applyId = sp.getInt("apply_counter", 0) + 1;
            sp.edit().putInt("apply_counter", applyId).commit();

            String effectiveKeylight = keylightEnabled ? "255" : "0";
            String effectiveOuter = outerEnabled ? "255" : "0";

            /* Direct immediate hardware feedback for keypad & outer display ON/OFF */
            writeSysfs("/sys/class/leds/keyboard_light/brightness", effectiveKeylight + "\n");
            writeSysfs("/sys/devices/platform/soc/soc:keyboard_light/leds/keyboard_light/brightness", effectiveKeylight + "\n");
            writeSysfs("/sys/class/leds/lcd-backlight2/brightness", effectiveOuter + "\n");

            /* Apply IMS & Telephony System Properties */
            if (imsForceVolte) {
                setProp("persist.dbg.volte_avail_ovr", "1");
                setProp("persist.dbg.volte_avail_ovr0", "1");
                setProp("persist.dbg.volte_avail_ovr1", "1");
                setProp("persist.dbg.volte_avail_ovr2", "1");
                setProp("persist.dbg.vt_avail_ovr", "1");
                setProp("persist.dbg.wfc_avail_ovr", "1");
                setProp("persist.dbg.allow_ims_off", "0");
                setProp("persist.sys.phh.ims.caf", "true");
            } else {
                setProp("persist.dbg.volte_avail_ovr", "0");
                setProp("persist.dbg.wfc_avail_ovr", "0");
                setProp("persist.dbg.allow_ims_off", "1");
            }

            if (imsAsyncIncoming) {
                setProp("persist.sys.phh.allow_binder_thread_on_incoming_calls", "1");
                setProp("ro.telephony.block_binder_thread_on_incoming_calls", "false");
            } else {
                setProp("persist.sys.phh.allow_binder_thread_on_incoming_calls", "0");
            }

            /* Request / Release persistent IMS cellular network */
            ImsNetworkRequester.updateState(ctx, imsRequestNetwork);

            StringBuilder sb = new StringBuilder();
            sb.append("ptt_short=").append(pttShort).append("\n");
            sb.append("ptt_long=").append(pttLong).append("\n");
            sb.append("ptt_incall=").append(pttIncall).append("\n");
            sb.append("outer_enabled=").append(outerEnabled ? "1" : "0").append("\n");
            sb.append("outer_brightness=").append(effectiveOuter).append("\n");
            sb.append("outer_theme=").append(outerTheme).append("\n");
            sb.append("outer_call_theme=").append(outerCallTheme).append("\n");
            sb.append("outer_footer=").append(outerFooter).append("\n");
            sb.append("keylight_enabled=").append(keylightEnabled ? "1" : "0").append("\n");
            sb.append("keylight_brightness=").append(effectiveKeylight).append("\n");
            sb.append("keylight_timeout=").append(keylightTimeout).append("\n");
            sb.append("mouse_speed=").append(mouseSpeed).append("\n");
            sb.append("apply=").append(applyId).append("\n");

            FileWriter fw = new FileWriter("/dev/shm_outerscreen_cmd", false);
            fw.write(sb.toString());
            fw.flush();
            fw.close();
        } catch (Exception ignored) {
        }
    }

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        ActionBar ab = getActionBar();
        if (ab != null) {
            ab.setDisplayHomeAsUpEnabled(true);
            ab.setTitle("Cat S22 Flip Settings");
        }
        getFragmentManager().beginTransaction()
                .replace(android.R.id.content, new CatPrefsFragment())
                .commit();
        syncConfigToDaemon(this);
    }

    @Override
    public boolean onOptionsItemSelected(MenuItem item) {
        if (item.getItemId() == android.R.id.home) {
            finish();
            return true;
        }
        return super.onOptionsItemSelected(item);
    }

    public static class CatPrefsFragment extends PreferenceFragment
            implements SharedPreferences.OnSharedPreferenceChangeListener {
        @Override
        public void onCreate(Bundle savedInstanceState) {
            super.onCreate(savedInstanceState);
            getPreferenceManager().setStorageDeviceProtected();
            addPreferencesFromResource(R.xml.cat_preferences);
        }

        @Override
        public void onViewCreated(View view, Bundle savedInstanceState) {
            super.onViewCreated(view, savedInstanceState);
            view.setFitsSystemWindows(true);
            ListView lv = view.findViewById(android.R.id.list);
            if (lv != null) {
                lv.setClipToPadding(false);
                int padTop = (int) (8 * getResources().getDisplayMetrics().density);
                int padBot = (int) (24 * getResources().getDisplayMetrics().density);
                lv.setPadding(lv.getPaddingLeft(), padTop, lv.getPaddingRight(), padBot);
            }
        }

        @Override
        public void onResume() {
            super.onResume();
            SharedPreferences sp = getPreferenceScreen().getSharedPreferences();
            if (sp != null) {
                sp.registerOnSharedPreferenceChangeListener(this);
                SwitchPreference sw = (SwitchPreference) findPreference("outer_enabled");
                if (sw != null) {
                    sw.setChecked(sp.getBoolean("outer_enabled", true));
                }
                SwitchPreference swNet = (SwitchPreference) findPreference("ims_request_network");
                if (swNet != null) {
                    swNet.setChecked(sp.getBoolean("ims_request_network", true));
                }
                SwitchPreference swVolte = (SwitchPreference) findPreference("ims_force_volte");
                if (swVolte != null) {
                    swVolte.setChecked(sp.getBoolean("ims_force_volte", true));
                }
                SwitchPreference swAsync = (SwitchPreference) findPreference("ims_async_incoming_calls");
                if (swAsync != null) {
                    swAsync.setChecked(sp.getBoolean("ims_async_incoming_calls", true));
                }
            }
        }

        @Override
        public void onPause() {
            SharedPreferences sp = getPreferenceScreen().getSharedPreferences();
            if (sp != null) sp.unregisterOnSharedPreferenceChangeListener(this);
            super.onPause();
        }

        @Override
        public void onSharedPreferenceChanged(SharedPreferences sharedPreferences, String key) {
            if ("apply_counter".equals(key)) return;
            if ("outer_enabled".equals(key)) {
                SwitchPreference sw = (SwitchPreference) findPreference("outer_enabled");
                if (sw != null) {
                    sw.setChecked(sharedPreferences.getBoolean("outer_enabled", true));
                }
            }
            syncConfigToDaemon(getActivity());
        }
    }
}
