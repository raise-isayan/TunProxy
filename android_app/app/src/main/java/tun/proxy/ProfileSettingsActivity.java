package tun.proxy;

import android.graphics.Color;
import android.os.Bundle;
import android.view.LayoutInflater;
import android.view.Menu;
import android.view.MenuItem;
import android.view.View;
import android.view.ViewGroup;
import android.widget.AdapterView;
import android.widget.ArrayAdapter;
import android.widget.EditText;
import android.widget.ImageButton;
import android.widget.ListView;
import android.widget.Spinner;
import android.widget.TextView;

import androidx.activity.EdgeToEdge;
import androidx.activity.SystemBarStyle;
import androidx.annotation.NonNull;
import androidx.annotation.Nullable;
import androidx.appcompat.app.ActionBar;
import androidx.appcompat.app.AlertDialog;
import androidx.appcompat.app.AppCompatActivity;
import androidx.core.graphics.Insets;
import androidx.core.view.ViewCompat;
import androidx.core.view.WindowInsetsCompat;

import java.util.List;
import java.util.Locale;

import tun.utils.NetUtil;

public class ProfileSettingsActivity extends AppCompatActivity {

    private ArrayAdapter<ProfileItem> adapter;
    private List<ProfileItem> profileList;

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        EdgeToEdge.enable(this, SystemBarStyle.dark(Color.TRANSPARENT));
        setContentView(R.layout.activity_profile_settings);

        ViewCompat.setOnApplyWindowInsetsListener(findViewById(R.id.profile_setting_container), (v, insets) -> {
            Insets systemBars = insets.getInsets(WindowInsetsCompat.Type.systemBars());
            v.setPadding(systemBars.left, systemBars.top, systemBars.right, systemBars.bottom);
            return WindowInsetsCompat.CONSUMED;
        });

        ActionBar actionBar = getSupportActionBar();
        if (actionBar != null) {
            actionBar.setDisplayHomeAsUpEnabled(true);
            actionBar.setTitle(R.string.title_profile_settings);
        }

