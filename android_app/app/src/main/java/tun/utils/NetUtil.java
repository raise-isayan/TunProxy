package tun.utils;

import android.content.Context;
import android.net.ConnectivityManager;
import android.net.LinkProperties;
import android.net.Network;
import android.net.NetworkCapabilities;
import android.util.Log;

import java.io.IOException;
import java.math.BigInteger;
import java.net.InetAddress;
import java.net.InetSocketAddress;
import java.net.Socket;
import java.net.UnknownHostException;
import java.util.ArrayList;
import java.util.List;

import tun.proxy.HostPortPair;

public class NetUtil {
    private static final String TAG = "Tun2Http.NetUtil";

    public static boolean isConnection(String host, int port) {
        boolean reachable = false;
        if (isValiPort(port)) {
            try (Socket socket = new Socket()) {
                socket.connect(new InetSocketAddress(host, port), 1000);
                reachable = true;
            } catch (IOException ignored) {
                // ignored
            }
        }
        return reachable;
    }

    public static boolean isNetworkCapabilities(Context context) {
        if (context != null) {
            final ConnectivityManager cm = (ConnectivityManager) context.getSystemService(Context.CONNECTIVITY_SERVICE);
            if (cm != null) {
                final Network n = cm.getActiveNetwork();
                if (n != null) {
                    final NetworkCapabilities nc = cm.getNetworkCapabilities(n);
                    if (nc != null) {
                        return (nc.hasTransport(NetworkCapabilities.TRANSPORT_CELLULAR) || nc.hasTransport(NetworkCapabilities.TRANSPORT_WIFI));
                    }
                }
            }
        }
        return false;
    }

    public static boolean resolvHost(String hostName) {
        boolean resolvable = false;
        try {
            InetAddress.getByName(hostName);
            resolvable = true;
        } catch (UnknownHostException ignored) {
            // ignored
        }
        return resolvable;
    }

    public static boolean isValidIPv4Address(String ipv4) {
        if (ipv4 == null || ipv4.isEmpty()) {
            return false;
        }
        String[] ipParts = ipv4.split("\\.");
        if (ipParts.length != 4) {
            return false;
        } else {
            for (int i = 0; i < ipParts.length; i++) {
                int ipPart = -1;
                try {
                    ipPart = Integer.parseInt(ipParts[i]);
                } catch (NumberFormatException e) {
                    return false;
                }
                if (!(0 <= ipPart && ipPart <= 255)) {
                    return false;
                }
            }
        }
        return true;
    }

    public static boolean isValidIPv6Address(String ipv6) {
        if (ipv6 == null || ipv6.isEmpty()) {
            return false;
        }

        // --- 【追加】ブラケット [...] で囲まれている場合の処理 ---
        if (ipv6.startsWith("[") && ipv6.endsWith("]")) {
            // 中身が空（"[]"）なら不正
            if (ipv6.length() == 2) {
                return false;
            }
            // 角括弧を外して中身の文字列を取り出す
            ipv6 = ipv6.substring(1, ipv6.length() - 1);
        } else {
            // 片方だけ角括弧があるような不正な形式（例: "[2001::" や "2001::]"）を弾く
            if (ipv6.contains("[") || ipv6.contains("]")) {
                return false;
            }
        }
        // --------------------------------------------------

        // 「::」そのものの場合は有効（未指定アドレス）
        if (ipv6.equals("::")) {
            return true;
        }

        // 先頭または末尾が ":" の場合の特殊処理
        if (ipv6.startsWith(":")) {
            if (!ipv6.startsWith("::")) return false;
        }
        if (ipv6.endsWith(":")) {
            if (!ipv6.endsWith("::")) return false;
        }

        // "::" が何回使われているかを正確にカウント
        int countDoubleColon = 0;
        for (int i = 0; i < ipv6.length() - 1; i++) {
            if (ipv6.charAt(i) == ':' && ipv6.charAt(i + 1) == ':') {
                countDoubleColon++;
                if (i + 2 < ipv6.length() && ipv6.charAt(i + 2) == ':') {
                    return false;
                }
                i++;
            }
        }

        if (countDoubleColon > 1) {
            return false;
        }

        // splitする際、末尾の空要素を保持するために第二引数に -1 を指定
        String[] parts = ipv6.split(":", -1);

        boolean hasDoubleColon = countDoubleColon == 1;

        // ブロック数の検証
        if (hasDoubleColon) {
            if (parts.length > 9 || parts.length < 3) {
                return false;
            }
        } else {
            if (parts.length != 8) {
                return false;
            }
        }

        int emptyCount = 0;

        for (String part : parts) {
            if (part.isEmpty()) {
                emptyCount++;
                if (emptyCount > 2) {
                    return false;
                }
                continue;
            }

            if (part.length() > 4) {
                return false;
            }

            try {
                int value = Integer.parseInt(part, 16);
                if (value < 0 || value > 0xFFFF) {
                    return false;
                }
            } catch (NumberFormatException e) {
                return false;
            }
        }

        if (!hasDoubleColon && emptyCount > 0) {
            return false;
        }

        return true;
    }

    public static boolean isValidHostPort(String hostPort) {
        if (hostPort == null || hostPort.isEmpty()) {
            return false;
        }
        try {
            HostPortPair hostPortPair = HostPortPair.parse(hostPort);
            if (!(isValidIPv4Address(hostPortPair.getHost()) || isDomain(hostPortPair.getHost()))) {
                return false;
            }
            if (!isValiPort(hostPortPair.getPort())) {
                return false;
            }
        } catch (NumberFormatException e) {
            return false;
        }
        return true;
    }

