package com.winlator;

import android.app.Activity;
import android.os.Bundle;
import android.view.View;

import androidx.annotation.Nullable;
import androidx.appcompat.app.AppCompatActivity;
import androidx.appcompat.widget.Toolbar;

/** Full container-profile editor opened from the S11 desktop Settings app. */
public class S11ContainerSettingsActivity extends AppCompatActivity {
    public static final String EXTRA_CONTAINER_ID = "s11_container_id";

    public void markContainerProfileSaved() {
        setResult(Activity.RESULT_OK);
    }

    @Override
    protected void onCreate(@Nullable Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        setContentView(R.layout.s11_container_settings_activity);

        Toolbar toolbar = findViewById(R.id.S11ContainerSettingsToolbar);
        setSupportActionBar(toolbar);
        if (getSupportActionBar() != null) getSupportActionBar().setDisplayHomeAsUpEnabled(true);
        toolbar.setNavigationOnClickListener((View view) -> finish());

        int containerId = getIntent().getIntExtra(EXTRA_CONTAINER_ID, 0);
        if (containerId <= 0) {
            finish();
            return;
        }
        if (savedInstanceState == null) {
            getSupportFragmentManager().beginTransaction()
                .replace(R.id.S11ContainerSettingsContent, new ContainerDetailFragment(containerId))
                .commit();
        }
    }
}