        ListView listView = findViewById(R.id.profile_list);
        profileList = MyApplication.getInstance().loadProfiles();
        adapter = new ArrayAdapter<ProfileItem>(this, R.layout.item_profile, profileList) {
            @NonNull
            @Override
            public View getView(int position, @Nullable View convertView, @NonNull ViewGroup parent) {
                if (convertView == null) {
                    convertView = LayoutInflater.from(getContext()).inflate(R.layout.item_profile, parent, false);
                }
                ProfileItem item = getItem(position);
                if (item != null) {
                    TextView textName = convertView.findViewById(R.id.text_name);
                    TextView textHostPort = convertView.findViewById(R.id.text_host_port);
                    TextView textType = convertView.findViewById(R.id.text_type);
                    ImageButton btnEdit = convertView.findViewById(R.id.btn_edit);
                    ImageButton btnDelete = convertView.findViewById(R.id.btn_delete);

                    textName.setText(item.getName());
                    textHostPort.setText(HostPortPair.valueOf(item.getHost(), item.getPort()));
                    if (item.getAuthMethod() == MyApplication.AuthMethod.USERNAME_PASSWORD) {
                        textType.setText(getString(R.string.profile_item_auth, item.getType().name()));
                    } else {
                        textType.setText(item.getType().name());
                    }

                    boolean running = MyApplication.getInstance().loadProxyRunning(false);
                    btnEdit.setEnabled(!running);
                    btnDelete.setEnabled(!running);

                    btnEdit.setOnClickListener(v -> showProfileEditDialog(item, position));
                    btnDelete.setOnClickListener(v -> {
                        new AlertDialog.Builder(ProfileSettingsActivity.this)
                                .setTitle(R.string.profile_delete_title)
                                .setMessage(R.string.profile_delete_msg)
                                .setPositiveButton(android.R.string.ok, (dialog, which) -> {
                                    profileList.remove(position);
                                    MyApplication.getInstance().storeProfiles(profileList);
                                    notifyDataSetChanged();
                                })
                                .setNegativeButton(android.R.string.cancel, null)
                                .show();
                    });
                }
                return convertView;
            }
        };
        listView.setAdapter(adapter);
    }

    @Override
    public boolean onCreateOptionsMenu(Menu menu) {
        getMenuInflater().inflate(R.menu.menu_profile_settings, menu);
        return true;
    }

    @Override
    public boolean onPrepareOptionsMenu(Menu menu) {
        MenuItem item = menu.findItem(R.id.action_add_profile);
        if (item != null) {
            boolean running = MyApplication.getInstance().loadProxyRunning(false);
            item.setEnabled(!running);
        }
        return super.onPrepareOptionsMenu(menu);
    }

    @Override
    public boolean onOptionsItemSelected(MenuItem item) {
        if (item.getItemId() == R.id.action_add_profile) {
            showProfileEditDialog(null, -1);
            return true;
        }
        return super.onOptionsItemSelected(item);
    }

    private void showProfileEditDialog(ProfileItem profile, int position) {
        View dialogView = LayoutInflater.from(this).inflate(R.layout.dialog_profile_edit, null);
        EditText editName = dialogView.findViewById(R.id.edit_name);
        EditText editHostPort = dialogView.findViewById(R.id.edit_host_port);
        Spinner spinnerType = dialogView.findViewById(R.id.spinner_type);
        Spinner spinnerAuthMethod = dialogView.findViewById(R.id.spinner_auth_method);
        EditText editUsername = dialogView.findViewById(R.id.edit_username);
        EditText editPassword = dialogView.findViewById(R.id.edit_password);
        TextView textError = dialogView.findViewById(R.id.text_error);

        ArrayAdapter<CharSequence> typeAdapter = ArrayAdapter.createFromResource(this,
                R.array.proxy_types, R.layout.spinner_item);
        typeAdapter.setDropDownViewResource(R.layout.spinner_dropdown_item);
        spinnerType.setAdapter(typeAdapter);

        ArrayAdapter<CharSequence> authAdapter = ArrayAdapter.createFromResource(this,
                R.array.auth_methods, R.layout.spinner_item);
        authAdapter.setDropDownViewResource(R.layout.spinner_dropdown_item);
        spinnerAuthMethod.setAdapter(authAdapter);

        Runnable updateAuthViews = () -> {
            MyApplication.ProxyType type = MyApplication.ProxyType.values()[spinnerType.getSelectedItemPosition()];
            if (!type.isAuthSupported()) {
                spinnerAuthMethod.setSelection(MyApplication.AuthMethod.NONE.ordinal());
            }
            spinnerAuthMethod.setEnabled(type.isAuthSupported());
            boolean useAuth = type.isAuthSupported()
                    && MyApplication.AuthMethod.values()[spinnerAuthMethod.getSelectedItemPosition()] == MyApplication.AuthMethod.USERNAME_PASSWORD;
            int visibility = useAuth ? View.VISIBLE : View.GONE;
            editUsername.setVisibility(visibility);
            editPassword.setVisibility(visibility);
        };
        AdapterView.OnItemSelectedListener authListener = new AdapterView.OnItemSelectedListener() {
            @Override
            public void onItemSelected(AdapterView<?> parent, View view, int position, long id) {
                updateAuthViews.run();
            }

            @Override
            public void onNothingSelected(AdapterView<?> parent) {
            }
        };
        spinnerType.setOnItemSelectedListener(authListener);
        spinnerAuthMethod.setOnItemSelectedListener(authListener);

        if (profile != null) {
            editName.setText(profile.getName());
            editHostPort.setText(HostPortPair.valueOf(profile.getHost(), profile.getPort()));
            spinnerType.setSelection(profile.getType().ordinal());
            spinnerAuthMethod.setSelection(profile.getAuthMethod().ordinal());
            editUsername.setText(profile.getUsername());
            editPassword.setText(profile.getPassword());
        }

        AlertDialog dialog = new AlertDialog.Builder(this)
                .setTitle(profile == null ? R.string.profile_new_title : R.string.profile_edit_title)
                .setView(dialogView)
                .setPositiveButton(android.R.string.ok, null)
                .setNegativeButton(android.R.string.cancel, null)
                .create();

        dialog.show();

        dialog.getButton(AlertDialog.BUTTON_POSITIVE).setOnClickListener(v -> {
            String name = editName.getText().toString();
            String hostPort = editHostPort.getText().toString();
            if (name.isEmpty()) {
                textError.setText(R.string.profile_error_name_fields);
                textError.setVisibility(View.VISIBLE);
                return;
            }
            if (!NetUtil.isValidHostPort(hostPort)) {
                textError.setText(R.string.profile_error_format);
                textError.setVisibility(View.VISIBLE);
                return;
            }
            MyApplication.AuthMethod authMethod = MyApplication.AuthMethod.values()[spinnerAuthMethod.getSelectedItemPosition()];
            if (!MyApplication.ProxyType.values()[spinnerType.getSelectedItemPosition()].isAuthSupported()) {
                authMethod = MyApplication.AuthMethod.NONE;
            }
            String username = editUsername.getText().toString();
            String password = editPassword.getText().toString();
            if (authMethod == MyApplication.AuthMethod.USERNAME_PASSWORD) {
                if (username.isEmpty()) {
                    textError.setText(R.string.auth_error_username);
                    textError.setVisibility(View.VISIBLE);
                    return;
                }
                if (!ProfileItem.isValidCredential(username) || !ProfileItem.isValidCredential(password)) {
                    textError.setText(R.string.auth_error_length);
                    textError.setVisibility(View.VISIBLE);
                    return;
                }
            } else {
                username = "";
                password = "";
            }
            try {
                HostPortPair hostPortPair = HostPortPair.parse(hostPort);
                String host = hostPortPair.getHost();
                int port = hostPortPair.getPort();
                if (!NetUtil.isValiPort(port)) {
                    textError.setText(R.string.profile_error_port);
                    textError.setVisibility(View.VISIBLE);
                    return;
                }
                MyApplication.ProxyType type = MyApplication.ProxyType.values()[spinnerType.getSelectedItemPosition()];
                if (profile == null) {
                    profileList.add(new ProfileItem(name, host, port, type, authMethod, username, password));
                } else {
                    profile.setName(name);
                    profile.setHost(host);
                    profile.setPort(port);
                    profile.setType(type);
                    profile.setAuthMethod(authMethod);
                    profile.setUsername(username);
                    profile.setPassword(password);
                }
                MyApplication.getInstance().storeProfiles(profileList);
                adapter.notifyDataSetChanged();
                dialog.dismiss();
            } catch (IllegalArgumentException e) {
                textError.setText(R.string.profile_error_format);
                textError.setVisibility(View.VISIBLE);
            }
        });
    }

    @Override
    public boolean onSupportNavigateUp() {
        finish();
        return true;
    }
}
