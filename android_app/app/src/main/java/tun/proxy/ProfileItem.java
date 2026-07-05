package tun.proxy;

import org.json.JSONException;
import org.json.JSONObject;

import java.io.Serializable;

public class ProfileItem implements Serializable {
    private String name;
    private String host;
    private int port;
    private MyApplication.ProxyType type;

    public ProfileItem(String name, String host, int port, MyApplication.ProxyType type) {
        this.name = name;
        this.host = host;
        this.port = port;
        this.type = type;
    }

    public String getName() {
        return name;
    }

    public void setName(String name) {
        this.name = name;
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

    public MyApplication.ProxyType getType() {
        return type;
    }

    public void setType(MyApplication.ProxyType type) {
        this.type = type;
    }

    public JSONObject toJSONObject() throws JSONException {
        JSONObject obj = new JSONObject();
        obj.put("name", name);
        obj.put("host", host);
        obj.put("port", port);
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
}
