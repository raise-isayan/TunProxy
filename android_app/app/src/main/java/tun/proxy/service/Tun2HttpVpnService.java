package tun.proxy.service;

import android.annotation.SuppressLint;
import android.content.Context;
import android.content.Intent;
import android.content.SharedPreferences;
import android.content.pm.PackageManager;
import android.net.ConnectivityManager;
import android.net.Network;
import android.net.NetworkCapabilities;
import android.net.VpnService;
import android.os.Binder;
import android.os.IBinder;
import android.os.Parcel;
import android.os.ParcelFileDescriptor;
import android.os.PowerManager;
import android.os.RemoteException;

import androidx.annotation.NonNull;
import androidx.core.app.ServiceCompat;
import androidx.preference.PreferenceManager;

import android.text.TextUtils;
import android.util.Log;
import android.widget.Toast;

import java.io.ByteArrayOutputStream;
import java.io.IOException;
import java.io.InputStream;
import java.io.OutputStream;
import java.net.InetAddress;
import java.net.InetSocketAddress;
import java.net.Socket;
import java.nio.charset.StandardCharsets;
import java.util.ArrayList;
import java.util.Arrays;
import java.util.List;
import java.util.Set;

import tun.proxy.MyApplication;
import tun.proxy.R;
import tun.utils.NetUtil;

@SuppressLint("VpnServicePolicy")
public class Tun2HttpVpnService extends VpnService {
    private static final String TAG = "Tun2Http.Service";

    public static final String PREF_PROXY_HOST = "pref_proxy_host";
    public static final String PREF_PROXY_PORT = "pref_proxy_port";
    public static final String PREF_PROXY_TYPE = "pref_proxy_type";
    public static final String PREF_PROXY_AUTH_METHOD = "pref_proxy_auth_method";
    public static final String PREF_PROXY_USERNAME = "pref_proxy_username";
    public static final String PREF_PROXY_PASSWORD = "pref_proxy_password";
    private static final int AUTH_PROBE_TIMEOUT = 5000; // milliseconds
    private static final String ACTION_START = "start";
    private static final String ACTION_STOP = "stop";
    private static volatile PowerManager.WakeLock wlInstance = null;

    static {
        System.loadLibrary("tun2http");
    }

    private ParcelFileDescriptor vpn = null;

    synchronized private static PowerManager.WakeLock getLock(Context context) {
        if (wlInstance == null) {
            PowerManager pm = (PowerManager) context.getSystemService(Context.POWER_SERVICE);
            wlInstance = pm.newWakeLock(PowerManager.PARTIAL_WAKE_LOCK, context.getString(R.string.app_name) + " wakelock");
            wlInstance.setReferenceCounted(true);
        }
        return wlInstance;
    }

    public static void start(Context context) {
        Intent intent = new Intent(context, Tun2HttpVpnService.class);
        intent.setAction(ACTION_START);
        context.startService(intent);
    }

    public static void stop(Context context) {
        Intent intent = new Intent(context, Tun2HttpVpnService.class);
        intent.setAction(ACTION_STOP);
        context.startService(intent);
    }

    private native void jni_init();

    private native void jni_start(int tun, boolean fwd53, int rcode, String proxyIp, int proxyPort, int proxyType,
                                  String proxyUser, String proxyPass, String authChallenge);

    private native void jni_stop(int tun);

    private native int jni_get_mtu();

    private native void jni_done();

    @Override
    public IBinder onBind(Intent intent) {
        return new ServiceBinder();
    }

    public boolean isRunning() {
        return vpn != null;
    }

    private void start() {
        if (vpn == null) {
            Builder lastBuilder = getBuilder();
            vpn = startVPN(lastBuilder);
            if (vpn == null) {
                throw new IllegalStateException(getString((R.string.msg_start_failed)));
            }
            startNative(vpn);
        }
    }

    private void stop() {
        if (vpn != null) {
            stopNative(vpn);
            stopVPN(vpn);
            vpn = null;
        }
        ServiceCompat.stopForeground(this, ServiceCompat.STOP_FOREGROUND_REMOVE);
    }

    @Override
    public void onRevoke() {
        Log.i(TAG, "Revoke");

        stop();
        vpn = null;

        super.onRevoke();
    }

