package com.toshi.gpsselector;
/*****************************************************************************
*
*	GpsSelector:SettingsFragment -- NAVI本体GPSと外付けGPSユニットの自動切換え
*
*	外付けGPSユニットがINSしているときに
*	NAVIの現在値を外付けGPSユニットの値で置き換える
*
*	rev1.0	09.Oct.2021	initial revision by	Toshi
*	rev1.1	02.Mar.2022	画面のキャプチャを追加
*
*****************************************************************************/

import android.app.Activity;
import android.graphics.Point;
import android.os.Bundle;

import androidx.preference.CheckBoxPreference;
import androidx.preference.Preference;
import androidx.preference.PreferenceFragmentCompat;
import android.content.Context;
import android.content.Intent;
import android.content.SharedPreferences;
import android.util.Log;
import android.view.Display;
import android.widget.Toast;
import android.os.Build;
import androidx.preference.ListPreference;
import androidx.preference.PreferenceManager;
import androidx.preference.SwitchPreferenceCompat;
import android.bluetooth.BluetoothAdapter;
import android.bluetooth.BluetoothDevice;
import java.util.HashSet;
import java.util.Set;
import java.util.HashMap;
import android.hardware.usb.UsbDevice;
import android.hardware.usb.UsbManager;
import android.media.projection.MediaProjectionManager;
import android.util.DisplayMetrics;

