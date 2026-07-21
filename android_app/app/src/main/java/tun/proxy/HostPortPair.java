package tun.proxy;

import java.util.Locale;

public class HostPortPair {

    private String host = "";
    private int port = -1;

    public HostPortPair() {

    }

    public HostPortPair(String host, int port) {
        this.host = host;
        this.port = port;
    }

    public String getHost() {
        return host;
    }

    public void setHost(String host) {
        this.host = host;
    }

    public int getPort() {
        return port;
    }

    public void setPort(int port) {
        this.port = port;
    }

    public static HostPortPair parse(String hostPort) throws IllegalArgumentException {
        final HostPortPair pair = new HostPortPair();
        String[] parts = hostPort.split(":");
        int port = -1; // デフォルト値にはならないはず
        if (parts.length == 2) {
            port = Integer.parseInt(parts[1]);
            pair.host = parts[0];
            pair.port = port;
        } else {
            throw new IllegalArgumentException("Invalid host:port format:" + hostPort);
        }
        return pair;
    }

    public static String valueOf(String host, int port) {
        return String.format(Locale.ROOT, "%s:%d", host, port);
    }

    @Override
    public boolean equals(Object obj) {
        HostPortPair other = (HostPortPair) obj;
        if (other == null)
            return false;

        if (!this.host.equals(other.host))
            return false;

        if (this.port != other.port)
            return false;

        return true;
    }

}