    private ParcelFileDescriptor startVPN(Builder builder) throws SecurityException {
        try {
            return builder.establish();
        } catch (SecurityException ex) {
            throw ex;
        } catch (Throwable ex) {
            Log.e(TAG, ex + "\n" + Log.getStackTraceString(ex));
            return null;
        }
    }

    private Builder getBuilder() {

        // Build VPN service
        Builder builder = new Builder();
        builder.setSession(getString(R.string.app_name));

        // VPN address
        SharedPreferences prefs = PreferenceManager.getDefaultSharedPreferences(this);
        String vpn4 = prefs.getString("vpn4", "10.1.10.1");
        builder.addAddress(vpn4, 32);
        String vpn6 = prefs.getString("vpn6", "fd00:1:fd00:1:fd00:1:fd00:1");
        builder.addAddress(vpn6, 128);

        builder.addRoute("0.0.0.0", 0);
        builder.addRoute("0:0:0:0:0:0:0:0", 0);

        MyApplication app = (MyApplication) this.getApplication();
        assert app != null;
        if (app.loadUseDnsCustom()) {
            String dns1 = app.loadPrimaryDns(getString((R.string.dns_google_primary)));
            String dns2 = app.loadSecondaryDns(getString((R.string.dns_google_secondary)));
            if (!TextUtils.isEmpty(dns1)) {
                Log.i(TAG, "custom primary DNS:" + dns1);
                builder.addDnsServer(dns1);
            }
            if (!TextUtils.isEmpty(dns2)) {
                Log.i(TAG, "custom secondary DNS:" + dns2);
                builder.addDnsServer(dns2);
            }
        } else {
            List<String> dnsList = NetUtil.getDefaultDNS(app.getApplicationContext());
            for (String dns : dnsList) {
                Log.i(TAG, "default DNS:" + dns);
                builder.addDnsServer(dns);
            }
        }
        if (builder.listDns.isEmpty()) {
            Toast.makeText(this, "DNS settings not found", Toast.LENGTH_SHORT).show();
        }

        // MTU
        int mtu = jni_get_mtu();
        Log.i(TAG, "MTU=" + mtu);
        builder.setMtu(mtu);

        // AAdd list of allowed and disallowed applications
        if (app.loadVPNMode() == MyApplication.VPNMode.DISALLOW) {
            Set<String> disallow = app.loadVPNApplication(MyApplication.VPNMode.DISALLOW);
            Log.d(TAG, "disallowed:" + disallow.size());
            List<String> notFoundPackageList = new ArrayList<>();
            builder.addDisallowedApplication(Arrays.asList(disallow.toArray(new String[0])), notFoundPackageList);
            disallow.removeAll(notFoundPackageList);
            app.storeVPNApplication(MyApplication.VPNMode.DISALLOW, disallow);
        } else {
            Set<String> allow = app.loadVPNApplication(MyApplication.VPNMode.ALLOW);
            Log.d(TAG, "allowed:" + allow.size());
            List<String> notFoundPackageList = new ArrayList<>();
            builder.addAllowedApplication(Arrays.asList(allow.toArray(new String[0])), notFoundPackageList);
            allow.removeAll(notFoundPackageList);
            app.storeVPNApplication(MyApplication.VPNMode.ALLOW, allow);
        }

        // Add list of allowed applications
        return builder;
    }

