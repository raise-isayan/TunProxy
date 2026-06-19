package tun.proxy;

import static org.junit.Assert.assertEquals;

import org.junit.Test;

import java.util.EnumSet;

import tun.utils.IPUtil;


/**
 * Example local unit test, which will execute on the development machine (host).
 *
 * @see <a href="http://d.android.com/tools/testing">Testing documentation</a>
 */
public class TunTest {
    @Test
    public void testSettingsActivity() {
        EnumSet<SettingsActivity.FilterAppType> filterType = EnumSet.allOf(SettingsActivity.FilterAppType.class);
        System.out.println("FilterAppType:" + filterType.toString());
        EnumSet<SettingsActivity.FilterAppType> result =  SettingsActivity.FilterAppType.parseEnumSet(filterType.toString());
        assertEquals(result, filterType);
    }
}