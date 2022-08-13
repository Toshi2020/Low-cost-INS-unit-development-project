package com.toshi.gpsselector;
/*****************************************************************************
*
*	GpsSelector:MainActivity -- NAVI本体GPSと外付けGPSユニットの自動切換え
*
*	外付けGPSユニットがINSしているときに
*	NAVIの現在値を外付けGPSユニットの値で置き換える
*
*	rev1.0	09.Oct.2021	initial revision by	Toshi
*	rev1.1	02.Mar.2022	画面のキャプチャを追加
*
*****************************************************************************/

import androidx.appcompat.app.AppCompatActivity;
import androidx.core.app.ActivityCompat;
import androidx.core.content.ContextCompat;

import android.content.pm.PackageManager;
import android.os.Bundle;
import android.util.Log;
import android.view.Gravity;
import android.view.View;
import android.view.ViewTreeObserver;
import android.widget.TextView;
import android.widget.Button;
import android.widget.Toast;
import android.os.Handler;
import android.Manifest;
import java.util.ArrayList;
import androidx.preference.PreferenceManager;
import org.osmdroid.api.IMapController;
import org.osmdroid.config.Configuration;
import org.osmdroid.util.GeoPoint;
import org.osmdroid.views.MapView;
import org.osmdroid.views.overlay.Marker;
import org.osmdroid.views.overlay.ScaleBarOverlay;
import android.content.SharedPreferences;

public class MainActivity extends AppCompatActivity {

	private static final String TAG = "★MainActivity";
	private final int REQUEST_PERMISSIONS_REQUEST_CODE = 1;
	private TextView mTextExtGps;
	private Button mButton;
	private final Handler mTimerHandler = new Handler();
	private boolean fLogReq;
	// Map関連
	private static MapInfo mMapInfo;
	private MapView mMapView = null;
	private IMapController mMapController;
	private MapView map = null; 
	private Marker mMarker, mMarker2;
	private ScaleBarOverlay mScaleBarOverlay;
	private static double Latz, Lonz;
	private static float Dirz;
	private static int Satz;
	private float Zoom;
	private static float Zoomz;
	private int AfterTapTime = 1000;

