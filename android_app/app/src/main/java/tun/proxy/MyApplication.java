package tun.proxy;

import android.app.Application;
import android.content.SharedPreferences;
import androidx.preference.PreferenceManager;

import java.util.HashSet;
import java.util.Set;

public class MyApplication extends Application {
    private final static String PREF_VPN_MODE = "pref_vpn_connection_mode";
    private final static String PREF_APP_KEY[] = {"pref_vpn_disallowed_application", "pref_vpn_allowed_application"};

    private static MyApplication instance;

    public static MyApplication getInstance() {
        return instance;
    }

    @Override
    public void onCreate() {
        super.onCreate();
        instance = this;
    }

        public VPNMode loadVPNMode() {
        final SharedPreferences sharedPreferences = PreferenceManager.getDefaultSharedPreferences(getApplicationContext());
        final String vpn_mode = sharedPreferences.getString(PREF_VPN_MODE, MyApplication.VPNMode.DISALLOW.name());
        return VPNMode.valueOf(vpn_mode);
    };

    public void storeVPNMode(VPNMode mode) {
        final SharedPreferences prefs = PreferenceManager.getDefaultSharedPreferences(getApplicationContext());
        final SharedPreferences.Editor editor = prefs.edit();
        editor.putString(PREF_VPN_MODE, mode.name()).apply();
        return;
    };

    public Set<String> loadVPNApplication(VPNMode mode) {
        final SharedPreferences prefs = PreferenceManager.getDefaultSharedPreferences(getApplicationContext());
        final Set<String> preference = prefs.getStringSet(PREF_APP_KEY[mode.ordinal()], new HashSet<String>());
        return preference;
    };

    public void storeVPNApplication(VPNMode mode, final Set<String> set) {
        final SharedPreferences prefs = PreferenceManager.getDefaultSharedPreferences(getApplicationContext());
        final SharedPreferences.Editor editor = prefs.edit();
        editor.putStringSet(PREF_APP_KEY[mode.ordinal()], set).apply();
        return;
    };

    public enum VPNMode {DISALLOW, ALLOW}

    public enum AppSortBy {APPNAME, PKGNAME}

    public enum AppOrderBy {ASC, DESC}

    public enum AppFiltertBy {APPNAME, PKGNAME}

    private static final String PREF_DNS_USE_CUSTOM = "pref_dns_use_custom";
    public static final String PREF_DNS_PRIMARY = "pref_dns_primary";
    public static final String PREF_DNS_SECONDARY = "pref_dns_secondary";

    public boolean loadUseDnsCustom() {
        final SharedPreferences prefs = PreferenceManager.getDefaultSharedPreferences(getApplicationContext());
        return prefs.getBoolean(PREF_DNS_USE_CUSTOM, false);
    }

    public void storeUseDnsCustom(boolean isUseDnsCustom) {
        final SharedPreferences prefs = PreferenceManager.getDefaultSharedPreferences(getApplicationContext());
        final SharedPreferences.Editor editor = prefs.edit();
        editor.putBoolean(PREF_DNS_USE_CUSTOM, isUseDnsCustom).apply();
    }

    public String loadPrimaryDns(String defValue) {
        final SharedPreferences prefs = PreferenceManager.getDefaultSharedPreferences(getApplicationContext());
        return prefs.getString(PREF_DNS_PRIMARY, defValue);
    }

    public void storePrimaryDns(String primaryDns) {
        final SharedPreferences prefs = PreferenceManager.getDefaultSharedPreferences(getApplicationContext());
        final SharedPreferences.Editor editor = prefs.edit();
        editor.putString(PREF_DNS_PRIMARY, primaryDns).apply();
    }

    public String loadSecondaryDns(String defValue) {
        final SharedPreferences prefs = PreferenceManager.getDefaultSharedPreferences(getApplicationContext());
        return prefs.getString(PREF_DNS_SECONDARY, defValue);
    }

    public void storeSecondaryDns(String secondaryDns) {
        final SharedPreferences prefs = PreferenceManager.getDefaultSharedPreferences(getApplicationContext());
        final SharedPreferences.Editor editor = prefs.edit();
        editor.putString(PREF_DNS_SECONDARY, secondaryDns).apply();
    }
}
