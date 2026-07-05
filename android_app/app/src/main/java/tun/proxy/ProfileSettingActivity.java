package tun.proxy;

import android.os.Bundle;
import android.view.LayoutInflater;
import android.view.View;
import android.widget.ArrayAdapter;
import android.widget.EditText;
import android.widget.ListView;
import android.widget.Spinner;
import android.widget.TextView;
import android.widget.Toast;

import androidx.appcompat.app.AlertDialog;
import androidx.appcompat.app.AppCompatActivity;
import androidx.appcompat.widget.Toolbar;

import com.google.android.material.floatingactionbutton.FloatingActionButton;

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
            @Override
            public View getView(int position, View convertView, android.view.ViewGroup parent) {
                if (convertView == null) {
                    convertView = LayoutInflater.from(getContext()).inflate(R.layout.item_profile, parent, false);
                }
                ProfileItem item = getItem(position);
                if (item != null) {
                    TextView textName = convertView.findViewById(R.id.text_name);
                    TextView textHostPort = convertView.findViewById(R.id.text_host_port);
                    TextView textType = convertView.findViewById(R.id.text_type);

                    textName.setText(item.getName());
                    textHostPort.setText(String.format(Locale.ROOT, "%s:%d", item.getHost(), item.getPort()));
                    textType.setText(item.getType().name());
                }
                return convertView;
            }
        };
        listView.setAdapter(adapter);

        listView.setOnItemClickListener((parent, view, position, id) -> {
            showProfileEditDialog(profileList.get(position), position);
        });

        listView.setOnItemLongClickListener((parent, view, position, id) -> {
            new AlertDialog.Builder(this)
                    .setTitle(R.string.profile_delete_title)
                    .setMessage(R.string.profile_delete_msg)
                    .setPositiveButton(android.R.string.ok, (dialog, which) -> {
                        profileList.remove(position);
                        MyApplication.getInstance().storeProfiles(profileList);
                        adapter.notifyDataSetChanged();
                    })
                    .setNegativeButton(android.R.string.cancel, null)
                    .show();
            return true;
        });

        FloatingActionButton fab = findViewById(R.id.fab_add);
        fab.setOnClickListener(v -> showProfileEditDialog(null, -1));
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