    private void startNative(ParcelFileDescriptor vpn) {
        SharedPreferences prefs = PreferenceManager.getDefaultSharedPreferences(this);
        final String proxyHost = prefs.getString(PREF_PROXY_HOST, "");
        final int proxyPort = prefs.getInt(PREF_PROXY_PORT, -1);
        final String proxyTypeName = prefs.getString(PREF_PROXY_TYPE, MyApplication.ProxyType.HTTP.name());
        final MyApplication.ProxyType proxyType = Enum.valueOf(MyApplication.ProxyType.class, proxyTypeName);
        final String authMethodName = prefs.getString(PREF_PROXY_AUTH_METHOD, MyApplication.AuthMethod.NONE.name());
        final MyApplication.AuthMethod authMethod = Enum.valueOf(MyApplication.AuthMethod.class, authMethodName);
        final boolean useAuth = proxyType.isAuthSupported() && authMethod == MyApplication.AuthMethod.USERNAME_PASSWORD;
        final String proxyUser = useAuth ? prefs.getString(PREF_PROXY_USERNAME, "") : "";
        final String proxyPass = useAuth ? prefs.getString(PREF_PROXY_PASSWORD, "") : "";

        if (NetUtil.isValidHost(proxyHost) && NetUtil.isValiPort(proxyPort)) {
            new Thread(() -> {
                String proxyIp = "";
                try {
                    InetAddress address = InetAddress.getByName(proxyHost);
                    proxyIp = address.getHostAddress();
                } catch (Exception e) {
                    Log.e(TAG, "DNS resolution failed for " + proxyHost + ": " + e.getMessage());
                }

                if (TextUtils.isEmpty(proxyIp)) {
                    stop();
                    return;
                }

                final String finalProxyIp = proxyIp;
                // HTTP proxy: learn whether Basic or Digest is required before the first request
                String authChallenge = "";
                if (proxyType == MyApplication.ProxyType.HTTP && !TextUtils.isEmpty(proxyUser)) {
                    authChallenge = probeProxyAuthChallenge(finalProxyIp, proxyPort);
                }
                jni_start(vpn.getFd(), false, 3, finalProxyIp, proxyPort, proxyType.ordinal(),
                        proxyUser, proxyPass, authChallenge);
                MyApplication app = (MyApplication) getApplication();
                if (app != null) {
                    app.storeProxyRunning(true);
                }
            }).start();
        }
    }

    /**
     * Send a CONNECT request without credentials to the HTTP proxy
     * and return the response header when authentication is required.
     */
    private String probeProxyAuthChallenge(String proxyIp, int proxyPort) {
        try (Socket socket = new Socket()) {
            // bind creates the file descriptor, so that it can be excluded from the VPN
            socket.bind(null);
            if (!protect(socket)) {
                Log.w(TAG, "Proxy authentication probe: protect failed");
                return "";
            }
            socket.connect(new InetSocketAddress(proxyIp, proxyPort), AUTH_PROBE_TIMEOUT);
            socket.setSoTimeout(AUTH_PROBE_TIMEOUT);
            String target = (proxyIp.contains(":") ? "[" + proxyIp + "]" : proxyIp) + ":" + proxyPort;
            String request = "CONNECT " + target + " HTTP/1.1\r\nHost: " + target + "\r\n\r\n";
            OutputStream out = socket.getOutputStream();
            out.write(request.getBytes(StandardCharsets.ISO_8859_1));
            out.flush();

            InputStream in = socket.getInputStream();
            ByteArrayOutputStream header = new ByteArrayOutputStream();
            byte[] buffer = new byte[4096];
            int len;
            while (header.size() < 16 * 1024 && (len = in.read(buffer)) > 0) {
                header.write(buffer, 0, len);
                String response = header.toString(StandardCharsets.ISO_8859_1.name());
                int end = response.indexOf("\r\n\r\n");
                if (end >= 0) {
                    response = response.substring(0, end + 4);
                    if (response.matches("(?s)HTTP/1\\.\\d 407.*")) {
                        return response;
                    }
                    break;
                }
            }
        } catch (IOException e) {
            Log.w(TAG, "Proxy authentication probe failed: " + e.getMessage());
        }
        return "";
    }

    private void stopNative(ParcelFileDescriptor vpn) {
        try {
            jni_stop(vpn.getFd());

        } catch (Throwable ex) {
            // File descriptor might be closed
            Log.e(TAG, ex + "\n" + Log.getStackTraceString(ex));
            jni_stop(-1);
        }

        SharedPreferences prefs = PreferenceManager.getDefaultSharedPreferences(this);
        MyApplication app = (MyApplication) this.getApplication();
        assert app != null;
        app.storeProxyRunning(false);
    }

    private void stopVPN(ParcelFileDescriptor pfd) {
        Log.i(TAG, "Stopping");
        try {
            pfd.close();
        } catch (IOException ex) {
            Log.e(TAG, ex + "\n" + Log.getStackTraceString(ex));
        }
    }

    // Called from native code
    private void nativeExit(String reason) {
        Log.w(TAG, "Native exit reason=" + reason);
        if (reason != null) {
            SharedPreferences prefs = PreferenceManager.getDefaultSharedPreferences(this);
            prefs.edit().putBoolean("enabled", false).apply();
        }
    }

    // Called from native code
    private void nativeError(int error, String message) {
        Log.w(TAG, "Native error " + error + ": " + message);
    }

