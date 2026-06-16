package tun.proxy;

import android.net.VpnService;
import android.os.Bundle;

import android.app.AlertDialog;
import android.content.ComponentName;
import android.content.Context;
import android.content.Intent;
import android.content.ServiceConnection;
import android.content.SharedPreferences;
import android.content.pm.PackageManager;

import androidx.appcompat.app.AppCompatActivity;
import androidx.appcompat.widget.Toolbar;
import androidx.fragment.app.Fragment;
import androidx.preference.Preference;
import androidx.preference.PreferenceFragmentCompat;
import androidx.preference.PreferenceManager;

import android.os.Handler;
import android.os.IBinder;
import android.os.Looper;
import android.text.TextUtils;
import android.view.View;
import android.view.Menu;
import android.view.MenuItem;
import android.widget.Button;
import android.widget.EditText;

import tun.proxy.service.Tun2HttpVpnService;
import tun.utils.IPUtil;

public class MainActivity extends AppCompatActivity implements
        PreferenceFragmentCompat.OnPreferenceStartFragmentCallback {
    public static final int REQUEST_VPN = 1;
    public static final int REQUEST_CERT = 2;

    Button start;
    Button stop;
    EditText hostEditText;
    Handler statusHandler = new Handler(Looper.getMainLooper());

    private Tun2HttpVpnService service;

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        setContentView(R.layout.activity_main);
        Toolbar toolbar = findViewById(R.id.toolbar);
        setSupportActionBar(toolbar);

        start = findViewById(R.id.start);
        stop = findViewById(R.id.stop);
        hostEditText = findViewById(R.id.host);

        start.setOnClickListener(new View.OnClickListener() {
            @Override
            public void onClick(View v) {
                startVpn();
            }
        });
        stop.setOnClickListener(new View.OnClickListener() {
            @Override
            public void onClick(View v) {
                stopVpn();
            }
        });
        start.setEnabled(true);
        stop.setEnabled(false);

        loadHostPort();

    }
    @Override
    public boolean onPreferenceStartFragment(PreferenceFragmentCompat caller, Preference pref) {
        final Bundle args = pref.getExtras();
        final Fragment fragment = getSupportFragmentManager().getFragmentFactory().instantiate(getClassLoader(), pref.getFragment());
        fragment.setArguments(args);
        fragment.setTargetFragment(caller, 0);
        getSupportFragmentManager().beginTransaction()
            .replace(R.id.activity_settings, fragment)
            .addToBackStack(null)
            .commit();
        setTitle(pref.getTitle());
        return true;
    }

    @Override
    public boolean onCreateOptionsMenu(Menu menu) {
        // Inflate the menu; this adds items to the action bar if it is present.
        getMenuInflater().inflate(R.menu.menu_main, menu);
        return true;
    }

    @Override
    public boolean onPrepareOptionsMenu(Menu menu) {
        MenuItem item = menu.findItem(R.id.action_activity_settings);
        item.setEnabled(start.isEnabled());
        return true;
    }

    @Override
    public boolean onOptionsItemSelected(MenuItem item) {
        int item_id = item.getItemId();
        if (item_id == R.id.action_activity_settings) {
            Intent intent = new android.content.Intent(this, SettingsActivity.class);
            startActivity(intent);
        }
        else if (item_id ==  R.id.action_show_about) {
            new AlertDialog.Builder(this)
                .setTitle(getString(R.string.app_name) + getVersionName())
                .setMessage(R.string.app_name)
                .show();
        }
        else {
            return super.onOptionsItemSelected(item);
        }
        return true;
    }

    protected String getVersionName() {
        PackageManager packageManager = getPackageManager();
        if (packageManager == null) {
            return null;
        }

        try {
            return packageManager.getPackageInfo(getPackageName(), 0).versionName;
        } catch (PackageManager.NameNotFoundException e) {
            return null;
        }
    }

    private ServiceConnection serviceConnection = new ServiceConnection() {
        public void onServiceConnected(ComponentName className, IBinder binder) {
            Tun2HttpVpnService.ServiceBinder serviceBinder = (Tun2HttpVpnService.ServiceBinder) binder;
            service = serviceBinder.getService();
        }

        public void onServiceDisconnected(ComponentName className) {
            service = null;
        }
    };

    @Override
    protected void onResume() {
        super.onResume();
        start.setEnabled(false);
        stop.setEnabled(false);
        updateStatus();

        statusHandler.post(statusRunnable);

        Intent intent = new Intent(this, Tun2HttpVpnService.class);
        bindService(intent, serviceConnection, Context.BIND_AUTO_CREATE);
    }

    boolean isRunning() {
        return service != null && service.isRunning();
    }

    Runnable statusRunnable = new Runnable() {
        @Override
        public void run() {
        updateStatus();
        statusHandler.post(statusRunnable);
        }
    };

    @Override
    protected void onPause() {
        super.onPause();
        statusHandler.removeCallbacks(statusRunnable);
        unbindService(serviceConnection);
    }

    void updateStatus() {
        if (service == null) {
            return;
        }
        if (isRunning()) {
            start.setEnabled(false);
            hostEditText.setEnabled(false);
            stop.setEnabled(true);
        } else {
            start.setEnabled(true);
            hostEditText.setEnabled(true);
            stop.setEnabled(false);
        }
    }

    private void stopVpn() {
        start.setEnabled(true);
        stop.setEnabled(false);
        Tun2HttpVpnService.stop(this);
    }

    private void startVpn() {
        Intent i = VpnService.prepare(this);
        if (i != null) {
            startActivityForResult(i, REQUEST_VPN);
        } else {
            onActivityResult(REQUEST_VPN, RESULT_OK, null);
        }
    }

    @Override
    protected void onActivityResult(int requestCode, int resultCode, Intent data) {
        super.onActivityResult(requestCode, resultCode, data);
        if (resultCode != RESULT_OK) {
            return;
        }
        if (requestCode == REQUEST_VPN && parseAndSaveHostPort()) {
            start.setEnabled(false);
            stop.setEnabled(true);
            Tun2HttpVpnService.start(this);
        }
    }

    private void loadHostPort() {
        final SharedPreferences prefs = PreferenceManager.getDefaultSharedPreferences(this);
        final String proxyHost = prefs.getString(Tun2HttpVpnService.PREF_PROXY_HOST, "");
        int proxyPort = prefs.getInt(Tun2HttpVpnService.PREF_PROXY_PORT, 0);

        if (TextUtils.isEmpty(proxyHost)) {
            return;
        }
        hostEditText.setText(proxyHost + ":" + proxyPort);
    }

    private boolean parseAndSaveHostPort() {
        String hostPort = hostEditText.getText().toString();
        if (!IPUtil.isValidIPv4Address(hostPort)) {
            hostEditText.setError(getString(R.string.enter_host));
            return false;
        }
        String parts[] = hostPort.split(":");
        int port = 0;
        if (parts.length > 1) {
            try {
                port = Integer.parseInt(parts[1]);
            } catch (NumberFormatException e) {
                hostEditText.setError(getString(R.string.enter_host));
                return false;
            }
        }
        String[] ipParts = parts[0].split("\\.");
        String host = parts[0];
        SharedPreferences prefs = PreferenceManager.getDefaultSharedPreferences(this);
        SharedPreferences.Editor edit = prefs.edit();
        edit.putString(Tun2HttpVpnService.PREF_PROXY_HOST, host);
        edit.putInt(Tun2HttpVpnService.PREF_PROXY_PORT, port);
        edit.commit();
        return true;
    }
}