//****************************************************************************
// 設定画面のフラグメント
//
public class SettingsFragment extends PreferenceFragmentCompat
			implements SharedPreferences.OnSharedPreferenceChangeListener,
			Preference.OnPreferenceClickListener {

	private static final String TAG = "●SettingsFragment";
	private final int REQUEST_PERMISSIONS_REQUEST_CODE = 1;
	private SharedPreferences sharedPref;
	private SharedPreferences.Editor sharedEdit;
	private BluetoothAdapter bluetoothAdapter = null;
	private ListPreference prefDevices;
	private SwitchPreferenceCompat prefSw, prefCaptureSw;
	private UsbManager usbManager;
	private Activity mActivity;
	private MediaProjectionManager mpManager;
	private Intent mResultData;
	private boolean fCapture;	// 画面記録スイッチ状態

	//------------------------------------------------------------------------
	// インスタンス初期設定時の処理
	//------------------------------------------------------------------------
	@Override
	public void onCreatePreferences(Bundle savedInstanceState,
				String rootKey) {
		Log.d(TAG,"onCreatePreferences");
		setPreferencesFromResource(R.xml.root_preferences, rootKey);
		mActivity = requireActivity();

		// SharedPreferencesを取得しておく
		sharedPref =
			PreferenceManager.getDefaultSharedPreferences(mActivity);

		// 変更した時のリスナーの設定
		sharedPref.registerOnSharedPreferenceChangeListener(this);
		sharedEdit = sharedPref.edit();
		prefDevices = (ListPreference)findPreference("DEVICE");
		prefDevices.setOnPreferenceClickListener(this);
		prefSw = (SwitchPreferenceCompat)findPreference("START_SW");
		prefCaptureSw = (SwitchPreferenceCompat)findPreference("CAPTURE_SW");

		// bluetoothアダプタの取得
		bluetoothAdapter = BluetoothAdapter.getDefaultAdapter();
		// USBマネージャの取得
		usbManager =
			(UsbManager)mActivity.getSystemService(Context.USB_SERVICE);
		// MediaProjectionマネージャの取得
		mpManager = (MediaProjectionManager)
					getActivity().getSystemService(
						Context.MEDIA_PROJECTION_SERVICE);

		// 作動要求SWオンでサービス未起動？
		if (prefSw.isChecked() && !MyService.IsServiceRunning()) {
			// サービスを起動
			StartService();
		}
	}
	//------------------------------------------------------------------------
	// パーミッション確認するintentを投げる(非推奨な手法ではあるが)
	//------------------------------------------------------------------------
	@SuppressWarnings("deprecation")
	private void MystartActivityForResult(Intent intent, int code) {
		startActivityForResult(intent, code);
	}
	//------------------------------------------------------------------------
	// 画面取得の判断を受け取る
	//------------------------------------------------------------------------
	@Override
	public void onActivityResult(int requestCode, int resultCode,
				Intent intent) {
		super.onActivityResult(requestCode, resultCode, intent);
		if (requestCode == REQUEST_PERMISSIONS_REQUEST_CODE) {
			if (resultCode != Activity.RESULT_OK) {
				ToastShow("許可を得られませんでした");
				prefCaptureSw.setChecked(false);
				return;
			}
//			ToastShow("許可を得ました");
			mResultData = intent;
			StartService();	// サービスへ伝える
		}
	}
	//------------------------------------------------------------------------
	// インスタンス作成時の処理
	//------------------------------------------------------------------------
	@Override
	public void onCreate(Bundle savedInstanceState) {
		Log.d(TAG,"onCreate");
		super.onCreate(savedInstanceState);
	}
	//------------------------------------------------------------------------
	// 画面(再)表示時の処理
	//------------------------------------------------------------------------
	@Override
	public void onResume() {
		Log.d(TAG,"onResume");
		super.onResume();

		// デバイスサマリーアップデート
		// (↓onCreatePreferencesでやるとクラッシュしてしまうのでここで)
		updateDevicePrefSummary();
	}
	//------------------------------------------------------------------------
	// 終了時処理
	//------------------------------------------------------------------------
	@Override
	public void onDestroy() {
		Log.d(TAG,"onDestroy");
		super.onDestroy();

		sharedPref.unregisterOnSharedPreferenceChangeListener(this);
	}
	//------------------------------------------------------------------------
	// デバイスサマリーアップデート
	//------------------------------------------------------------------------
	private void updateDevicePrefSummary() {
		String devname = "";
		String devadrs = sharedPref.getString("DEVICE", null);

		// 今bluetoothデバイスが選択されているなら
		if (BluetoothAdapter.checkBluetoothAddress(devadrs)) {
			// bluetoothのデバイス名
			devname = bluetoothAdapter.getRemoteDevice(devadrs).getName();
			sharedEdit.putInt("PRODUCT_ID", 0).commit();	// USBのIDをクリア
			sharedEdit.putInt("VENDOR_ID", 0).commit();
		} else {
			// USBのデバイス名
			devname = getSelectedDeviceSummaryUSB();
		}

		// デバイス名をサマリーにセット
		if (prefDevices != null) {
			prefDevices.setSummary(getString(
				R.string.pref_device_summary, devname));
		}
	}
	//------------------------------------------------------------------------
	// デバイスリストアップデート
	//------------------------------------------------------------------------
	private void updateDevicePrefList() {

		// 接続されているUSBデバイスを取得
		HashMap<String, UsbDevice> usbdev = usbManager.getDeviceList();

		// ペアになっているBluetoothデバイスを取得
		Set<BluetoothDevice> pairdev = new HashSet<>();
		if (bluetoothAdapter != null){
			pairdev = bluetoothAdapter.getBondedDevices();
		}

		String[] entryval = new String[usbdev.size() + pairdev.size()];
		String[] entries = new String[usbdev.size() + pairdev.size()];

		int i = 0;
		// USBデバイスの数だけ
		for (UsbDevice dev : usbdev.values()) {

			String devname = "";
			if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.LOLLIPOP) {
				String manuname = dev.getManufacturerName();
				if (manuname == null) {	// CH340はnullが返る
					manuname = "CH340";
				}
				devname = manuname + " " + dev.getProductName();
			} else {
					devname = "USB " + dev.getDeviceProtocol() + " " +
							dev.getDeviceName();
			}

			entryval[i] = dev.getDeviceName();
			entries[i] = devname;
			i++;
		}

		// ペアになったBluetoothデバイスの数だけ
		for (BluetoothDevice dev : pairdev) {
			// アドレスとデバイス名を取得
			Log.d(TAG, "device: " + dev.getName() + " -- " +
				dev.getAddress() + " " + dev.getBluetoothClass());
			entryval[i] = dev.getAddress();
			entries[i] = dev.getName();
			i++;
		}
		// ListPreferenceにアドレスとデバイス名を追加
		prefDevices.setEntryValues(entryval);
		prefDevices.setEntries(entries);

		// デバイスサマリーアップデート
		updateDevicePrefSummary();
	}
	//------------------------------------------------------------------------
	// 今選択されているUSBデバイス名を得る
	//------------------------------------------------------------------------
	private String getSelectedDeviceSummaryUSB() {
		int pid = sharedPref.getInt("PRODUCT_ID", 0);
		int vid = sharedPref.getInt("VENDOR_ID", 0);

		String devname = "";
		for (UsbDevice dev : usbManager.getDeviceList().values()) {
			if (dev.getVendorId() == vid && dev.getProductId() == pid) {

				if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.LOLLIPOP) {
					String manuname = dev.getManufacturerName();
					if (manuname == null && vid == 6790) {
						manuname = "CH340";	// CH340はnullが返る
					}
					devname = manuname + " " + dev.getProductName();
				} else {
					devname = "USB " + dev.getDeviceProtocol() + " " +
								dev.getDeviceName();
				}
				break;
			}
		}
		return devname;
	}
	//------------------------------------------------------------------------
	// デバイスリストクリック時のリスナー
	//------------------------------------------------------------------------
	@Override
	public boolean onPreferenceClick(Preference preference) {
		// いったんサービス終了
		sharedEdit.putBoolean("START_SW", false).apply();
		// デバイスリストアップデート
		updateDevicePrefList();

		return true;
	}
	//------------------------------------------------------------------------
	// プレファレンス変更時のリスナー
	//------------------------------------------------------------------------
	@Override
	public void onSharedPreferenceChanged(SharedPreferences sharedPreferences,
								 String key) {
		boolean val = false;
		if (key.equals("DEVICE")) {

			String devname = sharedPref.getString("DEVICE", null);
			if (devname != null && !devname.isEmpty() &&
				usbManager.getDeviceList().containsKey(devname)) {

				UsbDevice device = usbManager.getDeviceList().get(devname);
				sharedEdit.putInt("PRODUCT_ID", device.getProductId());
				sharedEdit.putInt("VENDOR_ID", device.getVendorId());
				sharedEdit.apply();
			}
			// デバイスサマリーアップデート
			updateDevicePrefSummary();
		}
		if (key.equals("START_SW")) {
			val = sharedPreferences.getBoolean(key, false);
			// サービス側で操作されたSWの状態を画面に反映させる
			prefSw.setChecked(val);
			if (!val) {
				StopService();	// サービスを停止
				return;
			}
		}
		if (key.equals("CAPTURE_SW")) {
			val = sharedPreferences.getBoolean(key, false);
			// サービス側で操作されたSWの状態を画面に反映させる
			prefCaptureSw.setChecked(val);
			fCapture = val;
			if (fCapture && !MyService.IsGetResult()) {
				// スクリーンキャプチャのパーミッションを要求
				if (mpManager != null) {
					Intent intent = mpManager.createScreenCaptureIntent();
					MystartActivityForResult(intent,
						REQUEST_PERMISSIONS_REQUEST_CODE);
				}
			}
		}

		// サービスに伝える
		StartService();
		// デバイスリストアップデート
		updateDevicePrefList();
	}
	//------------------------------------------------------------------------
	// サービスに情報を伝える
	//------------------------------------------------------------------------
	@SuppressWarnings("deprecation")
	private void StartService() {

		DisplayMetrics displayMetrics =getResources().getDisplayMetrics();
		getActivity().getWindowManager().getDefaultDisplay().
									getMetrics(displayMetrics);

		// 画面の縦横サイズとdpを取得(2分時にこれじゃないとうまくいかない)
		Display display = getActivity().getWindowManager().getDefaultDisplay();
		Point point = new Point(0, 0);
		display.getRealSize(point);	// 非推奨だが
		// 少し小さくしないとエラーで落ちる。
		int width = point.x * 80/100;
		int height = point.y * 80/100;
		int density = displayMetrics.densityDpi * 80/100;;

		Intent intent = 
			new Intent(mActivity.getApplication(), MyService.class);

		intent.putExtra("data", mResultData);
		intent.putExtra("height", height);
		intent.putExtra("width", width);
		intent.putExtra("dpi", density);
		intent.putExtra("capture", fCapture);

		// Serviceの開始
		// API 26 以上
		if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.O) {
			mActivity.startForegroundService(intent);
		} else {
			mActivity.startService(intent);
		}
		Log.d(TAG,"Start Service");
	}
	//------------------------------------------------------------------------
	// サービスを停止
	//------------------------------------------------------------------------
	private void StopService() {
		Intent intent = 
			new Intent(mActivity.getApplication(), MyService.class);
		// Serviceの停止
		mActivity.stopService(intent);
		Log.d(TAG,"Stop Service");
	}
//****************************************************************************
// 画面表示
//
	private static Toast tst;
	//------------------------------------------------------------------------
	// toast出力
	//------------------------------------------------------------------------
	private void ToastShow(String str) {
		tst = Toast.makeText(mActivity, str, Toast.LENGTH_SHORT);
		// 画面の中央に表示
		//tst.setGravity(Gravity.CENTER, 0, 0);
		tst.show();
	}
}