    private boolean isSupported(int protocol) {
        return (protocol == 1 /* ICMPv4 */ ||
                protocol == 59 /* ICMPv6 */ ||
                protocol == 6 /* TCP */ ||
                protocol == 17 /* UDP */);
    }

    @Override
    public void onCreate() {
        // Native init
        jni_init();
        super.onCreate();
    }

    @Override
    public int onStartCommand(Intent intent, int flags, int startId) {
        Log.i(TAG, "Received " + intent);
        // Handle service restart
        if (intent == null) {
            return START_STICKY;
        }

        if (ACTION_START.equals(intent.getAction())) {
            start();
        }
        if (ACTION_STOP.equals(intent.getAction())) {
            stop();
        }
        return START_STICKY;
    }

    @Override
    public void onDestroy() {
        Log.i(TAG, "Destroy");

        try {
            if (vpn != null) {
                stopNative(vpn);
                stopVPN(vpn);
                vpn = null;
            }
        } catch (Throwable ex) {
            Log.e(TAG, ex + "\n" + Log.getStackTraceString(ex));
        }

        jni_done();

        super.onDestroy();
    }

    public class ServiceBinder extends Binder {
        @Override
        public boolean onTransact(int code, Parcel data, Parcel reply, int flags)
                throws RemoteException {
            // see Implementation of android.net.VpnService.Callback.onTransact()
            if (code == IBinder.LAST_CALL_TRANSACTION) {
                onRevoke();
                return true;
            }
            return super.onTransact(code, data, reply, flags);
        }

        public Tun2HttpVpnService getService() {
            return Tun2HttpVpnService.this;
        }
    }

    private class Builder extends VpnService.Builder {
        private int mtu;
        private final List<String> listAddress = new ArrayList<>();
        private final List<String> listRoute = new ArrayList<>();
        private final List<String> listDns = new ArrayList<>();

        private Builder() {
            super();
        }

        @NonNull
        @Override
        public VpnService.Builder setMtu(int mtu) {
            this.mtu = mtu;
            super.setMtu(mtu);
            return this;
        }

        @NonNull
        @Override
        public Builder addAddress(String address, int prefixLength) {
            listAddress.add(address + "/" + prefixLength);
            super.addAddress(address, prefixLength);
            return this;
        }

        @NonNull
        @Override
        public Builder addRoute(String address, int prefixLength) {
            listRoute.add(address + "/" + prefixLength);
            super.addRoute(address, prefixLength);
            return this;
        }

        @NonNull
        @Override
        public Builder addDnsServer(InetAddress address) {
            listDns.add(address.getHostAddress());
            super.addDnsServer(address);
            return this;
        }

        @NonNull
        @Override
        public Builder addDnsServer(String address) {
//            listDns.add(address);
            super.addDnsServer(address);
            return this;
        }

        // min sdk 26
        public void addAllowedApplication(final List<String> packageList, final List<String> notFoundPackegeList) {
            for (String pkg : packageList) {
                try {
                    Log.i(TAG, "allowed:" + pkg);
                    addAllowedApplication(pkg);
                } catch (PackageManager.NameNotFoundException e) {
                    notFoundPackegeList.add(pkg);
                }
            }
        }

        public void addDisallowedApplication(final List<String> packageList, final List<String> notFoundPackegeList) {
            //
            for (String pkg : packageList) {
                try {
                    Log.i(TAG, "disallowed:" + pkg);
                    addDisallowedApplication(pkg);
                } catch (PackageManager.NameNotFoundException e) {
                    notFoundPackegeList.add(pkg);
                }
            }
        }

        @Override
        public boolean equals(Object obj) {
            Builder other = (Builder) obj;

            if (other == null) {
                return false;
            }

            if (this.mtu != other.mtu) {
                return false;
            }

            if (this.listAddress.size() != other.listAddress.size()) {
                return false;
            }

            if (this.listRoute.size() != other.listRoute.size()) {
                return false;
            }

            if (this.listDns.size() != other.listDns.size()) {
                return false;
            }

            for (String address : this.listAddress) {
                if (!other.listAddress.contains(address)) {
                    return false;
                }
            }

            for (String route : this.listRoute) {
                if (!other.listRoute.contains(route)) {
                    return false;
                }
            }

            for (String dns : this.listDns) {
                if (!other.listDns.contains(dns)) {
                    return false;
                }
            }

            return true;
        }
    }

}
