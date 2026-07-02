package tun.proxy.receiver;

import android.content.BroadcastReceiver;
import android.content.Context;
import android.content.Intent;
import android.content.SharedPreferences;
import android.net.VpnService;

import androidx.preference.PreferenceManager;

import android.util.Log;

import tun.proxy.MyApplication;
import tun.proxy.R;

import tun.proxy.service.Tun2HttpVpnService;

public class BootReceiver extends BroadcastReceiver {

    @Override
    public void onReceive(final Context context, Intent intent) {
        if (intent != null && !Intent.ACTION_BOOT_COMPLETED.equals(intent.getAction())) {
            return;
        }
        final MyApplication app = MyApplication.getInstance();
        assert app != null;
        boolean isRunning = app.loadProxyRunning(false);
        if (isRunning) {
            Intent prepare = VpnService.prepare(context);
            if (prepare == null) {
                Log.d(context.getString(R.string.app_name) + ".Boot", "Starting vpn");
                Tun2HttpVpnService.start(context);
            } else {
                Log.d(context.getString(R.string.app_name) + ".Boot", "Not prepared");
            }
        }
    }
}