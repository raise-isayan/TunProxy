package tun.proxy;

import android.os.Bundle;
import android.view.LayoutInflater;
import android.view.Menu;
import android.view.MenuItem;
import android.view.View;
import android.view.ViewGroup;
import android.widget.ArrayAdapter;
import android.widget.EditText;
import android.widget.ImageButton;
import android.widget.ListView;
import android.widget.Spinner;
import android.widget.TextView;

import androidx.annotation.NonNull;
import androidx.annotation.Nullable;
import androidx.appcompat.app.AlertDialog;
import androidx.appcompat.app.AppCompatActivity;
import androidx.appcompat.widget.Toolbar;

import java.util.List;
import java.util.Locale;

import tun.utils.IPUtil;

public class ProfileSettingActivity extends AppCompatActivity {

    private ArrayAdapter<ProfileItem> adapter;
    private List<ProfileItem> profileList;

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        setContentView(R.layout.activity_profile_setting);

        Toolbar toolbar = findViewById(R.id.toolbar);
        setSupportActionBar(toolbar);
        if (getSupportActionBar() != null) {
            getSupportActionBar().setDisplayHomeAsUpEnabled(true);
            getSupportActionBar().setTitle(R.string.title_profile_settings);
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
                    textHostPort.setText(String.format(Locale.ROOT, "%s:%d", item.getHost(), item.getPort()));
                    textType.setText(item.getType().name());

                    boolean running = MyApplication.getInstance().loadProxyRunning(false);
                    btnEdit.setEnabled(!running);
                    btnDelete.setEnabled(!running);

                    btnEdit.setOnClickListener(v -> showProfileEditDialog(item, position));
                    btnDelete.setOnClickListener(v -> {
                        new AlertDialog.Builder(ProfileSettingActivity.this)
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
        getMenuInflater().inflate(R.menu.menu_profile_setting, menu);
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
        TextView textError = dialogView.findViewById(R.id.text_error);

        if (profile != null) {
            editName.setText(profile.getName());
            editHostPort.setText(String.format(Locale.ROOT, "%s:%d", profile.getHost(), profile.getPort()));
            spinnerType.setSelection(profile.getType().ordinal());
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
            if (name.isEmpty() || hostPort.isEmpty()) {
                textError.setText(R.string.profile_error_fields);
                textError.setVisibility(View.VISIBLE);
                return;
            }
            if (!IPUtil.isValidHostPort(hostPort)) {
                textError.setText(R.string.profile_error_format);
                textError.setVisibility(View.VISIBLE);
                return;
            }
            String[] parts = hostPort.split(":");
            if (parts.length < 2) {
                textError.setText(R.string.profile_error_format);
                textError.setVisibility(View.VISIBLE);
                return;
            }

            try {
                String host = parts[0];
                int port = Integer.parseInt(parts[1]);
                MyApplication.ProxyType type = MyApplication.ProxyType.values()[spinnerType.getSelectedItemPosition()];

                if (profile == null) {
                    profileList.add(new ProfileItem(name, host, port, type));
                } else {
                    profile.setName(name);
                    profile.setHost(host);
                    profile.setPort(port);
                    profile.setType(type);
                }
                MyApplication.getInstance().storeProfiles(profileList);
                adapter.notifyDataSetChanged();
                dialog.dismiss();
            } catch (NumberFormatException e) {
                textError.setText(R.string.profile_error_port);
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
