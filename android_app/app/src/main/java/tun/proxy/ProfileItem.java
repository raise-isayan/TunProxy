package tun.proxy;

import org.json.JSONException;
import org.json.JSONObject;

import java.io.Serializable;

import tun.proxy.service.Tun2HttpVpnService;

public class ProfileItem implements Serializable {
    private String name;
//    private String host;
//    private int port;
    private final HostPortPair hostPort;
    private MyApplication.ProxyType type;

    public ProfileItem(String name, String host, int port, MyApplication.ProxyType type) {
        this.name = name;
        this.hostPort = new HostPortPair(host, port);
        this.type = type;
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

    public JSONObject toJSONObject() throws JSONException {
        JSONObject obj = new JSONObject();
        obj.put("name", name);
        obj.put("host", this.hostPort.getHost());
        obj.put("port", this.hostPort.getPort());
        obj.put("type", type.name());
        return obj;
    }

    public static ProfileItem fromJSONObject(JSONObject obj) throws JSONException {
        return new ProfileItem(
                obj.getString("name"),
                obj.getString("host"),
                obj.getInt("port"),
                MyApplication.ProxyType.valueOf(obj.getString("type"))
        );
    }

    @Override
    public String toString() {
        return name;
    }

    @Override
    public boolean equals(Object obj) {
        ProfileItem other =  (ProfileItem)obj;
        if (other == null)
            return false;

        if (!this.name.equals(other.name))
            return false;

        if (!this.hostPort.equals(other.hostPort))
            return false;

        if (this.type != other.getType())
            return false;

        return true;
    }
}
