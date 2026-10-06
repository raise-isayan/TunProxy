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
import android.view.View;
import android.view.Menu;
import android.view.MenuItem;
import android.widget.ArrayAdapter;
import android.widget.Button;
import android.widget.EditText;
import android.widget.AdapterView;
import android.widget.Spinner;
import android.widget.Toast;

import java.util.ArrayList;
import java.util.List;

import tun.proxy.service.Tun2HttpVpnService;
import tun.utils.NetUtil;

public class MainActivity extends AppCompatActivity {
    private static final String TAG = "MainActivity";

    Button startButton;
    Button stopButton;
    EditText hostPortEditText;
    Spinner proxyTypeSpinner;
    Spinner authMethodSpinner;
    EditText usernameEditText;
    EditText passwordEditText;
    Spinner profileSpinner;
    private List<ProfileItem> profileList;
    private ArrayAdapter<ProfileItem> profileAdapter;
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

        startButton = findViewById(R.id.start);
        stopButton = findViewById(R.id.stop);
        hostPortEditText = findViewById(R.id.host);
        proxyTypeSpinner = findViewById(R.id.proxy_type);
        authMethodSpinner = findViewById(R.id.auth_method);
        usernameEditText = findViewById(R.id.username);
        passwordEditText = findViewById(R.id.password);
        profileSpinner = findViewById(R.id.spinner_profile);

        ArrayAdapter<CharSequence> adapter = ArrayAdapter.createFromResource(this,
                R.array.proxy_types, R.layout.spinner_item);
        adapter.setDropDownViewResource(R.layout.spinner_dropdown_item);
        proxyTypeSpinner.setAdapter(adapter);
        proxyTypeSpinner.setOnItemSelectedListener(new AdapterView.OnItemSelectedListener() {
            @Override
            public void onItemSelected(AdapterView<?> parent, View view, int position, long id) {
                if (!getSelectedProxyType().isAuthSupported()) {
                    authMethodSpinner.setSelection(MyApplication.AuthMethod.NONE.ordinal());
                }
                authMethodSpinner.setEnabled(proxyTypeSpinner.isEnabled() && getSelectedProxyType().isAuthSupported());
                updateAuthVisibility();
            }

            @Override
            public void onNothingSelected(AdapterView<?> parent) {
            }
        });

        ArrayAdapter<CharSequence> authAdapter = ArrayAdapter.createFromResource(this,
                R.array.auth_methods, R.layout.spinner_item);
        authAdapter.setDropDownViewResource(R.layout.spinner_dropdown_item);
        authMethodSpinner.setAdapter(authAdapter);
        authMethodSpinner.setOnItemSelectedListener(new AdapterView.OnItemSelectedListener() {
            @Override
            public void onItemSelected(AdapterView<?> parent, View view, int position, long id) {
                updateAuthVisibility();
            }

            @Override
            public void onNothingSelected(AdapterView<?> parent) {
            }
        });

        setupProfileSpinner();

