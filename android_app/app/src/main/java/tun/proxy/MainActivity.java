package tun.proxy;

import android.graphics.Color;
import android.net.VpnService;
import android.os.Bundle;

import android.app.AlertDialog;
import android.content.ComponentName;
import android.content.Context;
import android.content.Intent;
import android.content.ServiceConnection;
import android.content.SharedPreferences;
import android.content.pm.PackageManager;

import androidx.activity.EdgeToEdge;
import androidx.activity.SystemBarStyle;
import androidx.appcompat.app.AppCompatActivity;
import androidx.appcompat.widget.Toolbar;
import androidx.core.graphics.Insets;
import androidx.core.view.ViewCompat;
import androidx.core.view.WindowInsetsCompat;
import androidx.preference.PreferenceManager;
import androidx.activity.result.ActivityResultLauncher;
import androidx.activity.result.contract.ActivityResultContracts;

import android.os.Handler;
import android.os.IBinder;
import android.os.Looper;
import android.text.TextUtils;
import android.util.Log;
import android.view.View;
import android.view.Menu;
import android.view.MenuItem;
import android.widget.ArrayAdapter;
import android.widget.Button;
import android.widget.EditText;
import android.widget.Spinner;
import android.widget.Toast;

import java.net.InetSocketAddress;
import java.net.Socket;

import tun.proxy.service.Tun2HttpVpnService;
import tun.utils.IPUtil;

public class MainActivity extends AppCompatActivity {

    Button start;
    Button stop;
    EditText hostEditText;
    Spinner proxyTypeSpinner;
    private final ActivityResultLauncher<Intent> vpnRequestLauncher = registerForActivityResult(
            new ActivityResultContracts.StartActivityForResult(),
            result -> {
                if (result.getResultCode() == RESULT_OK) {
                    vpnPrepared();
                }
            }
    );
    Handler statusHandler = new Handler(Looper.getMainLooper());
    private Tun2HttpVpnService service;
    private final ServiceConnection serviceConnection = new ServiceConnection() {
        public void onServiceConnected(ComponentName className, IBinder binder) {
            Tun2HttpVpnService.ServiceBinder serviceBinder = (Tun2HttpVpnService.ServiceBinder) binder;
            service = serviceBinder.getService();
        }

        public void onServiceDisconnected(ComponentName className) {
            service = null;
        }
    };

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        EdgeToEdge.enable(this, SystemBarStyle.dark(Color.TRANSPARENT));
        setContentView(R.layout.activity_main);
        ViewCompat.setOnApplyWindowInsetsListener(findViewById(R.id.container), (v, insets) -> {
            Insets systemBars = insets.getInsets(WindowInsetsCompat.Type.systemBars());
            v.setPadding(systemBars.left, systemBars.top, systemBars.right, systemBars.bottom);
            return WindowInsetsCompat.CONSUMED;
        });
        Toolbar toolbar = findViewById(R.id.toolbar);
        setSupportActionBar(toolbar);

