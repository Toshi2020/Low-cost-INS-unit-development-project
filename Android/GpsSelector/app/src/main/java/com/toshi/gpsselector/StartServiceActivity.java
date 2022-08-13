package com.toshi.gpsselector;
/*****************************************************************************
*
*	GpsSelector:StartServiceActivity -- サービス起動用アクティビティ
*
*	rev1.0	09.Oct.2021	initial revision by	Toshi
*
*****************************************************************************/

import androidx.appcompat.app.AppCompatActivity;
import android.os.Bundle;
import android.content.Intent;
import android.os.Build;
import androidx.preference.PreferenceManager;
import android.content.SharedPreferences;

public class StartServiceActivity extends AppCompatActivity {

	private static final String TAG = "⦿StartService ";

	@Override
	protected void onCreate(Bundle savedInstanceState) {
		super.onCreate(savedInstanceState);
		setContentView(R.layout.activity_start_service);

		// 設定値を読む
		SharedPreferences sharedPref =
				PreferenceManager.getDefaultSharedPreferences(this);

		// 起動SWオンかつサービスが起動していない？
		if (sharedPref.getBoolean("START_SW", false) &&
				!MyService.IsServiceRunning()) {

			// サービス起動
			Intent intent = new Intent(this, MyService.class);
			// API 26 以上
			if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.O) {
				startForegroundService(intent);
			} else {
				startService(intent);
			}
		}
		finish();	// これで終了
	}
}
