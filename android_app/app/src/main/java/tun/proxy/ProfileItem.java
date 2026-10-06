package tun.proxy;

import org.json.JSONException;
import org.json.JSONObject;

import java.io.Serializable;
import java.nio.charset.StandardCharsets;

import tun.proxy.service.Tun2HttpVpnService;

public class ProfileItem implements Serializable {
    private String name;
    private final HostPortPair hostPort;
    private MyApplication.ProxyType type;
    private MyApplication.AuthMethod authMethod;
    private String username;
    private String password;

    public ProfileItem(String name, String host, int port, MyApplication.ProxyType type) {
        this(name, host, port, type, MyApplication.AuthMethod.NONE, "", "");
    }

    public ProfileItem(String name, String host, int port, MyApplication.ProxyType type,
                       MyApplication.AuthMethod authMethod, String username, String password) {
        this.name = name;
        this.hostPort = new HostPortPair(host, port);
        this.type = type;
        this.authMethod = authMethod;
        this.username = username;
        this.password = password;
    }

    public String getName() {
        return name;
    }

    public void setName(String name) {
        this.name = name;
    }

    public String getHost() {
        return this.hostPort.getHost();
    }

    public void setHost(String host) {
        this.hostPort.setHost(host);
    }

    public int getPort() {
        return this.hostPort.getPort();
    }

    public void setPort(int port) {
        this.hostPort.setPort(port);
    }

    public MyApplication.ProxyType getType() {
        return type;
    }

    public void setType(MyApplication.ProxyType type) {
        this.type = type;
    }

    public MyApplication.AuthMethod getAuthMethod() {
        return authMethod;
    }

    public void setAuthMethod(MyApplication.AuthMethod authMethod) {
        this.authMethod = authMethod;
    }

    public String getUsername() {
        return username;
    }

    public void setUsername(String username) {
        this.username = username;
    }

    public String getPassword() {
        return password;
    }

    public void setPassword(String password) {
        this.password = password;
    }

    /**
     * SOCKS5 (RFC 1929) limits username and password to 255 bytes
     */
    public static boolean isValidCredential(String value) {
        return value.getBytes(StandardCharsets.UTF_8).length <= 255;
    }

    public JSONObject toJSONObject() throws JSONException {
        JSONObject obj = new JSONObject();
        obj.put("name", name);
        obj.put("host", this.hostPort.getHost());
        obj.put("port", this.hostPort.getPort());
        obj.put("type", type.name());
        obj.put("auth", authMethod.name());
        obj.put("username", username);
        obj.put("password", password);
        return obj;
    }

    public static ProfileItem fromJSONObject(JSONObject obj) throws JSONException {
        // Profiles saved before authentication support have no auth fields
        return new ProfileItem(
                obj.getString("name"),
                obj.getString("host"),
                obj.getInt("port"),
                MyApplication.ProxyType.valueOf(obj.getString("type")),
                MyApplication.AuthMethod.valueOf(obj.optString("auth", MyApplication.AuthMethod.NONE.name())),
                obj.optString("username", ""),
                obj.optString("password", "")
        );
    }

    @Override
    public String toString() {
        return name;
    }

    @Override
    public boolean equals(Object obj) {
        ProfileItem other = (ProfileItem) obj;
        if (other == null) {
            return false;
        }

        if (!this.name.equals(other.name)) {
            return false;
        }

        if (!this.hostPort.equals(other.hostPort)) {
            return false;
        }

        if (this.type != other.getType()) {
            return false;
        }

        if (this.authMethod != other.authMethod) {
            return false;
        }

        if (!this.username.equals(other.username) || !this.password.equals(other.password)) {
            return false;
        }

        return true;
    }
}