        start = findViewById(R.id.start);
        stop = findViewById(R.id.stop);
        hostEditText = findViewById(R.id.host);
        proxyTypeSpinner = findViewById(R.id.proxy_type);
        ArrayAdapter<CharSequence> adapter = ArrayAdapter.createFromResource(this,
                R.array.proxy_types, R.layout.spinner_item);
        adapter.setDropDownViewResource(android.R.layout.simple_spinner_dropdown_item);
        proxyTypeSpinner.setAdapter(adapter);

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
        } else if (item_id == R.id.action_show_about) {
            new AlertDialog.Builder(this)
                    .setTitle(getString(R.string.app_name) + getVersionName())
                    .setMessage(R.string.app_name)
                    .show();
        } else {
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

    @Override
    protected void onPause() {
        super.onPause();
        statusHandler.removeCallbacks(statusRunnable);
        unbindService(serviceConnection);
    }

    Runnable statusRunnable = new Runnable() {
        @Override
        public void run() {
            updateStatus();
            statusHandler.post(statusRunnable);
        }
    };

    void updateStatus() {
        if (service == null) {
            return;
        }
        if (isRunning()) {
            start.setEnabled(false);
            hostEditText.setEnabled(false);
            proxyTypeSpinner.setEnabled(false);
            stop.setEnabled(true);
        } else {
            start.setEnabled(true);
            hostEditText.setEnabled(true);
            proxyTypeSpinner.setEnabled(true);
            stop.setEnabled(false);
        }
    }

    private void stopVpn() {
        start.setEnabled(true);
        stop.setEnabled(false);
        Tun2HttpVpnService.stop(this);
    }

    private void startVpn() {
        final MyApplication app = MyApplication.getInstance();
        assert app != null;
        final SharedPreferences prefs = PreferenceManager.getDefaultSharedPreferences(this);
        boolean connectivityCheck = app.loadProxyConnectivityCheck(false);

        if (connectivityCheck) {
            if (parseAndSaveHostPort()) {
                String host = prefs.getString(Tun2HttpVpnService.PREF_PROXY_HOST, "");
                int port = prefs.getInt(Tun2HttpVpnService.PREF_PROXY_PORT, 0);

                new Thread(() -> {
                    boolean reachable = false;
                    if (service != null && service.isNetworkConnected()) {
                        try (Socket socket = new Socket()) {
                            socket.connect(new InetSocketAddress(host, port), 3000);
                            reachable = true;
                        } catch (Exception e) {
                            Log.e("MainActivity", "Reachability check failed: " + e.getMessage());
                        }
                    }

                    boolean finalReachable = reachable;
                    runOnUiThread(() -> {
                        if (finalReachable) {
                            proceedWithStartVpn();
                        } else {
                            Toast.makeText(this, "Connectivity check failed", Toast.LENGTH_SHORT).show();
                        }
                    });
                }).start();
            }
        } else {
            if (parseAndSaveHostPort()) {
                proceedWithStartVpn();
            }
        }
    }

    private void proceedWithStartVpn() {
        Intent i = VpnService.prepare(this);
        if (i != null) {
            vpnRequestLauncher.launch(i);
        } else {
            vpnPrepared();
        }
    }

    private void vpnPrepared() {
        start.setEnabled(false);
        stop.setEnabled(true);
        Tun2HttpVpnService.start(this);
    }

    private void loadHostPort() {
        final SharedPreferences prefs = PreferenceManager.getDefaultSharedPreferences(this);
        final String proxyHost = prefs.getString(Tun2HttpVpnService.PREF_PROXY_HOST, "");
        int proxyPort = prefs.getInt(Tun2HttpVpnService.PREF_PROXY_PORT, 0);
        String proxyType = prefs.getString(Tun2HttpVpnService.PREF_PROXY_TYPE, "HTTP");

        if (TextUtils.isEmpty(proxyHost)) {
            return;
        }
        hostEditText.setText(proxyHost + ":" + proxyPort);
        if ("SOCKS5".equals(proxyType)) {
            proxyTypeSpinner.setSelection(1);
        } else {
            proxyTypeSpinner.setSelection(0);
        }
    }

    private boolean parseAndSaveHostPort() {
        String proxyTarget = hostEditText.getText().toString();
        if (!IPUtil.isValidIPv4Address(proxyTarget)) {
            hostEditText.setError(getString(R.string.enter_host));
            return false;
        }
        // IPv4:port 分離
        String parts[] = proxyTarget.split(":");
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
        String proxyType = (String) proxyTypeSpinner.getSelectedItem();

        SharedPreferences prefs = PreferenceManager.getDefaultSharedPreferences(this);
        SharedPreferences.Editor edit = prefs.edit();
        edit.putString(Tun2HttpVpnService.PREF_PROXY_HOST, host);
        edit.putInt(Tun2HttpVpnService.PREF_PROXY_PORT, port);
        edit.putString(Tun2HttpVpnService.PREF_PROXY_TYPE, proxyType);
        edit.apply();
        return true;
    }


}