        startButton.setOnClickListener(new View.OnClickListener() {
            @Override
            public void onClick(View v) {
                startVpn();
            }
        });
        stopButton.setOnClickListener(new View.OnClickListener() {
            @Override
            public void onClick(View v) {
                stopVpn();
            }
        });
        startButton.setEnabled(true);
        stopButton.setEnabled(false);
        loadHostPort();
    }

    private void setupProfileSpinner() {
        profileList = new ArrayList<>();
        profileList.add(new ProfileItem(getString(R.string.profile_manual), "", -1, MyApplication.ProxyType.HTTP));
        profileList.addAll(MyApplication.getInstance().loadProfiles());

        profileAdapter = new ArrayAdapter<>(this, R.layout.spinner_item, profileList);
        profileAdapter.setDropDownViewResource(R.layout.spinner_dropdown_item);
        profileSpinner.setAdapter(profileAdapter);

        profileSpinner.setOnItemSelectedListener(new AdapterView.OnItemSelectedListener() {
            @Override
            public void onItemSelected(AdapterView<?> parent, View view, int position, long id) {
                hostPortEditText.setError(null);
                usernameEditText.setError(null);
                if (position == 0) { // "Manual"
                    hostPortEditText.setText("");
                    proxyTypeSpinner.setSelection(MyApplication.ProxyType.HTTP.ordinal());
                    authMethodSpinner.setSelection(MyApplication.AuthMethod.NONE.ordinal());
                    usernameEditText.setText("");
                    passwordEditText.setText("");
                } else if (position > 0) { // Not "Manual"
                    ProfileItem profile = profileList.get(position);
                    hostPortEditText.setText(HostPortPair.valueOf(profile.getHost(), profile.getPort()));
                    proxyTypeSpinner.setSelection(profile.getType().ordinal());
                    authMethodSpinner.setSelection(profile.getAuthMethod().ordinal());
                    usernameEditText.setText(profile.getUsername());
                    passwordEditText.setText(profile.getPassword());
                }
                updateAuthVisibility();
            }

            @Override
            public void onNothingSelected(AdapterView<?> parent) {
            }
        });
    }

    @Override
    public boolean onCreateOptionsMenu(Menu menu) {
        // Inflate the menu; this adds items to the action bar if it is present.
        getMenuInflater().inflate(R.menu.menu_main, menu);
        return true;
    }

    @Override
    public boolean onPrepareOptionsMenu(Menu menu) {
        MenuItem itemSettings = menu.findItem(R.id.action_activity_settings);
        MenuItem itemProfile = menu.findItem(R.id.action_profile_settings);
        boolean enabled = this.startButton.isEnabled();
        if (itemSettings != null) itemSettings.setEnabled(enabled);
        if (itemProfile != null) itemProfile.setEnabled(enabled);
        return true;
    }

    @Override
    public boolean onOptionsItemSelected(MenuItem item) {
        int item_id = item.getItemId();
        if (item_id == R.id.action_activity_settings) {
            Intent intent = new android.content.Intent(this, SettingsActivity.class);
            startActivity(intent);
        } else if (item_id == R.id.action_profile_settings) {
            Intent intent = new Intent(this, ProfileSettingsActivity.class);
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
        startButton.setEnabled(false);
        stopButton.setEnabled(false);
        updateStatus();

        refreshProfileSpinner();

        statusHandler.post(statusRunnable);

        Intent intent = new Intent(this, Tun2HttpVpnService.class);
        bindService(intent, serviceConnection, Context.BIND_AUTO_CREATE);
    }

    private void refreshProfileSpinner() {
        if (profileList != null) {
            int selected = profileSpinner.getSelectedItemPosition();
            profileList.clear();
            profileList.add(new ProfileItem(getString(R.string.profile_manual), "", -1, MyApplication.ProxyType.HTTP));
            profileList.addAll(MyApplication.getInstance().loadProfiles());
            profileAdapter.notifyDataSetChanged();
            if (selected < profileList.size()) {
                profileSpinner.setSelection(selected);
            }
        }
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
        boolean running = isRunning();
        if (running) {
            startButton.setEnabled(false);
            hostPortEditText.setEnabled(false);
            proxyTypeSpinner.setEnabled(false);
            authMethodSpinner.setEnabled(false);
            usernameEditText.setEnabled(false);
            passwordEditText.setEnabled(false);
            profileSpinner.setEnabled(false);
            stopButton.setEnabled(true);
        } else {
            startButton.setEnabled(true);
            hostPortEditText.setEnabled(true);
            proxyTypeSpinner.setEnabled(true);
            authMethodSpinner.setEnabled(getSelectedProxyType().isAuthSupported());
            usernameEditText.setEnabled(true);
            passwordEditText.setEnabled(true);
            profileSpinner.setEnabled(true);
            stopButton.setEnabled(false);
        }
    }

    private void stopVpn() {
        startButton.setEnabled(true);
        stopButton.setEnabled(false);
        Tun2HttpVpnService.stop(this);
    }

    private void startVpn() {
        final MyApplication app = MyApplication.getInstance();
        assert app != null;
        final SharedPreferences prefs = PreferenceManager.getDefaultSharedPreferences(this);
        boolean connectivityCheck = app.loadProxyConnectivityCheck(false);

        if (parseAndSaveHostPort()) {
            String host = prefs.getString(Tun2HttpVpnService.PREF_PROXY_HOST, "");
            int port = prefs.getInt(Tun2HttpVpnService.PREF_PROXY_PORT, -1);

            new Thread(() -> {
                boolean resolvable = NetUtil.resolvHost(host);
                if (!resolvable) {
                    runOnUiThread(() -> {
                        Toast.makeText(this, "DNS resolution failed for " + host, Toast.LENGTH_SHORT).show();
                    });
                    return;
                }
                if (connectivityCheck) {
                    boolean reachable = false;
                    if (NetUtil.isNetworkCapabilities(service)) {
                        reachable = NetUtil.isConnection(host, port);
                    }
                    if (!reachable) {
                        runOnUiThread(() -> {
                            Toast.makeText(this, "Connectivity check failed", Toast.LENGTH_SHORT).show();
                        });
                        return;
                    }
                }

                runOnUiThread(this::proceedWithStartVpn);
            }).start();
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
        startButton.setEnabled(false);
        stopButton.setEnabled(true);
        Tun2HttpVpnService.start(this);
    }

    private void loadHostPort() {
        final SharedPreferences prefs = PreferenceManager.getDefaultSharedPreferences(this);
        final String proxyHost = prefs.getString(Tun2HttpVpnService.PREF_PROXY_HOST, "");
        int proxyPort = prefs.getInt(Tun2HttpVpnService.PREF_PROXY_PORT, -1);
        String proxyTypeName = prefs.getString(Tun2HttpVpnService.PREF_PROXY_TYPE, MyApplication.ProxyType.HTTP.name());
        MyApplication.ProxyType proxyType = Enum.valueOf(MyApplication.ProxyType.class, proxyTypeName);

        if (!NetUtil.isValidHost(proxyHost) || !NetUtil.isValiPort(proxyPort)) {
            return;
        }
        hostPortEditText.setText(HostPortPair.valueOf(proxyHost, proxyPort));
        proxyTypeSpinner.setSelection(proxyType.ordinal());

        String authMethodName = prefs.getString(Tun2HttpVpnService.PREF_PROXY_AUTH_METHOD, MyApplication.AuthMethod.NONE.name());
        MyApplication.AuthMethod authMethod = Enum.valueOf(MyApplication.AuthMethod.class, authMethodName);
        authMethodSpinner.setSelection(authMethod.ordinal());
        usernameEditText.setText(prefs.getString(Tun2HttpVpnService.PREF_PROXY_USERNAME, ""));
        passwordEditText.setText(prefs.getString(Tun2HttpVpnService.PREF_PROXY_PASSWORD, ""));
        updateAuthVisibility();
    }

    private MyApplication.ProxyType getSelectedProxyType() {
        return MyApplication.ProxyType.values()[proxyTypeSpinner.getSelectedItemPosition()];
    }

    private MyApplication.AuthMethod getSelectedAuthMethod() {
        if (!getSelectedProxyType().isAuthSupported()) {
            return MyApplication.AuthMethod.NONE;
        }
        return MyApplication.AuthMethod.values()[authMethodSpinner.getSelectedItemPosition()];
    }

    private void updateAuthVisibility() {
        int visibility = getSelectedAuthMethod() == MyApplication.AuthMethod.USERNAME_PASSWORD ? View.VISIBLE : View.GONE;
        usernameEditText.setVisibility(visibility);
        passwordEditText.setVisibility(visibility);
    }

    private boolean parseAndSaveHostPort() {
        String proxyTarget = hostPortEditText.getText().toString();
        if (!NetUtil.isValidHostPort(proxyTarget)) {
            hostPortEditText.setError(getString(R.string.enter_host));
            return false;
        }
        MyApplication.AuthMethod authMethod = getSelectedAuthMethod();
        String username = usernameEditText.getText().toString();
        String password = passwordEditText.getText().toString();
        if (authMethod == MyApplication.AuthMethod.USERNAME_PASSWORD) {
            if (username.isEmpty()) {
                usernameEditText.setError(getString(R.string.auth_error_username));
                return false;
            }
            if (!ProfileItem.isValidCredential(username) || !ProfileItem.isValidCredential(password)) {
                usernameEditText.setError(getString(R.string.auth_error_length));
                return false;
            }
        }
        try {
            // host:port 分離
            HostPortPair hostPortPair = HostPortPair.parse(proxyTarget);
            String host = hostPortPair.getHost();
            int port = hostPortPair.getPort();
            String proxyType = (String) proxyTypeSpinner.getSelectedItem();
            SharedPreferences prefs = PreferenceManager.getDefaultSharedPreferences(this);
            SharedPreferences.Editor edit = prefs.edit();
            edit.putString(Tun2HttpVpnService.PREF_PROXY_HOST, host);
            edit.putInt(Tun2HttpVpnService.PREF_PROXY_PORT, port);
            edit.putString(Tun2HttpVpnService.PREF_PROXY_TYPE, proxyType);
            edit.putString(Tun2HttpVpnService.PREF_PROXY_AUTH_METHOD, authMethod.name());
            edit.putString(Tun2HttpVpnService.PREF_PROXY_USERNAME, username);
            edit.putString(Tun2HttpVpnService.PREF_PROXY_PASSWORD, password);
            edit.apply();
        } catch (NumberFormatException e) {
            hostPortEditText.setError(getString(R.string.enter_host));
            return false;
        }
        return true;
    }


}