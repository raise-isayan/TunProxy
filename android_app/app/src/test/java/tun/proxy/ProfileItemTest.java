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
        item.setName("Edit Name");
        item.setHost("Edit Host");
        item.setPort(2);
        item.setType(MyApplication.ProxyType.SOCKS5);

        assertEquals("Edit Name", item.getName());
        assertEquals("Edit Host", item.getHost());
        assertEquals(2, item.getPort());
        assertEquals(MyApplication.ProxyType.SOCKS5, item.getType());
    }

    @Test
    public void testEq()  {
        ProfileItem item1 = new ProfileItem("Test Profile", "127.0.0.1", 8080, MyApplication.ProxyType.HTTP);
        ProfileItem item2 = new ProfileItem("Test Profile", "127.0.0.1", 8080, MyApplication.ProxyType.HTTP);
        assertFalse(item1.equals(null));
        assertTrue(item1.equals(item2));
    }


    @Test
    public void testAuthSerialization() throws JSONException {
        ProfileItem item = new ProfileItem("Auth Profile", "127.0.0.1", 8080, MyApplication.ProxyType.HTTP,
                MyApplication.AuthMethod.USERNAME_PASSWORD, "user", "pass");
        JSONObject json = item.toJSONObject();

        assertEquals("USERNAME_PASSWORD", json.getString("auth"));
        assertEquals("user", json.getString("username"));
        assertEquals("pass", json.getString("password"));

        ProfileItem item2 = ProfileItem.fromJSONObject(json);
        assertEquals(MyApplication.AuthMethod.USERNAME_PASSWORD, item2.getAuthMethod());
        assertEquals("user", item2.getUsername());
        assertEquals("pass", item2.getPassword());
        assertTrue(item.equals(item2));

        item2.setPassword("other");
        assertFalse(item.equals(item2));
    }

    @Test
    public void testLegacyJSON() throws JSONException {
        // Profiles saved before authentication support
        JSONObject json = new JSONObject();
        json.put("name", "Old Profile");
        json.put("host", "127.0.0.1");
        json.put("port", 1080);
        json.put("type", "SOCKS5");

        ProfileItem item = ProfileItem.fromJSONObject(json);
        assertEquals(MyApplication.AuthMethod.NONE, item.getAuthMethod());
        assertEquals("", item.getUsername());
        assertEquals("", item.getPassword());
    }

    @Test
    public void testIsValidCredential() {
        assertTrue(ProfileItem.isValidCredential(""));
        assertTrue(ProfileItem.isValidCredential(new String(new char[255]).replace('\0', 'a')));
        assertFalse(ProfileItem.isValidCredential(new String(new char[256]).replace('\0', 'a')));
        // 3 bytes per character in UTF-8
        assertFalse(ProfileItem.isValidCredential(new String(new char[86]).replace('\0', '\u3042')));
    }

    @Test
    public void testToString() {
        ProfileItem item = new ProfileItem("Profile Name", "host", 80, MyApplication.ProxyType.HTTP);
        assertEquals("Profile Name", item.toString());
    }
}