	//------------------------------------------------------------------------
	// インスタンス作成時の処理
	//------------------------------------------------------------------------
	@Override
	protected void onCreate(Bundle savedInstanceState) {
		super.onCreate(savedInstanceState);

		// OpenStreetMap設定
		Configuration.getInstance().load(getApplicationContext(),
				PreferenceManager.getDefaultSharedPreferences(
					getApplicationContext()));

		setContentView(R.layout.activity_main);
		// 設定用画面に置き換え
		if (savedInstanceState == null) {
			getSupportFragmentManager()
					.beginTransaction()
					.replace(R.id.settings, new SettingsFragment())
					.commit();
		}

		// リソースのID取得
		mTextExtGps = (TextView)findViewById(R.id.textViewExtGps);
		mButton = (Button)findViewById(R.id.button);
		mButton.setOnClickListener(new View.OnClickListener() {
			@Override
			public void onClick(View v) {
				if (AfterTapTime >= 5)	// ダブルクリック防止
				{
					if (fLogReq) {
						fLogReq = false;
						MyService.ReqLogStop();
					} else {
						fLogReq = true;
						MyService.ReqLogStart();
					}
				}
				AfterTapTime = 0;
			}
		});

		// OpenStreetMap初期化
		mMapView = (MapView)findViewById(R.id.mapView);	// リソース取得
		mMapView.setMultiTouchControls(true);		// マルチタッチでズーム可
		mMapController = mMapView.getController();	// Mapコントローラー取得
		mMapController.setZoom(17.9f);	// ズームレベル(18だと道幅が狭くなる)
		// 初期位置は東京駅とする
		GeoPoint centerPoint = new GeoPoint(35.6796388d, 139.7686040d);
		mMapController.setCenter(centerPoint);	// Map中央の座標を設定
		mMarker = new Marker(mMapView);			// マーカー
		mMarker2 = new Marker(mMapView);
		mScaleBarOverlay = new ScaleBarOverlay(mMapView);	// スケールバー
		mScaleBarOverlay.setCentred(true);

		// スケールバーの表示位置
		// onWindowFocusChangedだと2画面からの復帰時にうまくいかない
		ViewTreeObserver observer = mMapView.getViewTreeObserver();
		observer.addOnGlobalLayoutListener(
		new ViewTreeObserver.OnGlobalLayoutListener() {
			@Override
			public void onGlobalLayout() {
				// Viewが作られたらスケールバーをトップセンターに置く
				mScaleBarOverlay.setScaleBarOffset(
								mMapView.getWidth() / 2, 10);
				// スケールバーの表示
				mMapView.getOverlays().add(mScaleBarOverlay);
			}
		});

		// 画面キャプチャはオフ
		SharedPreferences pref = PreferenceManager.
									getDefaultSharedPreferences(this);
		SharedPreferences.Editor ed = pref.edit();
		ed.putBoolean("CAPTURE_SW", false).apply();

		// タイマー開始
		mTimerHandler.postDelayed(TimerTask, 100);
	}
	//------------------------------------------------------------------------
	// 初回表示時およびポーズからの復帰時
	//------------------------------------------------------------------------
	@Override
	protected void onResume()
	{
		super.onResume();
		if (mMapView != null) {
			mMapView.onResume();
		}
		try {
			// 必要なパーミッションを要求
			requestPermissionsIfNecessary(new String[] {
				Manifest.permission.ACCESS_FINE_LOCATION,
				Manifest.permission.WRITE_EXTERNAL_STORAGE
			});
		} catch(Exception e){
		}
	}
	//------------------------------------------------------------------------
	// ポーズ時
	//------------------------------------------------------------------------
	@Override
	public void onPause() {
		super.onPause();
		if (mMapView != null) {
			mMapView.onPause();
		}
	}
	//------------------------------------------------------------------------
	// パーミッション要求結果の受け取り
	//------------------------------------------------------------------------
	@Override
	public void onRequestPermissionsResult(int requestCode,
					String[] permissions, int[] grantResults) {
		super.onRequestPermissionsResult(
				requestCode, permissions, grantResults);
		ArrayList<String> permissionsToRequest = new ArrayList<>();
		for (int i = 0; i < grantResults.length; i++) {
			permissionsToRequest.add(permissions[i]);
		}
		if (permissionsToRequest.size() > 0) {
			ActivityCompat.requestPermissions(
				this,
				permissionsToRequest.toArray(new String[0]),
				REQUEST_PERMISSIONS_REQUEST_CODE);
		}
	}
	//------------------------------------------------------------------------
	// 必要なパーミッションを要求
	//------------------------------------------------------------------------
	private void requestPermissionsIfNecessary(String[] permissions) {
		ArrayList<String> permissionsToRequest = new ArrayList<>();
		for (String permission : permissions) {
			 if (ContextCompat.checkSelfPermission(this, permission)
					!= PackageManager.PERMISSION_GRANTED) {
				permissionsToRequest.add(permission);
			}
		}
		if (permissionsToRequest.size() > 0) {
			ActivityCompat.requestPermissions(
				this,
				permissionsToRequest.toArray(new String[0]),
				REQUEST_PERMISSIONS_REQUEST_CODE);
		}
	}
	//------------------------------------------------------------------------
	// 終了時処理
	//------------------------------------------------------------------------
	@Override
	protected void onDestroy() {
		super.onDestroy();
		Log.d(TAG,"onDestroy");

		MyService.ReqLogStop();	// ログ停止

		// タイマー終了
		mTimerHandler.removeCallbacks(TimerTask);
	}
//****************************************************************************
// タイマー
//
	//------------------------------------------------------------------------
	// タイマー割込み処理
	//------------------------------------------------------------------------
	private final Runnable TimerTask = new Runnable() {

		@Override
		public void run() {
			boolean fgpsdisp = false;

			if (AfterTapTime < 30000)	// ボタンタップ後の時間
			{
				AfterTapTime++;
			}
			String s = MyService.GetStrIns();	// 表示文字列を取得
			if (!s.equals("")) {
				mTextExtGps.setText(s);			// データ表示
			}
			long lin = MyService.GetLogLine();	// ログした行数
			s = String.valueOf(lin);
			if (MyService.IsLogging()) {
				mButton.setText("\n■\t" + s + "\n");	// 行数表示
			} else {
				mButton.setText("\n●Log開始\n");
			}

			// 画面がオフしたら
			if (MyService.IsScreenOff()) {
				finish();	// メインアクティビティは終了させておく
			}

			// Map表示更新
			try {
			  mMapInfo = MyService.GetMapInfo();	// Map表示用データを取得
			  // データが更新されたら
			  if ((Latz != mMapInfo.lat || Lonz != mMapInfo.lon ||
					Dirz != mMapInfo.dir || Satz != mMapInfo.sat) &&
					mMapInfo.lat != 0.0d && mMapInfo.lon != 0.0d){
				// Map中央の座標
				GeoPoint centerPoint = 
							new GeoPoint(mMapInfo.lat, mMapInfo.lon);
				// GPSの座標(仮にINSと同じに)
				GeoPoint gpspoint = new GeoPoint(mMapInfo.lat, mMapInfo.lon);
				// GPS座標が存在するなら
				if (mMapInfo.gpslat > 0.0d) {
					gpspoint.setCoords(mMapInfo.gpslat, mMapInfo.gpslon);
					fgpsdisp = true;
				}
				mMapController.setCenter(centerPoint);// Map移動
				// 現在地マーカーをリセットする
				if (mMarker.getPosition() != null) {// 現在地マーカーがあれば
					mMarker.remove(mMapView);	// 現在地マーカーを削除する
				}
				if (mMarker2.getPosition() != null) {// GPSマーカーがあれば
					mMarker2.remove(mMapView);	// GPSマーカーを削除する
				}
				mMarker.setPosition(centerPoint);	// Map中央に表示
				mMarker2.setPosition(gpspoint);		// GPS位置に表示
				// 回転の中央を設定
				mMarker.setAnchor(Marker.ANCHOR_CENTER, Marker.ANCHOR_CENTER);
				mMarker2.setAnchor(Marker.ANCHOR_CENTER,Marker.ANCHOR_CENTER);
				// ズームレベルの決定
				// ズームレベル最大、最小、最小となる車速
				float zmax = 17.9f, zmin = 16.9f, vmax = 100.0f;
				float v = his(mMapInfo.speed);	// 車速にヒスを入れる
				if (v <= 0.0) {
					Zoom = zmax;
				} else if (v >= vmax) {
					Zoom = zmin;
				} else {
					Zoom = zmax - (zmax - zmin) * v / vmax;
				}
				if (Zoom != Zoomz) {
					mMapController.setZoom(Zoom);
				}

				// アイコンを設定
				switch (mMapInfo.sat) {
					case 0:	// GPSロスト中
					case 1:	// GPSロスト中
						mMarker.setIcon(getResources().getDrawable(
							R.drawable.ic_baseline_navigation_40_black));
						break;
					case 2:	// GPS FIX中だが疑わしい
						mMarker.setIcon(getResources().getDrawable(
							R.drawable.ic_baseline_navigation_40_red));
						break;
					default:	// GPS FIX中かつ信用できる
						mMarker.setIcon(getResources().getDrawable(
							R.drawable.ic_baseline_navigation_40));
				}
				mMarker2.setIcon(getResources().getDrawable(
							R.drawable.ic_baseline_navigation_40_green));
				// マーカーの角度(右が負)
				mMarker.setRotation(-mMapInfo.dir);
				mMarker2.setRotation(-mMapInfo.gpsdir);
				mMapView.getOverlays().clear();	// 以前の表示を消去
				// マーカーを表示
				if (fgpsdisp) {
					mMapView.getOverlays().add(mMarker2);
				}
				mMapView.getOverlays().add(mMarker);
				// スケールバーの表示
				mMapView.getOverlays().add(mScaleBarOverlay);
			  }
			  Latz = mMapInfo.lat;
			  Lonz = mMapInfo.lon;
			  Dirz = mMapInfo.dir;
			  Satz = mMapInfo.sat;
			  Zoomz = Zoom;
			} catch(Exception e){	// ありえない座標だった場合のF/S
				ToastShow("OSM error");
			}

			// タイマー再開
			mTimerHandler.postDelayed(this, 100);
		}
	};
	// 車速にヒスを入れる
	static float xz;
	private float his(float x) {
		float hisx = 5.0f;	// ヒスの幅
		if (x >= xz + hisx)
		{
			xz += hisx;
		} else if (x <= xz - hisx) {
			xz -= hisx;
		}
		if (xz < 0) xz = 0;
		return xz;
	}
//****************************************************************************
// 画面表示
//
	private static Toast tst;
	//------------------------------------------------------------------------
	// toast出力
	//------------------------------------------------------------------------
	private void ToastShow(String str) {
		tst = Toast.makeText(this, str, Toast.LENGTH_SHORT);
		// 画面の中央に表示
		tst.setGravity(Gravity.CENTER, 0, 0);
		tst.show();
	}
}