    public static boolean isValiPort(int port) {
        if (0 <= port && port < 65536) {
            return true;
        }
        return false;
    }

    public static boolean isDomain(String host) {
        if (host == null || host.isEmpty()) return false;
        // Basic domain validation:
        // 1. Starts with an alphabetic character
        // 2. Contains only alphanumeric characters, dots, and hyphens
        // 3. No leading/trailing hyphens in labels
        return host.matches("^[a-zA-Z]([a-zA-Z0-9-]{0,61}[a-zA-Z0-9])?(\\.[a-zA-Z0-9]([a-zA-Z0-9-]{0,61}[a-zA-Z0-9])?)*$");
    }

    public static boolean isValidHost(String host) {
        return isValidIPv4Address(host) || isDomain(host);
    }

    public static List<CIDR> toCIDR(String start, String end) throws UnknownHostException {
        return toCIDR(InetAddress.getByName(start), InetAddress.getByName(end));
    }

    public static List<CIDR> toCIDR(InetAddress start, InetAddress end) throws UnknownHostException {
        List<CIDR> listResult = new ArrayList<>();

        Log.i(TAG, "toCIDR(" + start.getHostAddress() + "," + end.getHostAddress() + ")");

        long from = inet2long(start);
        long to = inet2long(end);
        while (to >= from) {
            byte prefix = 32;
            while (prefix > 0) {
                long mask = prefix2mask(prefix - 1);
                if ((from & mask) != from)
                    break;
                prefix--;
            }
            byte max = (byte) (32 - Math.floor(Math.log(to - from + 1) / Math.log(2)));
            if (prefix < max)
                prefix = max;

            listResult.add(new CIDR(long2inet(from), prefix));

            BigInteger p = BigInteger.valueOf(2).pow(32 - prefix);
            from += p.longValue();
//            from += (long) Math.pow(2, (32 - prefix));
        }
        for (CIDR cidr : listResult)
            Log.i(TAG, cidr.toString());

        return listResult;
    }

    private static long prefix2mask(int bits) {
        return (0xFFFFFFFF00000000L >> bits) & 0xFFFFFFFFL;
    }

    private static long inet2long(InetAddress addr) {
        long result = 0;
        if (addr != null)
            for (byte b : addr.getAddress())
                result = result << 8 | (b & 0xFF);
        return result;
    }

    private static InetAddress long2inet(long addr) {
        try {
            byte[] b = new byte[4];
            for (int i = b.length - 1; i >= 0; i--) {
                b[i] = (byte) (addr & 0xFF);
                addr = addr >> 8;
            }
            return InetAddress.getByAddress(b);
        } catch (UnknownHostException ignore) {
            return null;
        }
    }

    public static InetAddress minus1(InetAddress addr) {
        return long2inet(inet2long(addr) - 1);
    }

    public static InetAddress plus1(InetAddress addr) {
        return long2inet(inet2long(addr) + 1);
    }

    private static native String jni_getprop(String name);

    public static List<String> getDefaultDNS(Context applicationContext) {
        List<String> dnsServers = new ArrayList<>();
        // ConnectivityManagerの取得
        ConnectivityManager cm = (ConnectivityManager) applicationContext.getSystemService(Context.CONNECTIVITY_SERVICE);
        if (cm == null) {
            return dnsServers;
        }

        // 現在アクティブなネットワークを取得
        Network activeNetwork = cm.getActiveNetwork();
        if (activeNetwork == null) {
            return dnsServers;
        }
        // ネットワークのリンクプロパティ（DNS情報などを含む）を取得
        LinkProperties linkProperties = cm.getLinkProperties(activeNetwork);
        if (linkProperties == null) {
            return dnsServers;
        }
        // DNSサーバーのリスト（InetAddress形式）を取得
        List<InetAddress> dnsAddresses = linkProperties.getDnsServers();
        for (InetAddress dnsAddress : dnsAddresses) {
            // ホスト名/IPアドレスの文字列を取得（IPv4/IPv6 両対応）
            dnsServers.add(dnsAddress.getHostAddress());
        }
        return dnsServers;
    }

    public static class CIDR implements Comparable<CIDR> {
        public InetAddress address;
        public int prefix;

        public CIDR(InetAddress address, int prefix) {
            this.address = address;
            this.prefix = prefix;
        }

        public CIDR(String ip, int prefix) {
            try {
                this.address = InetAddress.getByName(ip);
                this.prefix = prefix;
            } catch (UnknownHostException ex) {
                Log.e(TAG, ex + "\n" + Log.getStackTraceString(ex));
            }
        }

        public InetAddress getStart() {
            return long2inet(inet2long(this.address) & prefix2mask(this.prefix));
        }

        public InetAddress getEnd() {
            return long2inet((inet2long(this.address) & prefix2mask(this.prefix)) + (1L << (32 - this.prefix)) - 1);
        }

        @Override
        public String toString() {
            return this.address.getHostAddress() + "/" + this.prefix + "=" + getStart().getHostAddress() + "..." + getEnd().getHostAddress();
        }

        @Override
        public int compareTo(CIDR other) {
            Long lcidr = NetUtil.inet2long(this.address);
            Long lother = NetUtil.inet2long(other.address);
            return lcidr.compareTo(lother);
        }


    }
}
