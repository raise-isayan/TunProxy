package tun.proxy;

import org.json.JSONException;
import org.json.JSONObject;
import org.junit.Test;

import static org.junit.Assert.*;

public class ProfileItemTest {

    @Test
    public void testSerialization() throws JSONException {
        ProfileItem item = new ProfileItem("Test Profile", "127.0.0.1", 8080, MyApplication.ProxyType.HTTP);
        JSONObject json = item.toJSONObject();

        assertEquals("Test Profile", json.getString("name"));
        assertEquals("127.0.0.1", json.getString("host"));
        assertEquals(8080, json.getInt("port"));
        assertEquals("HTTP", json.getString("type"));

        ProfileItem item2 = ProfileItem.fromJSONObject(json);
        assertEquals(item.getName(), item2.getName());
        assertEquals(item.getHost(), item2.getHost());
        assertEquals(item.getPort(), item2.getPort());
        assertEquals(item.getType(), item2.getType());

        item.setName("Edit Profile");
        item.setHost("192.168.0.2");
        item.setPort(1080);
        item.setType(MyApplication.ProxyType.SOCKS5);

        assertEquals("Edit Profile", item.getName());
        assertEquals("192.168.0.2", item.getHost());
        assertEquals(1080, item.getPort());
        assertEquals(MyApplication.ProxyType.SOCKS5, item.getType());
    }

    @Test
    public void testSettersAndGetters() {
        ProfileItem item = new ProfileItem("Name", "Host", 1, MyApplication.ProxyType.HTTP);
        item.setName("New Name");
        item.setHost("New Host");
        item.setPort(2);
        item.setType(MyApplication.ProxyType.SOCKS5);

        assertEquals("New Name", item.getName());
        assertEquals("New Host", item.getHost());
        assertEquals(2, item.getPort());
        assertEquals(MyApplication.ProxyType.SOCKS5, item.getType());
    }

    @Test
    public void testToString() {
        ProfileItem item = new ProfileItem("Profile Name", "host", 80, MyApplication.ProxyType.HTTP);
        assertEquals("Profile Name", item.toString());
    }
}
