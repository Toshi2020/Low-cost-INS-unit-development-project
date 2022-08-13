package com.toshi.gpslogview;
/*****************************************************************************
*
*	GpsLogView:MainActivity -- GpsSelectorのログをマップ表示
*
*	rev1.0	31.Aug.2022	initial revision by	Toshi
*
*****************************************************************************/

import androidx.appcompat.app.AppCompatActivity;
import androidx.core.app.ActivityCompat;
import androidx.core.content.ContextCompat;

import android.content.Context;
import android.content.SharedPreferences;
import android.graphics.Paint;
import android.graphics.PorterDuff;
import android.graphics.drawable.Drawable;
import android.os.Bundle;
import android.os.Handler;
import android.os.Looper;
import android.os.SystemClock;
import android.util.DisplayMetrics;
import android.util.Log;
import android.view.Gravity;
import android.view.View;
import android.widget.Button;
import android.widget.Switch;
import android.widget.TextView;
import android.widget.Toast;
import java.io.File;
import android.Manifest;
import android.content.pm.PackageManager;
import android.os.Environment;
import androidx.preference.PreferenceManager;

import java.io.BufferedReader;
import java.io.FileInputStream;
import java.io.InputStreamReader;
import java.util.ArrayList;
import java.util.Arrays;
import java.util.List;
import java.util.SortedMap;
import java.util.TreeMap;
// OSM関連
import org.osmdroid.api.IMapController;
import org.osmdroid.config.Configuration;
import org.osmdroid.util.GeoPoint;
import org.osmdroid.views.MapView;
import org.osmdroid.views.overlay.Marker;
import org.osmdroid.views.overlay.Polyline;
import org.osmdroid.views.overlay.ScaleBarOverlay;
import org.osmdroid.views.overlay.advancedpolyline.ColorMappingForScalar;
import org.osmdroid.views.overlay.advancedpolyline.PolychromaticPaintList;
import org.osmdroid.views.overlay.advancedpolyline.ColorMappingRanges;
import android.graphics.Color;

public class MainActivity extends AppCompatActivity
		implements FileSelectionDialog.OnFileSelectListener {

	private static final String TAG = "★MainActivity";
	private SharedPreferences sharedPref;
	private SharedPreferences.Editor sharedEdit;
	// 初期フォルダ
	private String m_strInitialDir =
				Environment.getExternalStorageDirectory().getPath();
	// ログファイル関連
	public static List<List<String>> Buffer = new ArrayList<>();
//	private final int DATAMAX = 21;	// 1行のデータ個数
	private final int DATAMAX = 31;	// 1行のデータ個数
	private TextView mTextView;
	// OSM関連
	private MapView mMapView = null;
	private IMapController mMapController;
	private final int REQUEST_PERMISSIONS_REQUEST_CODE = 1;
	private double mLat = 35.6796388;
	private double mLon = 139.7686040;
	private double mGpsLat = 35.6796388;
	private double mGpsLon = 139.7686040;
	private int mCol, mGpsCol;
	private float mZoom;
	private ScaleBarOverlay mScaleBarOverlay;
	private String LastFileName;
	private File OpenFile;
	private boolean fDot = true;
	private final float LINEWIDTH = 3.0f;	// 軌跡ライン描画太さ

	//------------------------------------------------------------------------
	// インスタンス作成時の処理
	//------------------------------------------------------------------------
	@Override
	protected void onCreate(Bundle savedInstanceState) {
		super.onCreate(savedInstanceState);
		// OSM初期値
		Configuration.getInstance().load(getApplicationContext(),
				PreferenceManager.getDefaultSharedPreferences(
					getApplicationContext()));
		setContentView(R.layout.activity_main);
		// プリファレンス
		sharedPref = PreferenceManager.getDefaultSharedPreferences(this);
		sharedEdit = sharedPref.edit();
		// プリファレンスから初期フォルダを読み込む
		String prefDir = sharedPref.getString("DIR", null);
		if (prefDir != null) {
			File DocDir = new File(prefDir);
			if (DocDir.exists()) {			// 存在するなら
				m_strInitialDir = prefDir;// それを初期フォルダとする
			}
		}
		// プリファレンスから表示スタイルを読む
		fDot = sharedPref.getBoolean("DOT", false);
		// 初期座標は東京駅
		mLat = (double)sharedPref.getLong("LASTLAT", 356796388) / 10000000d;
		mLon = (double)sharedPref.getLong("LASTLON", 1397686040) / 10000000d;
		mZoom = sharedPref.getFloat("LASTZOOM", 16.0f);

		// ボタン
		Button button = (Button)findViewById(R.id.button);
		button.setOnClickListener(new View.OnClickListener()
		{
			// ボタンが押されたときの処理
			@Override
			public void onClick(View view) {
				if (view.equals(button)) {
					DispDialog();	// ファイルオープンダイアログの表示
				}
			}
		});
		// スイッチ
		Switch switch1 = (Switch)findViewById(R.id.switch1);
		switch1.setChecked(fDot);
		switch1.setOnClickListener(new View.OnClickListener()
		{
			// スイッチ操作時の処理
			@Override
			public void onClick(View view) {
				if (view.equals(switch1)) {
					fDot = switch1.isChecked();	// スイッチの状態
					// 状態をプリファレンスに書き込む
					sharedEdit.putBoolean("DOT", fDot).apply();
					if (OpenFile != null) {		// 表示されていたら
						onFileSelect(OpenFile);	// 再表示する
					}
				}
			}
		});
		mTextView = (TextView)findViewById(R.id.textview);

		// OSM
		mMapView = (MapView)findViewById(R.id.mapView);	// リソース取得
		mMapView.setMultiTouchControls(true);	// マルチタッチでズーム可
		mMapController = mMapView.getController();	// Mapコントローラー取得
		GeoPoint centerPoint = new GeoPoint(mLat, mLon);
		mMapController.setCenter(centerPoint);	// Map中央の座標を設定
		mMapController.setZoom(mZoom);			// ズームレベル

		mScaleBarOverlay = new ScaleBarOverlay(mMapView);
		mScaleBarOverlay.setCentred(true);
		DisplayMetrics dm = getResources().getDisplayMetrics();
		// トップセンター
		mScaleBarOverlay.setScaleBarOffset(dm.widthPixels / 2, 40);
		// ボトムセンター
//		mScaleBarOverlay.setScaleBarOffset(dm.widthPixels / 2,
//								dm.heightPixels - (int)(60 * dm.density));
		// ボトムライト
//		mScaleBarOverlay.setScaleBarOffset(
//								dm.widthPixels - (int)(100 * dm.density),
//								dm.heightPixels - (int)(60 * dm.density));
//		mScaleBarOverlay.setLineWidth(3.0f);
		mMapView.getOverlays().add(mScaleBarOverlay);	// スケールバーの表示

		// 画面の回転により再起動されたら
		if (savedInstanceState != null) {
			// その時表示していたファイル名を取得
			LastFileName = savedInstanceState.getString("LastFile");
		}

		// メインスレッドにキュー設定
		// setContentViewで処理終了後メインスレッドが空になるので
		// キューに溜まっている処理が実行される
		mMapView.post(()-> {
			// 画面を回転する前にファイルを表示していたら
			if (LastFileName != null) {
				onFileSelect(new File(LastFileName));	// 再表示する
			}
		});
	}
	//------------------------------------------------------------------------
	// 初回表示時およびポーズからの復帰時
	//------------------------------------------------------------------------
	@Override
	public void onResume()
	{
		super.onResume();
		if (mMapView != null) {
			mMapView.onResume();
		}

		// 必要ならパーミッションを要求
		requestPermissionsIfNecessary(new String[] {
			Manifest.permission.ACCESS_FINE_LOCATION,
			Manifest.permission.READ_EXTERNAL_STORAGE,
			Manifest.permission.WRITE_EXTERNAL_STORAGE
		});

		if (OpenFile != null) {		// 表示されていたら
			onFileSelect(OpenFile);	// 再表示する
		}
	}
	//------------------------------------------------------------------------
	// onDestroyの前に呼び出される(画面の回転に対応するため)
	//------------------------------------------------------------------------
	@Override
	protected void onSaveInstanceState(Bundle outState) {
		super.onSaveInstanceState(outState);
		// 最後に開いていたファイル名を退避
		if (OpenFile != null) {
			outState.putString("LastFile", OpenFile.toString());
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
	// 必要ならパーミッション要求
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
	// ファイルオープンダイアログの表示
	//------------------------------------------------------------------------
	private FileSelectionDialog dlg;
	private void DispDialog() {
		// 既に開いていたら何もしない
		if (dlg != null && dlg.isShowing()) {
			return;
		}
		// ダイアログオブジェクト
		dlg = new FileSelectionDialog(this, this, "Log.*|NMEA.*", "txt");
		// ダイアログ表示
		dlg.show(new File(m_strInitialDir));
	}
	//------------------------------------------------------------------------
	// ファイルが選択されたときに呼び出される関数
	//------------------------------------------------------------------------
	public void onFileSelect(File file) {
		OpenFile = file;
		m_strInitialDir = file.getParent();
		// 選択されたときのフォルダをプリファレンスに書き込む
		sharedEdit.putString("DIR", m_strInitialDir).apply();
		// ログファイル読み込み
		ReadLogFile(OpenFile);
		// マップにライン追加
		AddLines();
		// ログファイル名を表示
		mTextView.setText(OpenFile.getName().toString());
	}
	//------------------------------------------------------------------------
	// ログファイル読み込み
	//		書き換え
	//		String	Buffer[][];	データ
	//------------------------------------------------------------------------
	private void ReadLogFile(File file) {

		// ストレージが読出しできるかチェック
		if (!isExternalStorageReadable()) {
			ToastShow("ストレージから読み出せません");
			finish();
			return;
		}
		Buffer.clear();	// 配列要素をクリア
		// ファイルが存在しなければ何もしない
		if (file == null || !file.exists()) {
			return;
		}
		try {
			BufferedReader br = new BufferedReader(
					new InputStreamReader(
						new FileInputStream(file), "SJIS"));
			String line;
			while ((line = br.readLine()) != null) {
				// モニターログまたはNMEAログでGGAセンテンス
				if (line.charAt(0) != '$' || line.contains("GGA")) {
					String[] rowdata = line.split(",", -1);	// 空文字も分割
					ArrayList<String> list = 
						new ArrayList<String>(Arrays.asList(rowdata));
					for (int i = rowdata.length; i < DATAMAX; i++) {
						list.add("");	// データが不足していたら""を追加(F/S)
					}
					Buffer.add(list);	// 一行分を追加
				}
			}
			br.close();	// ファイルクローズ
		} catch (Exception e) {
			ToastShow("データ読み出しエラー");
			finish();
		}
	}
	//------------------------------------------------------------------------
	// ストレージが読み書きできるかのチェック
	//------------------------------------------------------------------------
	private boolean isExternalStorageReadable() {
		String state = Environment.getExternalStorageState();
		return (Environment.MEDIA_MOUNTED.equals(state) ||
				Environment.MEDIA_MOUNTED_READ_ONLY.equals(state));
	}
	//------------------------------------------------------------------------
	// マップにライン追加
	//------------------------------------------------------------------------
	private void AddLines() {
		Integer len = Buffer.size();// データの個数
		if (len == 0) {			// データが存在しないなら
			return;				// 何もしない
		}
		// ドットの表示には時間がかかるのでメッセージを表示
		if (fDot) {
			ToastShow2("処理中(0/" + len.toString() + ")");
		}
		long tim = SystemClock.elapsedRealtime();

		// 座標の最大と最少取得のための変数
		double lonmin = 1000d, lonmax = -1000d;
		double latmin = 1000d, latmax = -1000d;

		// INS軌跡
		Polyline polyline = new Polyline(mMapView);
		// ポリラインの色 ShowAdvancedPolylineStyles.javaを参考にした
		Paint paint = new Paint();
		paint.setStrokeWidth(LINEWIDTH);
		paint.setAntiAlias(true);
		SortedMap<Float, Integer> crange = new TreeMap<>();
		int col0 = Color.parseColor("#00000000");	// 透過
		int col1 = Color.parseColor("#ff0000ff");	// GPS確定
		int col2 = Color.parseColor("#ffff0000");	// GPS精度悪化
		int col3 = Color.parseColor("#ff000000");	// GPSロスト
		crange.put(0.5f, col0);	// 透過
		crange.put(1.5f, col1);	// GPS確定
		crange.put(2.5f, col2);	// GPS精度悪化
		crange.put(3.5f, col3);	// GPSロスト
		ColorMappingRanges mapping = new ColorMappingRanges(crange, true);
		ColorMappingForScalar mappingforscalar =
						(ColorMappingForScalar)mapping;
		polyline.getOutlinePaintLists().add(
				new PolychromaticPaintList(paint, mapping, false));
		polyline.setInfoWindow(null);

		// INS軌跡
		Polyline gpolyline = new Polyline(mMapView);
		// ポリラインの色
		Paint gpaint = new Paint();
		gpaint.setStrokeWidth(LINEWIDTH);
		gpaint.setAntiAlias(true);
		SortedMap<Float, Integer> gcrange = new TreeMap<>();
		int gcol0 = Color.parseColor("#00000000");	// 透過
		int gcol1 = Color.parseColor("#ff008000");	// GPS確定
		int gcol2 = Color.parseColor("#44008000");	// GPSロスト
		gcrange.put(0.5f, gcol0);	// 透過
		gcrange.put(1.5f, gcol1);	// GPS確定
		gcrange.put(2.5f, gcol2);	// GPSロスト
		ColorMappingRanges gmapping = new ColorMappingRanges(gcrange, true);
		ColorMappingForScalar gmappingforscalar =
						(ColorMappingForScalar)gmapping;
		gpolyline.getOutlinePaintLists().add(
				new PolychromaticPaintList(gpaint, gmapping, false));
		gpolyline.setInfoWindow(null);

		mMapView.getOverlays().clear();	// 以前の表示を消去

		boolean flog = false;			// ファイルフォーマット種類=NMEA
		Integer i;
		for (i = 0; i < len; i++) {	// データの数だけ
			String s1 = Buffer.get(i).get(0);	// バッファデータの先頭
			if (s1.charAt(0) == '$') {
				NmeaEncode(i);		// NMEAエンコード
			} else {
				LogEncode(i);		// Logデータエンコード
				flog = true;		// Logフォーマット
			}
			if (mLat != 0.0 && mLon != 0.0)
			{
				GeoPoint gp = new GeoPoint(mLat, mLon);
				latmax = Math.max(mLat, latmax);	// 最大緯度
				lonmax = Math.max(mLon, lonmax);	// 最大経度
				latmin = Math.min(mLat, latmin);	// 最小緯度
				lonmin = Math.min(mLon, lonmin);	// 最小経度
				polyline.addPoint(gp);				// INS軌跡を作成
				mappingforscalar.add((float)mCol);	// カラーを設定

				GeoPoint gp2 = new GeoPoint(mGpsLat, mGpsLon);
				gpolyline.addPoint(gp2);			// GPS軌跡を作成
				gmappingforscalar.add((float)mGpsCol);// カラーを設定

				// ドットの数が多いと極端に時間がかかるので表示を間引く
				boolean fd = len < 20000 || i % 2 == 0;
				// DOT表示かつLogフォーマットなら
				int col;
				Drawable drawable2;
				if (fDot && flog && fd) {
					// ドットの表示には時間がかかるのでメッセージを表示
					long t = SystemClock.elapsedRealtime();
					if (t - tim >= 1000) {
						tim = t;
						ToastOff();
						ToastShow2("処理中(" + i.toString() + "/" + 
									len.toString() + ")");
					}
					// GPSのドットの色
					if (mGpsCol == 1) {	// GPS確定
						col = gcol1;
						drawable2 = getResources().getDrawable(
							R.drawable.ic_baseline_brightness_1_24_20);
					} else  {			// GPSロスト
						col = gcol2;
						drawable2 = getResources().getDrawable(
							R.drawable.ic_baseline_brightness_1_24_21);
					}
					// GPSのドットを表示
					drawable2.setColorFilter(col, PorterDuff.Mode.SRC_IN);
					Marker marker2 = new Marker(mMapView);		// マーカー
					marker2.setAnchor(
							Marker.ANCHOR_CENTER, Marker.ANCHOR_CENTER);
					marker2.setIcon(drawable2);
					marker2.setInfoWindow(null);
					marker2.setPosition(gp2);
					mMapView.getOverlays().add(marker2);
				}
				// DOT表示なら
				Drawable drawable;
				if (fDot && fd) {
					// INSのドットの色
					if (mCol == 1) {		// GPS確定
						col = col1;
						drawable = getResources().getDrawable(
								R.drawable.ic_baseline_brightness_1_24);
					} else if (mCol == 2) {	// GPS精度悪化
						col = col2;
						drawable = getResources().getDrawable(
								R.drawable.ic_baseline_brightness_1_24_1);
					} else {				// GPSロスト
						col = col3;
						drawable = getResources().getDrawable(
								R.drawable.ic_baseline_brightness_1_24_2);
					}
					// INSのドットを表示
					drawable.setColorFilter(col, PorterDuff.Mode.SRC_IN);
					Marker marker = new Marker(mMapView);		// マーカー
					marker.setAnchor(
							Marker.ANCHOR_CENTER, Marker.ANCHOR_CENTER);
					marker.setIcon(drawable);
					marker.setInfoWindow(null);
					marker.setPosition(gp);
					mMapView.getOverlays().add(marker);
				}
			}
		}
		// ドットの表示には時間がかかるのでメッセージを表示
		if (fDot && flog) {
			ToastOff();
			ToastShow2("処理中(" + len.toString() + "/" + 
							len.toString() + ")");
		}
		// 中心座標を算出
		double clat = (latmax + latmin) / 2d;
		double clon = (lonmax + lonmin) / 2d;
		GeoPoint cp = new GeoPoint(clat, clon);
		mMapController.setCenter(cp);	// Map中央の座標を設定
		// 軌跡全体がViewに収まるようにズーム
		double lat = latmax - latmin;	// 緯度の幅
		double lon = lonmax - lonmin;	// 経度の幅
		float zoom = 30;
		for (int j = 30; j > 0; j--) {
			// ズームを20から5まで0.5刻みに減らす
			zoom = (float)j * 0.5f + 5f;
			mMapController.setZoom(zoom);
			// 表示幅
			double latspan = mMapView.getLatitudeSpanDouble();
			double lonspan = mMapView.getLongitudeSpanDouble();
			// 表示幅以内に収まったらそこまで
			if (lat < latspan * 0.95f && lon < lonspan * 0.95f) {
				break;
			}
		}
		// Logフォーマットなら
		if (flog) {
			mMapView.getOverlays().add(gpolyline);		// GPSラインを描画
		}
		mMapView.getOverlays().add(polyline);			// INSラインを描画
		mMapController.setCenter(cp);	// Map中央の座標を再度設定
		mMapView.getOverlays().add(mScaleBarOverlay);	// スケールバーの表示
		mMapView.invalidate();			// 念のため再描画

		// 座標とズーム値をプリファレンスに書き込む
		sharedEdit.putLong("LASTLAT", (long)(clat * 10000000d)).apply();
		sharedEdit.putLong("LASTLON", (long)(clon * 10000000d)).apply();
		sharedEdit.putFloat("LASTZOOM", zoom).apply();

		ToastOff();
	}

	//------------------------------------------------------------------------
	// Logファイルエンコード
	//------------------------------------------------------------------------
	private void LogEncode(int i) {

		// データの数が足らないなら
//		if (Buffer.get(i).size() != DATAMAX) {
//			return;	// ここまで
//		}
		// INSデータ
		String sLon = Buffer.get(i).get(8);
		String sLat = Buffer.get(i).get(9);
		String sStat = Buffer.get(i).get(20);
		// データが空なら
		if (sLon == null || sLat == null || sStat == null ||
			sLon.equals("") || sLat.equals("") || sStat.equals("") ||
			sLon.equals("999999999") || sLat.equals("999999999")) {
			return;	// ここまで
		}
		try {
			mLon = Double.parseDouble(sLon) / 10000000d;
			mLat = Double.parseDouble(sLat) / 10000000d;
			int stat = Integer.parseInt(sStat);
			if ((stat & 0x02) != 0) {	// GPSがFIXしている
				mCol = 1;
				mGpsCol = 1;
			} else {					// GPSがロストしている
				mCol = 3;
				mGpsCol = 2;
			}
			if ((stat & 0x04) != 0) {	// GPSはFIXしているが精度が悪い
				mCol = 2;
				mGpsCol = 1;
			}
		} catch (Exception e) { }

		// GPSデータ
		sLon = Buffer.get(i).get(6);
		sLat = Buffer.get(i).get(7);
		// データが空なら
		if (sLon == null || sLat == null ||
			sLon.equals("") || sLat.equals("") ||
			sLon.equals("999999999") || sLat.equals("999999999")) {

				mGpsLon = mLon;
				mGpsLat = mLat;
				return;
		}
		try {
			mGpsLon = Double.parseDouble(sLon) / 10000000d;
			mGpsLat = Double.parseDouble(sLat) / 10000000d;
		} catch (Exception e) { }
	}
	//------------------------------------------------------------------------
	// NMEAファイルエンコード
	//------------------------------------------------------------------------
	private void NmeaEncode(int i) {
		mLon = parseNmeaLongitude(Buffer.get(i).get(4), Buffer.get(i).get(5));
		mLat = parseNmeaLatitude(Buffer.get(i).get(2), Buffer.get(i).get(3));
		mGpsLon = mLon;
		mGpsLat = mLat;
		String sSat = Buffer.get(i).get(7);
		if (sSat == null || sSat.equals("")) {
			sSat = "0";
		}
		try {
			int sat = Integer.parseInt(sSat);
			mGpsCol = 1;	// NMEA描画では使わないが一応セットしておく
			if (sat <= 1) {			// GPSがロストしている
				mCol = 3;
				mGpsCol = 2;
			} else if (sat == 2) {	// GPSはFIXしているが精度が悪い
				mCol = 2;
			} else {				// GPSがFIXしている
				mCol = 1;
			}
		} catch (Exception e) { }
	}
//****************************************************************************
// NMEAパーサーヘルパー
// https://github.com/freshollie/UsbGps4Droidより
//
	//------------------------------------------------------------------------
	// 緯度を[deg]に変換
	//------------------------------------------------------------------------
	private double parseNmeaLatitude(String lat, String orientation) {
		double latitude = 0.0;
		if (lat != null && orientation != null && !lat.equals("") &&
				!orientation.equals("")) {
			double temp1 = Double.parseDouble(lat);
			double temp2 = Math.floor(temp1 / 100);
			double temp3 = (temp1 / 100 - temp2) / 0.6;
			if (orientation.equals("S")) {
				latitude = -(temp2 + temp3);
			} else if (orientation.equals("N")) {
				latitude = (temp2 + temp3);
			}
		}
		return latitude;
	}
	//------------------------------------------------------------------------
	// 経度を[deg]に変換
	//------------------------------------------------------------------------
	private double parseNmeaLongitude(String lon, String orientation) {
		double longitude = 0.0;
		if (lon != null && orientation != null && !lon.equals("") &&
				!orientation.equals("")) {
			double temp1 = Double.parseDouble(lon);
			double temp2 = Math.floor(temp1 / 100);
			double temp3 = (temp1 / 100 - temp2) / 0.6;
			if (orientation.equals("W")) {
				longitude = -(temp2 + temp3);
			} else if (orientation.equals("E")) {
				longitude = (temp2 + temp3);
			}
		}
		return longitude;
	}
//****************************************************************************
// 画面表示
//
	private static Toast tst;
	//------------------------------------------------------------------------
	//	toast出力
	//------------------------------------------------------------------------
	private void ToastShow(String str) {
		tst = Toast.makeText(this, str, Toast.LENGTH_LONG);
		tst.show();
	}
	//------------------------------------------------------------------------
	// 別スレッドでtoast出力
	// https://qiita.com/glayash/items/c75a670e2c11938a2bdc
	//------------------------------------------------------------------------
	private void ToastShow2(String str) {
		new ToastThread(this, str).start();
	}
	private static class ToastThread extends Thread {
		final private Context context;
		final private CharSequence message;
		private ToastThread(Context context, CharSequence message){
			this.context = context.getApplicationContext();
			this.message = message;
		}
		@Override
		public void run() {
			Looper.prepare();

			tst = Toast.makeText(context, message, Toast.LENGTH_LONG);
			View view = tst.getView();
			view.setBackgroundColor(Color.rgb(0, 255, 0));
			// 画面の中央に表示
			tst.setGravity(Gravity.CENTER, 0, 0);
			tst.show();
			Looper.loop();
		}
	}
	//------------------------------------------------------------------------
	//	toast消去
	//------------------------------------------------------------------------
	private void ToastOff() {
		if (tst != null) {
			tst.cancel();
			tst = null;
		}
	}
}
