/*****************************************************************************
*
*	ExtGPS.ino -- 外部GPSユニット(4Xユニット用)
*
*	rev1.0	2019/01/25	initial revision by	Toshi
*	rev1.1	2019/02/22	車速と方位の位相をGPSにそろえるためにフィルタ追加
*	rev1.2	2019/03/04	GPS RMCで常時方位を設定する(停止すると空になるので)
*	rev1.3	2019/03/29	DMPやめる
*	rev2.0	2019/06/11	コンパスなし、ヨーレートFLOAT化、カルマンフィルタ使用
*	rev2.1	2019/06/30	HILS用ビルド対応
*	rev3.0	2019/12/27	コンパス復活トライ→やはり使えない
*	rev4.0	2021/04/28	全体を再構築
*	rev5.0	2021/09/15	4Xユニット専用に再構築
*	rev5.1	2021/10/28	GPS確定からしばらくはdoubt判定しない
*	rev5.2	2022/02/07	doubt判定は衛星のSNRで行う
*	rev5.3	2022/04/08	NMEAパーサーを自前で
*	rev5.4	2022/08/10	NMEA送信時にGPS確定時にもINS位置位置を送る
*
*	I2Cライブラリはこちらからコピー→https://github.com/jrowberg/i2cdevlib
*	ソフトシリアルは→https://www.pjrc.com/teensy/td_libs_AltSoftSerial.html
*
*****************************************************************************/
#define _HILS 0		// HILS用ビルドなら1
#define _HEADER 0	// モニタヘッダー出力するなら1(0でメモリ節約)
#define _NMEAINS 1	// NMEA送信時に常にINSデータで置き換えるなら1

// テストモードでビルドするときに0以外の値を設定
// _TESTMODE = 1:30m/h以上なら衛星未確定とする
#define _TESTMODE 0

#include <string.h>
#include <stdlib.h>
#include <math.h>
#include <Wire.h>			// I2Cライブラリ
#include <EEPROM.h>			// EEPROMライブラリ
#include <avr/wdt.h>		// WDT
#include "I2Cdev.h"
#include "MPU6050_kai.h" 	// ジャイロライブラリ
#include "AltSoftSerial.h"	// 割り込みによるソフトシリアルライブラリ
#include "ulib.h"			// ユーザーライブラリ

// ★MPU6050 YawGセンサ
// ↓センサオフセット(MPU6050_calibration.inoを焼いて実行)
#define AOFSTX -242
#define AOFSTY -1185
#define AOFSTZ 1561
#define GOFSTX -14
#define GOFSTY 41
#define GOFSTZ 9
#define CALTEMP 24.75	// キャリブレーション時の温度
// ↓printTempYawG()を実行し、結果をEXCELで回帰分析して傾きを取得
#define SLOPE_YAW -1.3527291E-02
#define SLOPE_G 1.4434167E-04
// ↓実車で8の字走行した結果が元の場所に戻るように設定
#define YAWGAIN 1.001	// ヨーレートセンサスケールゲイン

#define Sprint(x) Serial.print(x)
#define Sprintc(x) Serial.print(x),Serial.write(',')
#define Sprintf(x, y) Serial.print(x, y)
#define Sprintfc(x, y) Serial.print(x, y),Serial.write(',')
#define Sprintln(x) Serial.println(x)
#define Sprintfln(x, y) Serial.println(x, y)
// NMEA出力先
#define GpsPrint(x) Serial.print(x)
#define GpsPrintln(x) Serial.println(x)

// 定数
#define L1e7 10000000L
#define F1e7 10000000.0
#define G1 9.80665
#define GPSFILT 0.17		// GPSの遅れを補償するフィルタ定数(今回の値の重み)
#define YAWV 10.0			// GPS方位が得られる車速[km/h]
#define CURVEYAW 2.0		// カーブ判定ヨーレート[deg/s]
#if _HILS	// HILS用ビルドなら
 #define POWERONDELAYC 0	// 通電直後の安定待ち時間[s]x10
#else
 #define POWERONDELAYC 30	// 通電直後の安定待ち時間[s]x10
#endif //_HILS

/********* グローバル変数 *********/
volatile FLOAT bug,bug1,bug2,bug3,bug4,bug5,bug6,bug7,bug8,bug9;
SHORT AfterPowerOnTime;		// 通電後経過時間
ULONG LastTime;				// ループ時間計測用
FLOAT Dt;					// サンプリングタイム[s]
FLOAT TxDt;

// ソフトシリアル(割り込み版)を使用する
// PIN8=Rx, PIN9=Tx, PIN10のPWMと16bitタイマー(Timer1)使えない
AltSoftSerial Serial2;		// ソフトシリアルインスタンス

// センサ関連
MPU6050 Mpu;		// MPU6050クラスインスタンス
SHORT AccelXRaw, AccelYRaw, AccelZRaw;	// Gセンサ生データ
SHORT PitchRaw, RollRaw, YawRaw;		// ジャイロ生データ
FLOAT YawRate;		// オフセット補正後のヨーレート(右が正)[deg/s]
FLOAT YawOffset;	// ヨーレートオフセット[deg/s]
SHORT TempRaw;		// 温度センサ生データ
FLOAT Temperature;	// 温度[deg]
FLOAT YawRate0;		// 温度補償後のヨーレート(右が正)[deg/s]
FLOAT AccelY;		// 温度補償後の前後Gセンサ加速度[G]

// 車体関連
//#define VSPCONST 1412873	// VSP定数デフォルト値 60*60/(637*4)*1000*1000
#define VSPCONST 1428500	// VSP定数実測値(タイヤの動半径補正の影響)
ULONG VspConst = VSPCONST;	// 車速変換係数
volatile ULONG VspTime;		// VSPパルス周期[μs]
volatile SHORT VspItvl;		// VSP割り込みからの経過時間
volatile ULONG VspCountZ;	// 前回VSP割り込み時のカウンタ
FLOAT Vreal;				// 車速[km/h]
FLOAT VrealFilt;			// フィルタした車速[km/h]
FLOAT ArealFilt;			// フィルタした加速度[G]
BOOL fReverse;				// バックギア
BOOL fReverseRun;			// バック走行状態
BOOL fPark;					// パーキングブレーキ
SHORT StopTime = 9999;		// 自車停止時間[s]x10(起動時は停止状態)
BOOL fStop = TRUE;			// 自車停止フラグ(起動時は停止状態)
BOOL fCurve;				// カーブ走行中フラグ
FLOAT SlopeG;				// 道路勾配分加速度[G]

// GPS関連
#define GPS_KMPH_PER_KNOT 1.852
CHAR StrGpsTime[14];	// GPSから受信した文字列
CHAR StrGpsDate[10];
CHAR StrGpsGeoid[10];
BOOL fGpsFixRMC, fGpsFixGGA;	// RMC, GGA確定フラグ
BOOL fGpsFix;			// GPS確定フラグ
BOOL fGpsDoubt;			// GPSによる測位が疑わしいフラグ(受信レベル)
BOOL fGpsDoubt2;		// GPSによる測位が疑わしいフラグ(速度)
BOOL fGpsConfirm;		// GPS精度確定フラグ
BOOL fGpsYawConfirm;	// GPSヨーレート確定フラグ
BOOL fGpsRecv;			// GPSデータ変化フラグ
LONG GpsLon, GpsLat;	// GPS経度&緯度[deg]x1e7
FLOAT GpsYawRate;		// GPSヨーレート(右が正)[deg/s]
FLOAT GpsVreal;			// GPS車速[km/h]
FLOAT GpsCourse;		// GPS方位[deg]
FLOAT GpsAltitude;		// GPS標高[m]
FLOAT GpsHdop;			// GPS精度[th]
SHORT GpsSats;			// GPS衛星数[個]
SHORT GpsFixCount;		// GPSによる測位継続回数[回]
FLOAT SatLevelSum;		// GPS SN比合計
FLOAT SatLevel = 30.0;	// GPS SN比平均
FLOAT SatLevelFilt = 30.0;// GPS SN比平均フィルタ値
SHORT SatCount;			// GPS SN比平均データ数

UCHAR TxMode;			// 何を送信するか
#define TX_NMEA 0		// NMEAセンテンス出力(デフォルト)
#define TX_LOG 1		// モニターデータ出力
BOOL fGpsTransparent;	// NMEA出力時にGPSモジュールの信号をそのまま送る

#if _HILS	// HILS用ビルドなら
 #define BUFFMAX 300	// ローカル送受信バッファサイズ(HILS)
#else
 #define BUFFMAX 90		// ローカル送受信バッファサイズ(NMEA)
#endif //_HILS
CHAR Buff[BUFFMAX];		// ローカル送受信バッファ
SHORT BufPtr;			// バッファポインタ

// INS関連
LONG InsLon, InsLat;	// 慣性航法経度&緯度[deg]x1e7
FLOAT InsCourse;		// INS方位[deg]

#define CMDMAX 7		// コマンド受信受信バッファサイズ
CHAR CmdBuff[CMDMAX];	// コマンド受信バッファ
CHAR CmdPtr;			// コマンドバッファポインタ

#if _TESTMODE	// テストモードなら
CHAR TestMode;
#endif //_TESTMODE

// GPSモジュール設定関連
// [UBX][CFG][NMEA] NMEA V4.1 NumberingをExtended(3digit)に
const UCHAR GpsCmd1[] = {0xB5,0x62,0x06,0x17,0x14,0x00,0x00,0x41,0x00,0x02,
						 0x00,0x00,0x00,0x00,0x01,0x00,0x00,0x01,0x00,0x00,
						 0x00,0x00,0x00,0x00,0x00,0x00,0x76,0x63};
const UCHAR GpsCmd2[] = {0xB5,0x62,0x06,0x17,0x00,0x00,0x1D,0x5D};
// [UBX][CFG][NAV5] DynamicModel=Automotiveに
const UCHAR GpsCmd3[] = {0xB5,0x62,0x06,0x24,0x24,0x00,0xFF,0xFF,0x04,0x03,
						 0x00,0x00,0x00,0x00,0x10,0x27,0x00,0x00,0x05,0x00,
						 0xFA,0x00,0xFA,0x00,0x64,0x00,0x2C,0x01,0x00,0x3C,
						 0x00,0x00,0x00,0x00,0xC8,0x00,0x00,0x00,0x00,0x00,
						 0x00,0x00,0x18,0xE4,0xB5,0x62,0x06,0x24,0x00,0x00,
						 0x2A,0x84};
// [UBX][CFG][PRT] 19200bpsに
const UCHAR GpsCmd7[] = {0xB5,0x62,0x06,0x00,0x14,0x00,0x01,0x00,0x00,0x00,
						 0xD0,0x08,0x00,0x00,0x00,0x4B,0x00,0x00,0x07,0x00,
						 0x03,0x00,0x00,0x00,0x00,0x00,0x48,0x57};
const UCHAR GpsCmd8[] = {0xB5,0x62,0x06,0x00,0x01,0x00,0x01,0x08,0x22};


#if _HILS	// HILS用ビルドなら
FLOAT Dtime;
LONG RxCount;
BOOL fTimeOut = TRUE;
#endif //_HILS

// プロトタイプ宣言
BOOL JudgeGpsDoubtLevel(FLOAT v, FLOAT vgps, BOOL fgpsfix,
					FLOAT levelave, FLOAT* levelavefilt);
BOOL JudgeGpsDoubtVreal(FLOAT v, FLOAT vgps, SHORT gpscount);
FLOAT CalcVsp(FLOAT* afilt, FLOAT dt, FLOAT slopeg, FLOAT accely,
					BOOL freverse, BOOL fpark);
FLOAT GetTrueYawRate(FLOAT* yofst, SHORT yawraw, FLOAT temp, FLOAT gpsyaw,
					FLOAT v, FLOAT accel, BOOL fcurve, BOOL fpark,
					BOOL fstop, BOOL frun);
void LearnVsp(ULONG* vspconst, FLOAT v, FLOAT vgps, BOOL flearn);
BOOL JudgeCurve(FLOAT yaw, FLOAT gpsyaw, SHORT gpscount);
void CalcInsCoordinate(LONG* lat, LONG* lon, FLOAT cs, FLOAT v, FLOAT dt);

// ARDUINO ピン設定
#define VSP 3
#define MON 4
#define REVERSE 6
#define PARK 7
#define SCL_IN A5
#define SDA_IN A4
#define LED_PIN 13
#define VSPINT INT1

#define LED_ON digitalWrite(LED_PIN, 1);
#define LED_OFF digitalWrite(LED_PIN, 0);
/*----------------------------------------------------------------------------
	セットアップ
----------------------------------------------------------------------------*/
void setup()
{
	pinMode(LED_PIN, OUTPUT);
	pinMode(MON, INPUT_PULLUP);
	pinMode(PARK, INPUT_PULLUP);
	// INT1:fit3はVSPがオープンコレクタなのでプルアップが必要
	pinMode(VSP, INPUT_PULLUP);

	// ジャンパーがオンならモニターデータ出力モード
	TxMode = digitalRead(MON) ? TX_NMEA : TX_LOG;

	Wire.begin();			// I2C利用
	Wire.setClock(400000L);	// I2C高速モードに
	Wire.setWireTimeout();	// I2Cタイムアウト(古いVer.には存在しない)

	Serial.begin(115200);	// ハードウェアシリアル115.2kbps

	while (!Serial);

	// MPU6050の初期化
	Mpu.initialize();			// イニシャライズ
	// Z軸(Yaw)しか使わないのでクロックもZに(意味あるか不明)
	Mpu.setClockSource(MPU6050_CLOCK_PLL_ZGYRO);
	Mpu.setDLPFMode(6);			// LPF 5Hz(100msサンプルなので5Hzが上限)
	Mpu.setSleepEnabled(FALSE);
	if (!Mpu.testConnection())	// 接続エラーなら
	{
		Sprintln(F("MPU6050(ジャイロ)接続エラー"));
		Mpu.reset();				// リセットしてみる
		delay(2000);
		asm volatile("jmp 0");		// リスタート
	}
	// センサオフセット補正値設定
	Mpu.setXAccelOffset(AOFSTX);
	Mpu.setYAccelOffset(AOFSTY);
	Mpu.setZAccelOffset(AOFSTZ);
	Mpu.setXGyroOffset(GOFSTX);
	Mpu.setYGyroOffset(GOFSTY);
	Mpu.setZGyroOffset(GOFSTZ);

	// GPSのNEO-M8Nがフェイク品(フラッシュなし)でもいいように起動時に設定
	Serial2.begin(9600);	// ソフトシリアル(割り込み版)利用。最初は9600bps
	delay(2000);			// GPSユニット起動待ち(BN-880は長めに必要)

	// [UBX][CFG][NMEA] NMEA V4.1 NumberingをExtended(3digit)に設定
	SprintN(GpsCmd1, sizeof(GpsCmd1));	// コマンド送出
	delay(100);							// しばし待つ
	SprintN(GpsCmd2, sizeof(GpsCmd2));	// コマンド送出

	// [UBX][CFG][NAV5] DynamicModel=Automotiveに設定
	delay(100);							// しばし待つ
	SprintN(GpsCmd3, sizeof(GpsCmd3));	// コマンド送出

	// [UBX][CFG][PRT] ビットレート変更9600→19200bps
	delay(100);							// しばし待つ
	SprintN(GpsCmd7, sizeof(GpsCmd7));	// コマンド送出
	delay(100);							// しばし待つ
	SprintN(GpsCmd8, sizeof(GpsCmd8));	// コマンド送出

	delay(100);				// 念のため最後の文字送信終了を待つ
	Serial2.end();			// ソフトシリアルいったん終了
	Serial2.begin(19200);	// GPSモジュールRx用に再設定

	EepRead();				// 前回学習値読み込み

	attachInterrupt(VSPINT, VspProc, RISING);	// VSP割り込みハンドラ設定

	Dt = 0.1;	// F/S
	LastTime = micros();

	#if !_HILS	// HILS用ビルドでないなら
	wdt_enable(WDTO_2S);	// WDT開始
	#endif //_HILS
}
/*----------------------------------------------------------------------------
	メインループ
----------------------------------------------------------------------------*/
void loop()
{
	#if !_HILS	// HILS用ビルドでないなら
	RxCmd();		// コマンド受信処理
	RxTxGPS();		// GPSデータ送受信
	#else
	HILS_RxData();	// HILSデータ受信
	#endif //_HILS

	#if !_HILS	// HILS用ビルドでないなら
	//*** 100ms処理 ***
	if (micros() - LastTime >= 100000L)	// 100[ms]x1000
	#endif //_HILS
	{
		#if !_HILS	// HILS用ビルドでないなら
		wdt_reset();	// WDTリセット
		#endif //_HILS
		Job100ms();		// 100ms処理
	}
}
/*----------------------------------------------------------------------------
	100ms処理
----------------------------------------------------------------------------*/
#define RUNRV 5.0			// リバース走行判定解除車速[km/h]
#define STOPTIMEC 10		// 停止判定時間[s]x10
#define VSPLEARN_LV 30.0	// VSP学習最低車速[km/h]
#define VSPLEARN_HV 180.0	// VSP学習最高車速[km/h]
#define VSPLEARN_A 0.05		// VSP学習最高加速度[G]
#define GOFSTFILT 0.0001	// 加速度センサオフセットフィルタ定数
#define PERRGAIN 80.0		// 位置リセット閾値車速ゲイン(100km/hで約89m)
#define PERROFST 1000.0		// 位置リセット閾値オフセット(約11m)
#define CERR 45.0			// 方位リセット閾値[deg]
#define ERRTIME 10			// INS位置と方位をリセットするまでの時間閾値[回]
#define SLOPEDEADG 0.04		// 勾配加速度不感帯[G]
#define INSFILT 0.2			// INS GPS位置一致フィルタ定数
#define INSFILTCRS 0.05		// INS GPS方位一致フィルタ定数
void Job100ms()
{
	static SHORT rtime, prktime, poserrcount, crserrcount;
	static FLOAT yawratez, accelyfilt, sgfilt;
	FLOAT latcal, loncal, crscal;
	FLOAT poserr, tofst;
	FLOAT laterr, lonerr, crserr;

LED_ON
	AddOnTime(TRUE, &AfterPowerOnTime);	// 通電後の時間[s]x10

	#if !_HILS	// HILS用ビルドでないなら
	Dt = (FLOAT)(micros() - LastTime) / 1000000.0; // 前回からの時間[s]
	LastTime = micros();
	fReverse = digitalRead(REVERSE);	// リバースギア
	#else
	// 起動時の初期化(HILS対応)
	if (AddOnTime <= 1)
	{
		poserrcount = crserrcount = 0;
	}
	#endif //_HILS

	fPark = !digitalRead(PARK);			// パーキングブレーキ
	AddOnTime(fPark, &prktime);			// パーキング信号オン時間

	#if !_HILS	// HILS用ビルドでないなら
	// YawGセンサ生データ取得[deg/s]x250/32768
	// 左右方向をX、前後方向をYとする。右が正、前が正
	Mpu.getMotion6(&AccelXRaw, &AccelYRaw, &AccelZRaw,
							&PitchRaw, &RollRaw, &YawRaw, &TempRaw);
	YawRaw *= -1;	// 右回転を正とする(方位と符合をそろえる)
	RollRaw *= -1;	// 時計回りを正とする(方位と符合をそろえる)
	#endif //_HILS
	Temperature = (FLOAT)TempRaw / 340.0 + 36.53;	// センサ温度[deg]

	// 温度補償したセンサーの前後加速度[G]
	AccelY = AccelYRaw * 2.0 / 32768.0 - (Temperature - CALTEMP) * SLOPE_G;;

	// 車速算出処理
	// Slopeは前回値だが補正用なので問題なし
	Vreal = CalcVsp(&ArealFilt, Dt, SlopeG, AccelY,
						fReverse, fPark);
	Filter(&VrealFilt, Vreal, GPSFILT);	// GPS車速と位相をそろえた車速
	AddOnTime(Vreal == 0.0 && !fReverse, &StopTime);// 停止時間[s]x10
	fStop = BOOLSET(StopTime >= STOPTIMEC);			// 停止判定
	AddOnTime(fReverse, &rtime);		// リバース信号オン時間
	if (rtime >= 1)						// リバースギアを入れたら
	{
		fReverseRun = TRUE;				// リバース状態確定
	}
	else if (rtime == 0 && Vreal >= RUNRV)	// 前進したら
	{
		fReverseRun = FALSE;			// リバース状態解除
	}

	// 道路勾配による加速度算出
	// センサ加速度を車速から求めた加速度と位相合わせ
	Filter(&accelyfilt, AccelY, GPSFILT);
	// 差分が勾配による加速度
	Filter(&sgfilt, accelyfilt - ArealFilt, GPSFILT);
	// 勾配加速度不感帯
	if (abs(sgfilt) <= SLOPEDEADG)
	{
		SlopeG = 0.0;
	}
	else
	{
		SlopeG = sgfilt;
	}
bug1=SlopeG;

	// GPS精度が疑わしい判断
	// (SatLevelとSatLevelFiltの確定はfGpsRecvより後なのでここで行う)
	fGpsDoubt = JudgeGpsDoubtLevel(Vreal, GpsVreal, fGpsFix,
								SatLevel, &SatLevelFilt) || fGpsDoubt2;

	#if _TESTMODE == 1	// テストモード1なら
	if (Vreal >= 30.0)	// 1の期間は
		fGpsDoubt = TRUE;// 不確定状態とする
	#endif //_TESTMODE

	// GPS精度確定(GPS2回以上確定でかつ疑わしくなくバック走行でない)
	fGpsConfirm = BOOLSET(GpsFixCount >= 2 && !fGpsDoubt && !fReverseRun);

	// GPSヨーレート確定(さらに車速が規定以上)
	fGpsYawConfirm = BOOLSET(fGpsConfirm  && Vreal >= YAWV);

	// センサの温度補償とオフセット除去を行い真のヨーレートを得る[deg/s]
	// (fCurveは前回値だが補正に使っているだけなので問題なし)
	YawRate = GetTrueYawRate(&YawOffset, YawRaw, Temperature, GpsYawRate, 
						Vreal, ArealFilt, fCurve, fPark,
						// 停止時オフセットキャンセル条件
						fStop,
						// 走行時オフセットキャンセル条件
						fGpsYawConfirm && !fCurve && fGpsRecv);

	// カーブ判定
	fCurve = JudgeCurve(YawRate, GpsYawRate, GpsFixCount);

	// パーキングブレーキが解除されているなら
	if (!fPark)
	{
		// ヨーレートを累積してINS方位0～360[deg]を得る(台形法)
		InsCourse = Normal360(InsCourse + (yawratez + YawRate) / 2.0 * Dt);
	}
	// 今回値をメモリ
	yawratez = YawRate;

	// INS自車位置推定
	CalcInsCoordinate(&InsLat, &InsLon, InsCourse, Vreal, Dt);

	// GPSから受信した直後
	if (fGpsRecv)
	{
		fGpsRecv = FALSE;	// GPS受信フラグクリア

		// 車速からの疑わしい判断を追加
		fGpsDoubt2 = JudgeGpsDoubtVreal(Vreal, GpsVreal, GpsFixCount);
		fGpsDoubt |= fGpsDoubt2;
		if (fGpsDoubt2)
		{
			fGpsConfirm = fGpsYawConfirm = FALSE;
		}

		if (fGpsConfirm)	// GPS位置が得られているなら
		{
			// INS位置とGPS位置の差分
			laterr = (FLOAT)(GpsLat - InsLat);
			lonerr = (FLOAT)(GpsLon - InsLon);
			// INS位置補正量
			latcal = laterr * INSFILT;
			loncal = lonerr * INSFILT;
			// INS位置をGPS位置に徐々に一致させる
			InsLat += (LONG)latcal;
			InsLon += (LONG)loncal;
		}
		else
		{
			laterr = lonerr = latcal = loncal = 0.0;
		}
		// GPS方位が得られていて直進なら
		if (fGpsYawConfirm && !fCurve)
		{
			// INS方位とGPS方位の差分
			crserr = Normal180(Normal180(GpsCourse) - Normal180(InsCourse));
			// INS方位補正量
			crscal = crserr * INSFILTCRS;
			// INS方位をGPS方位に徐々に一致させる
			InsCourse = Normal360(InsCourse + crscal);
		}
		else
		{
			crserr = crscal = 0.0;
		}

		// INSとGPSが大きく乖離した回数
		poserr = abs(Vreal) * PERRGAIN + PERROFST;	// 位置ずれ判定距離
		AddOnTime(fGpsConfirm &&
					(abs(laterr) >= poserr || abs(lonerr) >= poserr),
					&poserrcount);
		AddOnTime(fGpsYawConfirm && abs(crserr) >= CERR,
					&crserrcount);

		// INSとGPSの乖離が連続した時の初期化
		if (poserrcount >= ERRTIME || crserrcount >= ERRTIME)
		{
			InsLat = GpsLat;		// INS位置にGPS位置を代入
			InsLon = GpsLon;
			InsCourse = GpsCourse;	// INS方位にGPS方位を代入
		}

		// VSP定数学習
		LearnVsp(&VspConst, VrealFilt, GpsVreal,
					// GPS取得かつ疑わしくなく
					// バック走行ではなくかつ
					// 車速が範囲内かつ
					// 加速度が範囲内かつ
					// ヨーレートが範囲内
					fGpsConfirm &&
					!fReverseRun &&
					Vreal >= VSPLEARN_LV &&
					Vreal <= VSPLEARN_HV &&
					abs(ArealFilt) <= VSPLEARN_A &&
					abs(GpsYawRate) < CURVEYAW);
bug=(FLOAT)VspConst / 10000.0;
	}

	if (TxMode == TX_LOG)	// モニターデータ出力モードか？
	{
		// 送信バッファが空だったら
		if (Serial.availableForWrite() >= SERIAL_TX_BUFFER_SIZE - 1)
		{
			#if !_HILS	// HILS用ビルドでないなら
			// モニタ用変数をシリアルに出力(100ms)
			// 使うときは↓どれか一つだけコメントを外す
			MonitorOut();		// ログ出力
			//printBugs();		// デバッグ情報
			//printTempYawG();	// 温度、ヨー、前後Gセンサ生値
			//printYawG();		// 温度補償したヨー、前後G
			#endif //_HILS
		}
		TxDt += Dt;
	}

	#if _HILS	// HILS用ビルドなら
	HILSOut();		// HILS用シリアル出力
	#endif //_HILS

	// EEP書き込み処理
	// 通電から時間が経過してからパーク信号が連続してオンになった瞬間
	if (AfterPowerOnTime >= POWERONDELAYC && prktime == 5)
	{
		EepWrite();	// EEPROM書き込み処理
	}

LED_OFF
}
/*----------------------------------------------------------------------------
	衛星受信レベルによるGPSデータが疑わしい判断(100ms)
		トンネルに入った直後などではGPSがFIXしていても位置がジャンプする
		そこで受信レベルが低下したことを検知して疑わしい状態を判断する
	書式 ret = JudgeGpsDoubt(FLOAT v, FLOAT vgps, BOOL fgpsfix,
					FLOAT levelave, FLOAT* levelavefilt)

	BOOL ret;		TRUE=疑わしい
	FLOAT v;		車速[km/h]
	FLOAT vgps;		GPS車速[km/h]
	BOOL fgpsfix;	GPS確定状態
	FLOAT levelave;	受信レベルの平均[dB]
	FLOAT levelavefilt;	フィルタ後の受信レベルの平均[dB]
----------------------------------------------------------------------------*/
#define DOUBTON 0.8			// GPSが疑わしいと判断するレベル
#define DOUBTOFF 0.85		// GPSが疑わしくないと判断するレベル
#define DOUBTOFFTIME 70		// GPSが疑わしくないと判断する最大時間[s]x10
#define DOUBTV 5.0			// 疑わしい状態回復の車速偏差[km/h]
#define DOUBTTIMEC 200		// 車速が正しい時の回復時間[s]x10
BOOL JudgeGpsDoubtLevel(FLOAT v, FLOAT vgps, BOOL fgpsfix,
					FLOAT levelave, FLOAT* levelavefilt)
{
	static BOOL fdoubt;
	static SHORT recovertime, judgetime, nodoubttime;
	BOOL fleveldown;

	// 衛星受信レベルの平均値が通常よりも低下
	fleveldown = BOOLSET(fgpsfix && levelave < *levelavefilt * DOUBTON);
	if (fgpsfix)	// GPS確定中なら
	{
		// 衛星受信レベルの平均値が通常よりも低下した初回？
		if (!fdoubt && fleveldown)
		{
			fdoubt = TRUE;	// GPSが疑わしいと判断
		}
		// 衛星受信レベルの平均値が回復して時間が経過した？
		else if (recovertime >= judgetime)
		{
			fdoubt = FALSE;	// GPSが疑わしくないと判断
			judgetime = 0;
		}
	}
	else
	{
		fdoubt = FALSE;	// GPSがFIXしていないならフラグを下しておく
	}
	// GPS未受信または精度悪化が継続した時間
	if ((fleveldown || !fgpsfix) && judgetime < DOUBTOFFTIME)
	{
		AddOnTime(TRUE, &judgetime);	// 判定解除時間を設定しておく
	}

	// 衛星受信レベルの平均値が回復してからの経過時間
	AddOnTime(fdoubt && levelave > *levelavefilt * DOUBTOFF, &recovertime);

	// GPSが疑わしい状態から回復できないときのF/S
	// GPSが疑わしい状態で車速があってほぼ正しい時間
	AddOnTime(fdoubt && v >= YAWV && abs(v - vgps) <= DOUBTV, &nodoubttime);
	// GPS車速がほぼ正しい状態が長引いた？
	if (fdoubt && nodoubttime >= DOUBTTIMEC)
	{
		fdoubt = FALSE;				// GPSが疑わしくないとする
		*levelavefilt = levelave;	// 次回判定用の値として現在値を使用する
		judgetime = 0;
	}
	return fdoubt;
}
/*----------------------------------------------------------------------------
	GPSデータが疑わしい判断[Vreal](1s)
		衛星からのデータの精度悪を車速の乱れで判定
	書式 ret = JudgeGpsDoubtVreal(FLOAT v, FLOAT vgps, SHORT gpscount)

	BOOL ret;		TRUE=疑わしい
	FLOAT v;		車速[km/h]
	FLOAT vgps;		GPS車速[km/h]
	SHORT gpscount;	GPS確定回数
----------------------------------------------------------------------------*/
#define VDOUBTTIMEC 3	// 車速回復待ち時間[s]
BOOL JudgeGpsDoubtVreal(FLOAT v, FLOAT vgps, SHORT gpscount)
{
	FLOAT err;
	static BOOL fdoubt;
	static SHORT oktime;

	if (AfterPowerOnTime <= 1)	// 起動時は
	{
		fdoubt = FALSE;
		oktime = 0;
	}
	if (gpscount >= 2)		// GPSが確定しているなら
	{
		err = abs(v - vgps);	// 車体速度とGPS車速との差
		if (err >= DOUBTV)		// 車速が悪化した？
		{
			fdoubt = TRUE;		// 車速による疑わしい判定開始
			oktime = 0;
		}
		AddOnTime(err < DOUBTV, &oktime);	// 車速が回復した時間
		if (oktime >= VDOUBTTIMEC)			// 回復が規定回数に達した？
		{
			fdoubt = FALSE;		// 疑わしい判定取り下げ
		}
	}
	else	// GPS未確定なら
	{
		fdoubt = FALSE;
		oktime = 0;
	}
	return fdoubt;
}
/*----------------------------------------------------------------------------
	カーブ走行判定(100ms)
	書式 ret = JudgeCurve(FLOAT yaw, FLOAT gpsyaw, SHORT gpscount);

	BOOL ret;		TRUE=カーブ中
	FLOAT yaw;		ヨーレート[deg/s]
	FLOAT gpsyaw;	GPSヨーレート[deg/s]
	SHORT gpscount;	GPS確定回数
----------------------------------------------------------------------------*/
#define CRESTIMEC 300	// カーブ期間リセット時間[s]x10
#define CURVETIMEC 20	// ヨーレート安定までの待ち時間[s]x10
#define CRETRYTIMEC 100	// 疑わしい期間リセット時間[s]x10
BOOL JudgeCurve(FLOAT yaw, FLOAT gpsyaw, SHORT gpscount)
{
	static BOOL fcrv;
	static SHORT curvedelay, gytime, ctime, retrytime;

	if (AfterPowerOnTime <= 1)	// 起動時は
	{
		curvedelay = 0;
		gytime = 0;
		ctime = 0;
		retrytime = 0;
	}

	// GPSヨーレートが出ていない時間
	AddOnTime(abs(gpsyaw) <= CURVEYAW && gpscount >= 2, &gytime);

	DecNonZero(&curvedelay);	// カーブ走行タイマデクリメント
	DecNonZero(&retrytime);		// リトライタイマデクリメント
	if (abs(yaw) >= CURVEYAW)	// ヨーレートが出ている期間は
	{
		curvedelay = CURVETIMEC;	// タイマ再セット
	}
	fcrv = BOOLSET(curvedelay > 0);	// カーブ走行中判断(仮)

	AddOnTime(fcrv, &ctime);	// カーブと判定されている時間
	// カーブと判定されている時間が長いが正しそうな時間も長い
	// (いつまでも終わらなくなることを防止するためのF/S処理)
	if (ctime >= CRESTIMEC && gytime >= CRESTIMEC)
	{
		retrytime = CRETRYTIMEC;	// いったん疑わしい期間を取りやめ
	}
	if (retrytime > 0)		// ディレイタイマが0でない期間は
	{
		fcrv = FALSE;		// カーブ判断を取り下げる
	}
	return fcrv;
}
/*----------------------------------------------------------------------------
	コマンド受信処理

	"ini"	EEP初期データ書き込み
	"set"	EEP設定データ書き込み
	"pos"	EEP初期位置と方位データのみ書き込み
----------------------------------------------------------------------------*/
void RxCmd()
{
	CHAR c;

	while (Serial.available() > 0)	// 文字を受信していれば
	{
		c = Serial.read();	// 1文字受信
		if (CmdPtr < CMDMAX - 2)	// バッファに余裕があるなら
		{
			CmdBuff[CmdPtr++] = c;	// 格納
			if (c == '\n')			// 行末か？
			{
				CmdBuff[CmdPtr] = '\0';	// 文字列をターミネート
				if (strncmp(CmdBuff, "ini", 3) == 0)
				{
					EepInitialWrite();	// EEP初期データ書き込み
				}
				else if (strncmp(CmdBuff, "log", 3) == 0)
				{
					TxMode = TX_LOG;	// モニターデータ出力モード
				}
				else if (strncmp(CmdBuff, "gps", 3) == 0)
				{
					TxMode = TX_NMEA;	// NMEA出力モード
				}
				else if (strncmp(CmdBuff, "raw", 3) == 0)
				{
					fGpsTransparent = TRUE;	// GPS透過モードに切り替え
				}
				CmdPtr = 0;	// ポインタを先頭に
			}
		}
		else	// バッファフルなら
		{
			CmdPtr = 0;	// ポインタを先頭に
		}
	}
}
/*----------------------------------------------------------------------------
	GPS受信＆送信処理

	【参照】
	BOOL fGpsFixRMC;	RMC確定
	BOOL fGpsFixGGA;	GGA確定
	CHAR* Buff			ローカル送受信バッファ(書き換わるので注意)
	FLOAT Vreal			車速[km/h]
	CHAR* StrGpsDate	GPSから受信した文字列
	CHAR* StrGpsGeoid	GPSから受信した文字列
	LONG InsLat			慣性航法緯度[deg]x1e7
	LONG InsLon			慣性航法経度[deg]x1e7
	FLOAT InsCourse		慣性航法方位[deg]
	BOOL fGpsDoubt		GPSによる測位が疑わしいフラグ
	【書き換え】
	■RxRMC
	LONG GpsLat			GPS緯度[deg]x1e7
	LONG GpsLon			GPS経度[deg]x1e7
	FLOAT GpsVreal		GPS車速[km/h]
	FLOAT GpsCourse		GPS方位[deg]
	FLOAT GpsYawRate	GPSヨーレート(右が正)[deg/s]
	■RxGGA
	SHORT GpsSats		GPS衛星数[個]
	FLOAT GpsAltitude	GPS標高[m]
	FLOAT GpsHdop		GPS精度[th]
	BOOL fGpsFix		GPS確定フラグ
	SHORT GpsFixCount	GPSによる測位継続回数[回]
	BOOL fGpsRecv		GPSデータ変化フラグ
	■RxGSV
	FLOAT SatLevelSum	GPS SN比合計
	SHORT SatCount		GPS SN比平均データ数
	■RxTxGPS
	FLOAT SatLevel		GPS SN比平均
	FLOAT SatLevelFilt	GPS SN比平均フィルタ値
----------------------------------------------------------------------------*/
#define GPSLEVELFILT 0.01	// GPSの受信レベルのフィルタ定数
void RxTxGPS()
{
	CHAR c;

	while (Serial2.available() > 0)	// 文字を受信していれば
	{
		c = Serial2.read();	// 1文字受信

		// NMEA出力モードでGPS透過モードなら
		if (TxMode == TX_NMEA && fGpsTransparent)
		{
			Serial.print(c);// そのまま送信
			break;			// ここまで
		}

		if (c == '$')		// 先頭文字なら
		{
			BufPtr = 0;		// バッファの先頭から
		}
		if (BufPtr < BUFFMAX - 2)	// バッファに余裕があるなら
		{
			Buff[BufPtr++] = c;	// 格納
			if (c == '\n')		// 行末か？
			{
				Buff[BufPtr] = '\0';	// 文字列をターミネート
				if (SumCheck(Buff))		// サムが正しいなら
				{
					if (strncmp(Buff + 3, "RMC", 3) == 0)
					{
						RxRMC();			// RMC受信処理
					}
					else if (strncmp(Buff + 3, "GGA", 3) == 0)
					{
						RxGGA();			// GGA受信処理
						SatLevelSum = 0.0;	// 受信レベル累積をクリア
						SatCount = 0;		// 受信レベル累積個数をクリア
					}
					else if (strncmp(Buff + 3, "GSV", 3) == 0)
					{
						RxGSV();		// GSV受信受信処理(レベル累積)
					}
					// 最後のセンテンスであるGLLを受けた？
					else if (strncmp(Buff + 3, "GLL", 3) == 0)
					{
						if (SatCount >= 3)	// 受信レベルの累積が3以上あるなら
						{
							// 受信レベルの平均値を算出
							SatLevel = SatLevelSum / (FLOAT)SatCount;
							// GPSが確定していたらフィルタリングしておく
							if (GpsFixCount >= 2 && !fGpsDoubt)
							{
								Filter(&SatLevelFilt, SatLevel, GPSLEVELFILT);
							}
						}
					}
					BufPtr = 0;	// ポインタを先頭に

					// NMEA送信モードなら
					if (TxMode == TX_NMEA)
					{
						TxGPS();	// GPS送信処理
					}
				}
				else	// サムが正しくないなら
				{
					BufPtr = 0;	// ポインタを先頭に
				}
			}
		}
		else	// バッファフルなら
		{
			BufPtr = 0;	// ポインタを先頭に
		}
	}
}
/*----------------------------------------------------------------------------
	RMCセンテンス受信処理
----------------------------------------------------------------------------*/
void RxRMC()
{
	static ULONG tim;
	static FLOAT dt;
	static FLOAT csz;
	static SHORT fixcount;
	static FLOAT cs, spd, cdif;
	CHAR* p;
	SHORT i, len;

	// 前回からの経過時間[s]
	dt = (FLOAT)(micros() - tim) / 1000000.0;	// [s]
	tim = micros();

	p = strtokComma(Buff);		// カンマで区切られた手前部分の文字列
	for (i = 0; i <= 9; i++ )
	{
		len = strlen(p);
		if (len != 0)
		{
			if (i == 1 && len < 14) strcpy(StrGpsTime, p);	// 時刻
			if (i == 2) fGpsFixRMC = BOOLSET(*p == 'A');	// ステータス
			if (i == 7) spd = atof(p) * GPS_KMPH_PER_KNOT;	// 速度[km/h]
			if (i == 8) cs = atof(p);			// 方位[deg]
			if (i == 9 && len < 10) strcpy(StrGpsDate, p);	// 日時
		}
		p = strtokComma(NULL);	// 次の文字列
	}

	AddOnTime(fGpsFixRMC, &fixcount);	// 連続確定回数
	if (fGpsFixRMC)	// 確定していたら
	{
		// バックなら負に
		GpsVreal = fReverse ? -spd : spd;	// 速度を更新

		// バック走行時は反転されてくるので車の向きと合うように反転する
		GpsCourse = fReverse ? Add180(cs) : cs;	// 方位を更新
		// 2回以上連続して確定していたら
		if (fixcount >= 2 && dt > 0.0)
		{
			// GPS方位の差分からGPSヨーレート[deg/s]を算出
			cdif = Normal180(Normal180(GpsCourse) - Normal180(csz));// 差分
			if (abs(cdif) >= 100.0)	// 反転直後のジャンプ時は
			{
				cdif = 0.0;			// ヨーレートは0とする
			}
			GpsYawRate = cdif / dt;
		}
		csz = GpsCourse;	// 今回の方位をメモリ
	}
	else
	{
		GpsYawRate = 0.0;
	}
}
/*----------------------------------------------------------------------------
	GGAセンテンス受信処理
----------------------------------------------------------------------------*/
void RxGGA()
{
	CHAR* p;
	SHORT i, len;
	static LONG lat, lon;

	p = strtokComma(Buff);		// カンマで区切られた手前部分の文字列
	for (i = 0; i <= 11; i++ )
	{
		len = strlen(p);
		if (len  != 0)
		{
			if (i == 2) lat = NMEA2Deg(p);	// 緯度
			if (i == 3 && *p == 'S') lat *= -1;
			if (i == 4) lon = NMEA2Deg(p);	// 経度
			if (i == 5 && *p == 'W') lon *= -1;
			if (i == 6) fGpsFixGGA = BOOLSET(*p != '0');// ステータス
			if (i == 7) GpsSats = atoi(p);		// 衛星の数
			if (i == 8) GpsHdop = atof(p);		// HDOP[th]良好なら1.0前後
			if (i == 9) GpsAltitude = atof(p);	// 高度[m]
			if (i == 11 && len < 10) strcpy(StrGpsGeoid, p);// ジオイド
		}
		p = strtokComma(NULL);	// 次の文字列
 	}
#if 0	// 受信アプリ側で異常値をはじくのでやらない
	// F/S:位置情報の妥当性チェック
	if (fGpsFixGGA)	// GPS確定？
	{
		// 値がやたら小さい？
		if (GpsLat != 0L && GpsLon != 0L &&
						(abs(lat) < (abs(GpsLat) / 10L) ||
						 abs(lon) < (abs(GpsLon) / 10L)))
		{
			fGpsFixGGA = FALSE;		// ロスト扱いとする
		}
		else	// 値が正常なら
		{
			GpsLat = lat;	// アップデート
			GpsLon = lon;
		}
	}
#else
	if (fGpsFixGGA)	// GPS確定？
	{
		GpsLat = lat;	// アップデート
		GpsLon = lon;
	}
#endif
	// GPS全受信FIX
	fGpsFix = fGpsFixRMC && fGpsFixGGA;
	AddOnTime(fGpsFix, &GpsFixCount);	// 連続確定回数
	// RMC&GGA受信完了(GPSモジュールからはRMC→GGAの順に来るので)
	fGpsRecv = TRUE;
}
/*----------------------------------------------------------------------------
	GSVセンテンス受信処理
----------------------------------------------------------------------------*/
#define ELEVATION_MIN 30	// GPSのレベルを累積する衛星の高度
#define SATLEVELERR 50		// 衛星レベルエラーSNR[dB]普通は30くらい
void RxGSV()
{
	CHAR* p;
	SHORT i = 0, j = 0, len;
	SHORT number, elevation, azimuth, snr;

	p = strtokComma(Buff);		// カンマで区切られた手前部分の文字列
	while (p)
	{
		len = strlen(p);
		if (len != 0)
		{
			if (i == (4 + j)) number = atoi(p);		// 衛星番号
			if (i == (5 + j)) elevation = atoi(p);	// 高度
			if (i == (6 + j)) azimuth = atoi(p);	// 方位
		}
		if (i == (7 + j))
		{
			// データが存在していて高度が規定以上の衛星なら
			if (len != 0 && elevation >= ELEVATION_MIN)
			{
				snr = atoi(p);	// SN比
				if (snr < SATLEVELERR)	// あまりに大いのは異常データ
				{
					SatLevelSum += (FLOAT)snr;	// 累積
					SatCount++;					// 累積した数
				}
			}
			j += 4;
		}
		i++;
		p = strtokComma(NULL);	// 次の文字列
	}
}
/*----------------------------------------------------------------------------
	GPS送信処理
----------------------------------------------------------------------------*/
void TxGPS()
{
	// RMCセンテンスだった？？
	if (strncmp(Buff + 3, "RMC", 3) == 0)
	{
		TxRMC(!fGpsFixRMC || fGpsDoubt);	// RMCセンテンスを書き換えて送信
	}
	// GGAセンテンスだった？？
	else if (strncmp(Buff + 3, "GGA", 3) == 0)
	{
		TxGGA(!fGpsFixGGA || fGpsDoubt);	// GGAセンテンスを書き換えて送信
	}
	else if (strncmp(Buff + 3, "GLL", 3) == 0)
	{
		TxHDT();// HDTセンテンスを送信(方位表示用のオマケ)
	}
	// VTGまたはGLLまたはHDTセンテンスなら
	else if (strncmp(Buff + 3, "VTG", 3) == 0 ||
			 strncmp(Buff + 3, "HDT", 3) == 0)
	{
		// 何もしない
	}
	else	// それ以外のセンテンスなら
	{
		GpsPrint(Buff);	// そのまま送信
	}
}
/*----------------------------------------------------------------------------
	degx1e7をNMEA形式x1e5に変換
	書式 ret = Deg2NMEA(LONG deg);

	LONG ret;	dddmm.mmmmm x 1e5
	LONG deg;	ddd.ddddddd x 1e7
----------------------------------------------------------------------------*/
LONG Deg2NMEA(LONG deg)
{
	LONG x0, x1, x2, x3, x4;

	// ddd.ddddddd x1e7 → ddd dddddd[deg]
	x0 = deg / L1e7;	// ddd[deg]
	x1 = deg % L1e7;	// 0.ddddddd[deg]x1e7

	// 0.dddddd[deg]x1e7 → mm mmmmmm[min]
	x2 = x1 * 60L / 100L;	// mm.mmmmm[min]x1e5
	x3 = x2 / 100000L;		// mm[min]
	x4 = x2 % 100000L;		// .mmmmm[min]x1e5
	return (x0 * 100L + x3) * 100000L + x4;
}
/*----------------------------------------------------------------------------
	NMEA形式をdegx1e7に変換
	書式 ret = NMEA2Deg(CHAR* nmea);

	LONG ret;	ddd.ddddddd x 1e7
	CHAR* nmea;	"dddmm.mmmmm"
----------------------------------------------------------------------------*/
LONG NMEA2Deg(CHAR* nmea)
{
	CHAR* p;
	LONG x = 0;
	FLOAT f;
	SHORT len;

	p = strchr(nmea, '.');	// 小数点の位置
	if (p)
	{
		*p = '\0';				// 小数点の位置でいったんターミネート
		len = strlen(nmea);		// 整数部分の文字長
		x = atol(nmea) / 100L;	// [deg]
		x *= L1e7;				// [deg]x1e7
		*p = '.';				// ターミネートを戻す
		f = atof(nmea + len - 2);// [min]
		f = (f / 60.0) * F1e7;	// [min]→[deg]x1e7
		x += (LONG)(f + 0.5);	// 小数点以下を四捨五入して加算
	}

	return x;
}
/*----------------------------------------------------------------------------
	NMEAチェックサム計算
	書式 ret = CalcSum(CHAR* buf);

	UCHAR ret;		チェックサム
	CHAR* buf;		バッファポインタ(先頭が'$'で末尾が'*'であること)
----------------------------------------------------------------------------*/
UCHAR CalcSum(CHAR* buf)
{
	UCHAR c = 0;
	SHORT i;

	for (i = 1; i < strlen(buf) - 1; i++)
	{
		c ^= buf[i];
	}
	return c;
}
/*----------------------------------------------------------------------------
	NMEAサムチェック
	書式 ret = SumCheck(CHAR* buf);

	BOOL ret;		サムチェック結果 TRUE=OK
	CHAR* buf;		バッファポインタ(先頭が'$'で末尾が'\n'であること)
----------------------------------------------------------------------------*/
BOOL SumCheck(CHAR* buf)
{
	UCHAR c = 0, len, sum;
	SHORT i;

	len = strlen(buf);
	sum = Hex2Dec(buf[len - 4]) * 16 + Hex2Dec(buf[len - 3]);
	for (i = 1; i < len - 5; i++)
	{
		c ^= buf[i];
	}
	return BOOLSET(c == sum);
}
/*----------------------------------------------------------------------------
	NMEAセンテンス送信
----------------------------------------------------------------------------*/
void TxNMEA()
{
	CHAR s[8];
	UCHAR sum;

	GpsPrint(Buff);			// バッファ内容をシリアル出力
	sum = CalcSum(Buff);	// sum計算
	stcu_h(s, sum);			// HEXに(バッファは最大6文字分確保必要)
	strcpy(Buff, &s[2]);	// サム2バイト分をバッファに入れる
	GpsPrintln(Buff);		// シリアル出力+改行
}
/*----------------------------------------------------------------------------
	RMCセンテンス送信(GPSの場合は方位を書き換える)
	GPSモジュールからの方位は停止中はなくなってしまうので常に代替えする
	書式 void TxRMC(BOOL fins);

	BOOL fins;		INSの場合TRUE
----------------------------------------------------------------------------*/
void TxRMC(BOOL fins)
{
	LONG lon, lat, spd;
	CHAR s[16];
	FLOAT crs = InsCourse;

	// [km/h] → [km/h]x100
	#if _NMEAINS	// 常にINSデータを送るなら
	spd = (LONG)(abs(Vreal) * 100.0);
	#else
	spd = (LONG)(abs(fins ? Vreal : GpsVreal) * 100.0);
	#endif //_NMEAINS
	if (Vreal < 2.0)	// 車速が低いときは
	{
		spd = 0L;		// NMEAで送る車速を強制的に0とする
	}
	// [km/h]x100 → [knot/h]x1000
	spd = (spd * 10000L + 1852L / 2L) / 1852L;

	#if _NMEAINS	// 常にINSデータを送るなら
	lat = InsLat;
	lon = InsLon;
	#else
	lat = fins ? InsLat : GpsLat;
	lon = fins ? InsLon : GpsLon;
	#endif //_NMEAINS
	if (fReverse)	// リバースなら
	{
		crs = Add180(crs);	// 180deg反転
	}
	strcpy(Buff, "$GNRMC,");
	strcat(Buff, StrGpsTime);
	strcat(Buff, ",A,");
	strcat(Buff, F2Str(s, Deg2NMEA(abs(lat)), 4, 5));
	strcat(Buff, lat < 0 ? ",S," : ",N,");
	strcat(Buff, F2Str(s, Deg2NMEA(abs(lon)), 5, 5));
	strcat(Buff, lon < 0 ? ",W," : ",E,");
	strcat(Buff, F2Str(s, spd, 0, 3));
	strcat(Buff, ",");
	strcat(Buff, F2Str(s, (LONG)(crs * 1000.0), 3, 3));
	strcat(Buff, ",");
	strcat(Buff, StrGpsDate);
	strcat(Buff, ",,,A*");
	TxNMEA();	// NMEAセンテンス送信
}
/*----------------------------------------------------------------------------
	GGAセンテンス送信
	書式 void TxGGA(BOOL fins);

	BOOL fins;		INSの場合TRUE
----------------------------------------------------------------------------*/
void TxGGA(BOOL fins)
{
	LONG lon, lat;
	CHAR s[16];

	#if _NMEAINS	// 常にINSデータを送るなら
	lat = InsLat;
	lon = InsLon;
	#else
	lat = fins ? InsLat : GpsLat;
	lon = fins ? InsLon : GpsLon;
	#endif //_NMEAINS

	strcpy(Buff, "$GNGGA,");
	strcat(Buff, StrGpsTime);
	strcat(Buff, ",");
	strcat(Buff, F2Str(s, Deg2NMEA(abs(lat)), 4, 5));
	strcat(Buff, lat < 0 ? ",S," : ",N,");
	strcat(Buff, F2Str(s, Deg2NMEA(abs(lon)), 5, 5));
	strcat(Buff, lon < 0 ? ",W," : ",E,");
	strcat(Buff, "1,");
	if (fins)
	{
		// GPSがFixしているが疑わしいときのINSなら2
		// GPSがFixしていない時のINSなら1
		sprintf(s, "%d,", fGpsDoubt ? 2 : 1);
	}
	else
	{
		sprintf(s, "%d,", GpsSats);
	}
	strcat(Buff, s);
	if (fins)
	{
		// GPSがFixしているが疑わしいときのINSならhdopをそのまま
		// GPSがFixしていない時のINSなら1.0
		strcat(Buff, F2Str(s, fGpsDoubt ?
				(LONG)(GpsHdop * 100.0) : 100L, 0, 2));
	}
	else
	{
		// hdopをそのまま
		strcat(Buff, F2Str(s, (LONG)(GpsHdop * 100.0), 0, 2));
	}
	strcat(Buff, ",");
	strcat(Buff, F2Str(s, GpsAltitude * 10.0, 0, 1));
	strcat(Buff, ",M,");
	strcat(Buff, StrGpsGeoid);
	strcat(Buff, ",M,,*");
	TxNMEA();	// NMEAセンテンス送信
}
/*----------------------------------------------------------------------------
	HDTセンテンス送信(GNSS Commanderでの方位表示用のおまけ機能)
----------------------------------------------------------------------------*/
void TxHDT()
{
	CHAR s[16];

	strcpy(Buff, "$HEHDT,");
	strcat(Buff, F2Str(s, (LONG)(InsCourse * 1000.0), 3, 3));
	strcat(Buff, ",T*");
	TxNMEA();	// NMEAセンテンス送信
}
/*----------------------------------------------------------------------------
	VSPレベル変化割り込み処理
----------------------------------------------------------------------------*/
void VspProc()
{
	extern volatile ULONG VspTime;		// VSPパルス周期[μs]
	extern volatile SHORT VspItvl;		// VSP割り込みからの経過時間
	extern volatile ULONG VspCountZ;	// 前回VSP割り込み時のカウンタ
	static ULONG x;
	ULONG dcount;

	x = micros();	// 今回値
	if (VspTime != 0 || VspCountZ != 0)	// 車速があるか前回値が0でないなら
	{
		dcount = x - VspCountZ;		// 前回からの差分
		// たまに2回連続して割り込みが発生するのをはじく
		if (dcount >= 1000L)
		{
			VspTime = dcount;
		}
	}
	VspCountZ = x;	// 今回値をメモリ
	VspItvl = 0;	// 割り込みインターバルクリア
}
/*----------------------------------------------------------------------------
	車速算出処理
	書式 ret = CalcVsp(FLOAT* afilt, FLOAT dt, FLOAT slopeg, FLOAT accely,
						BOOL freverse, BOOL fpark);

	FLOAT ret;		自車速[km/h]
	FLOAT* afilt;	フィルタ(GPSデータとの位相合わせ)後の自車加速度[G]
	FLOAT dt;		前回からの経過時間[s]
	FLOAT slopeg;	勾配による加速度[G]
	FLOAT accely;	センサ加速度[G]
	BOOL freverse;	バックギア
	BOOL fpark;		パーキングブレーキ
----------------------------------------------------------------------------*/
#define VSPMINV 0.1			// VSP最低車速 0.1[km/h]
#define VSPMINITVL 20		// VSP停止判定インターバル 2[s]x10
#define VSPSTARTG 0.015		// 発進判定加速度偏差[G]
#define VSPDELAYC 30		// G変化による発進判定オフディレイ 3[s]x10
FLOAT CalcVsp(FLOAT* afilt, FLOAT dt, FLOAT slopeg, FLOAT accely,
				BOOL freverse, BOOL fpark)
{
	extern volatile ULONG VspTime;		// VSPパルス周期[μs]
	extern volatile SHORT VspItvl;		// VSP割り込みからの経過時間
	extern volatile ULONG VspCountZ;	// 前回VSP割り込み時のカウンタ
	FLOAT ac, dv;
	static FLOAT v, vz;
	static FLOAT acz;
	static SHORT gtime;
	ULONG vtime;

	AddOnTime(TRUE, &VspItvl);	// VSP割り込みインターバル
	if (VspItvl >= VSPMINITVL)	// 最後のパルスから規定以上経過？
	{
		VspTime = 0;		// パルスインターバルなし
		VspCountZ = 0;		// 過去のパルスなし
	}
	// VspTimeが途中で0に書き換えられる可能性があるのでローカルにコピー
	vtime = VspTime;
	if (vtime > 0)		// パルスがあるなら
	{
		// 車速計算[km/h]
		v = (FLOAT)VspConst / (FLOAT)vtime;
	}
	else
	{
		v = 0.0;
	}

	DecNonZero(&gtime);	// ディレイタイマ減算
	// 停止中にGの変化が閾値を超えた？
	if (v == 0.0 && !fpark && AfterPowerOnTime >= POWERONDELAYC &&
			abs(accely - acz) >= VSPSTARTG)
	{
		gtime = VSPDELAYC;	// 停止中のG変動タイマスタート
	}
	// 最後のパルスから規定未満または停止中にG変動があった？
	if (VspItvl < VSPMINITVL || gtime > 0)
	{
		v = max(v, VSPMINV);	// 停止していないという意味で最低車速を設定
	}
	acz = accely;	// 今回のGセンサー加速度をメモリ

	if (freverse)	// リバースなら
	{
		v *= -1.0;	// 負
	}
	if (v < 5.0 && fpark)	// 車速が低くてパーキングブレーキオンなら
	{
		v = 0.0;	// 停止とする
	}

	v = CollectVspTire(v);				// タイヤの動半径補正

	// メーター車速を微分して前後加速度を求める
	dv = v - vz;	// 車速変化分
	if (v >= 2.0 && vz >= 2.0 && dt > 0.0)
	{
		ac = dv / 3.6 / dt / G1;	// 車速微分前後加速度[G]
	}
	else
	{
		ac = accely;	// 微分値が得られないときはセンサ加速度を入れておく
	}
	// GPSデータと位相合わせ[G]
	Filter(afilt, ac, GPSFILT);
	vz = v;		// 今回の車速をメモリ

	// 勾配による車速補正
	slopeg = min(1.0, abs(slopeg));
	v *= cos(asin(slopeg));

	return v;
}
/*---------------------------------------------------------------------------
	タイヤの動半径補正
---------------------------------------------------------------------------*/
#define MAXCALV 120.0	// 補正する最大車速[km/h]
#define VGAIN0 0.995	// 車速0でのゲイン
FLOAT CollectVspTire(FLOAT v)
{
	FLOAT vgain;

	if (abs(v) > MAXCALV)
	{
		vgain = 1.0;
	}
	else
	{
		vgain = VGAIN0 + abs(v) * (1.0 - VGAIN0) / MAXCALV;
	}
	return v * vgain;
}
/*----------------------------------------------------------------------------
	VSP定数学習処理(1s)
	書式 void LearnVsp(ULONG* vspconst, FLOAT v, FLOAT vgps, BOOL flearn);

	ULONG* vspconst;車速計算係数
	FLOAT vl;		車速[km/h]
	FLOAT vgps;		GPS車速[km/h]
	BOOL flearn;	学習イネイブル
----------------------------------------------------------------------------*/
#define VLEARNFILT 0.1
#define VERR1 3.0		// 最大エラー[km/h]
#define VCMAX 100.0		// 最大エラー時の車速計算係数補正量
void LearnVsp(ULONG* vspconst, FLOAT v, FLOAT vgps, BOOL flearn)
{
	static FLOAT dvflt;
	FLOAT vc;

	if (flearn)	// 学習するなら
	{
		Filter(&dvflt, v - vgps, VLEARNFILT);	// 差分をフィルタリング
		vc = abs(dvflt) * VCMAX / VERR1;
		vc = min(vc, VCMAX);
		if (dvflt > 0.0)		// メータ車速が大きいなら
		{
			*vspconst -= vc;	// 係数を減らす
		}
		else if (dvflt < 0.0)	// さもなければ
		{
			*vspconst += vc;	// 係数を増やす
		}
	}
}
/*----------------------------------------------------------------------------
	ヨーレートセンサの補償とオフセット除去を行い真のヨーレートを得る
	書式 ret = GetTrueYawRate(FLOAT* yofst, SHORT yawraw, FLOAT temp,
					FLOAT gpsyaw, FLOAT v, FLOAT accel,
					BOOL fcurve, BOOL fpark,
					BOOL fstop, BOOL frun);

	FLOAT ret;		ゼロ点補正したヨーレート[deg/s]
	FLOAT* yofstout;ヨーレートセンサオフセット[deg/s]
	SHORT yawraw;	ヨーレートセンサ生値[deg/s]x32768/250
	FLOAT temp;		温度[deg]
	FLOAT gpsyaw;	GPSヨーレート[deg/s]
	FLOAT v;		車速[km/h]
	FLOAT accel;	前後G[G]
	BOOL fcurve;	カーブ走行中フラグ
	BOOL fpark;		パーキングブレーキ
	BOOL fstop;		停止時オフセットキャンセル要求
	BOOL frun;		走行時オフセットキャンセル要求
----------------------------------------------------------------------------*/
#define FILT_OFCANSTOP 0.01		// 停止時オフセット除去フィルタ定数
#define FILT_OFCAN 0.005		// オフセット除去フィルタ定数
#define ACCEL2ANGLE 35.0		// 加速度角度変換係数
FLOAT GetTrueYawRate(FLOAT* yofst, SHORT yawraw, FLOAT temp,
					FLOAT gpsyaw, FLOAT v, FLOAT accel,
					BOOL fcurve, BOOL fpark,
					BOOL fstop, BOOL frun)
{
	FLOAT x, yaw, rpgain, roll, pitch, tofst;
	static FLOAT yawdelay;

	if (AfterPowerOnTime <= 1)	// 起動時は
	{
		yawdelay = yaw;
	}

	// 温度によって生じるオフセット量[deg/s]
	tofst = (temp - CALTEMP) * SLOPE_YAW;
	// オフセット補正前のヨーレート[deg/s]
	yaw = (FLOAT)yawraw * 250.0 / 32768.0;
	// 温度補償したヨーレート[deg/s]
	yaw -= tofst;
	YawRate0 = yaw;	// モニタ用

	// スケールゲイン補正
	yaw = yaw * YAWGAIN;

	// カーブ中ならロール角とピッチ角[rad]を推定
	if (fcurve)
	{
		roll = v / 3.6 * radians(yaw) / G1 * ACCEL2ANGLE;
		roll = min(30.0, roll);		// F/S
		roll = max(-30.0, roll);
		roll = radians(roll);
		pitch = accel * ACCEL2ANGLE;
		pitch = min(30.0, pitch);	// F/S
		pitch = max(-30.0, pitch);
		pitch = radians(pitch);
	}
	else
	{
		roll = 0.0;
		pitch = 0.0;
	}
	// ジャイロZ軸の鉛直線からの傾き[rad]
	x = atan(sqrt(tan(roll) * tan(roll) + tan(pitch) * tan(pitch)));
	x = cos(x);	// Z軸が傾くことによるセンサ出力の減少分
	if (x != 0.0)
	{
		rpgain = 1.0 / x;	// 傾きを補正するためのゲイン
	}
	else
	{
		rpgain = 1.0;
	}
	// ロール＆ピッチ補正
	yaw *= rpgain;

	// GPSヨーレートと位相を合わせたヨーレート
	Filter(&yawdelay, yaw, GPSFILT);

	/*** 停止時ヨーレートゼロ点補正 ***/
	// 停止中のセンサ出力はそのままオフセットといえる
	if (fstop)
	{
		// ヨーレートセンサオフセットアップデート[deg/s]
		Filter(yofst, yaw, fpark ? FILT_OFCANSTOP : FILT_OFCAN);
	}

	/*** 走行時ヨーレートゼロ点補正 ***/
	if (frun && abs(gpsyaw) <= CURVEYAW)
	{
		// ヨーレートセンサオフセットアップデート[deg/s]
		Filter(yofst, yawdelay - gpsyaw, FILT_OFCAN);
	}

	// オフセットを補正したヨーレート
	yaw -= *yofst;

	// ヨーレートを返す
	return yaw;
}
/*----------------------------------------------------------------------------
	慣性航法による緯度＆経度を計算(100ms)
	書式 void CalcInsCoordinate(LONG* lat, LONG* lon, FLOAT cs, FLOAT v,
								FLOAT dt)

	LONG* lat;		緯度計算結果[deg]x1e7
	LONG* lon;		経度計算結果[deg]x1e7
	FLOAT cs;		INS方位[deg](0～360:0=北, 90=東)
	FLOAT v;		車速[km/h]
	FLOAT dt;		サンプリングタイム[sec]
----------------------------------------------------------------------------*/
void CalcInsCoordinate(LONG* lat, LONG* lon, FLOAT cs, FLOAT v, FLOAT dt)
{
	FLOAT L, len, psi, a, thta, angllat, angllon, x, dx, dy;
	static FLOAT vz, csz;

	if (AfterPowerOnTime <= 1)	// 通電直後は初期化
	{
		vz = v;
		csz = cs;
	}
	if (vz > 0.0 && v > 0.0 && dt > 0.0)
	{
		a = (v - vz) / 3.6 / dt;	// 加速度[m/s^2]
	}
	else
	{
		a = 0.0;
	}
	// 進んだ距離 = 弧の長さL = (v + vz ) / 2・t + 1/2・a・t^2[m]
	L = (v + vz) / 2.0 / 3.6 * dt + a * dt * dt / 2.0;
	// 前回からの角度変化ψ[rad]
	psi = radians(Normal180(Normal180(cs) - Normal180(csz)));
	if (psi != 0.0)
	{
		// 弦の長さl = 2・sin(ψ/2)・L / ψ [m]
		len = 2.0 * sin(psi / 2.0) * L / psi;
	}
	else
	{
		len = L;
	}
	// 前回の方位からψ/2ずれた方向に進んだことにする
	thta = radians(csz) + psi / 2.0;
	// 北に進んだ距離
	dy = len * cos(thta);
	// 緯度に変換
	angllat = degrees(atan(dy / 6378150.0));
	// 東に進んだ距離
	dx = len * sin(thta);
	// 緯度による補正量
	x = abs(cos(radians((FLOAT)*lat / F1e7)));
	x = max(x, 0.1);
	// 経度に変換
	angllon = degrees(atan(dx / (6378150.0 * x))); 
	// 新たな位置
	*lat += (LONG)(angllat * F1e7);
	*lon += (LONG)(angllon * F1e7);
	// 現在値をメモリ
	vz = v;
	csz = cs;
}
/*----------------------------------------------------------------------------
	固定小数点数値を文字に変換(0を追加)
	書式 ret = F2Str(CHAR* s, LONG dat, SHORT n, SHORT m)

	CHAR* ret;		データ
	LONG dat;		データ
	CHAR* s;		出力先
	SHORT n;		整数部の文字数(0なら'0'を付けない。'-'は含まない)
	SHORT m;		小数部の文字数(0なら'.'以下は出さない)
----------------------------------------------------------------------------*/
CHAR* F2Str(CHAR* s, LONG dat, SHORT n, SHORT m)
{
	static CHAR buf[16];
	CHAR *p;
	SHORT x, y, i;
	LONG d1, d2, z;

	s[0] = '\0';
	z = 1L;
	for (i = 0; i < m; i++)
	{
		z *= 10L;
	}
	d1 = dat / z;		// 整数部の数値
	d2 = abs(dat) % z;	// 小数部の数値
	p = buf;
	ltoa(d1, p, 10);	// 10進数の文字に変換
	if (*p == '-')		// 負なら
	{
		strcat(s, "-");	// 1つ先から
		p++;
	}
	x = strlen(p);
	y = n - x;		// 出すべき'0'の数
	while (y-- > 0 && n > 0)
	{
		strcat(s, "0");	// 空白の長さ分0を入れる
	}
	strncat(s, p, x);	// 整数部の文字を追加

	p = buf;
	ltoa(d2, p, 10);// 小数部を文字に変換
	x = strlen(p);
	y = m - x;		// 出すべき'0'の数
	if (m > 0)		// 小数点以下を出すなら
	{
		strcat(s, ".");
		while (y-- > 0 && m > 0)
		{
			strcat(s, "0");	// 空白の長さ分0を入れる
		}
		strncat(s, p, x);	// 少数部の文字を追加
	}
	return s;
}
/*----------------------------------------------------------------------------
	2つの座標間の距離を返す
	書式 ret = Distance(LONG lat1, LONG lon1, LONG lat2, LONG lon2);

	FLOAT ret;	距離[m]
	LONG lat1;	緯度1[deg]x1e7
	LONG lon1;	経度1[deg]x1e7
	LONG lat2;	緯度2[deg]x1e7
	LONG lon2;	経度2[deg]x1e7
----------------------------------------------------------------------------*/
FLOAT Distance(LONG lat1, LONG lon1, LONG lat2, LONG lon2)
{
	FLOAT x, y;

	// 横方向の移動距離
	x = sin(radians((FLOAT)(lon2 - lon1) / F1e7)) * 6372795 * 
			abs(cos(radians((FLOAT)lat2 / F1e7)));
	// 縦方向の移動距離
	y = sin(radians((FLOAT)(lat2 - lat1) / F1e7)) * 6372795;
	return sqrt(x * x + y * y);
}
/*----------------------------------------------------------------------------
	180deg反転
	書式 ret = Add180(FLOAT deg)

	FLOAT ret;	方位[deg]
	FLOAT deg;	方位[deg]
----------------------------------------------------------------------------*/
FLOAT Add180(FLOAT deg)
{
	deg += 180.0;			// 180deg反転
	if (deg >= 360.0)
	{
		deg -= 360.0;
	}
	return deg;
}
/*----------------------------------------------------------------------------
	0以上360未満に正規化
	書式 ret = Normal360(FLOAT dat)

	LONG ret;	出力
	LONG dat;	入力
----------------------------------------------------------------------------*/
FLOAT Normal360(FLOAT dat)
{
	while (dat >= 360.0) dat -= 360.0;
	while (dat < 0) dat += 360.0;
	return dat;
}
/*----------------------------------------------------------------------------
	-180以上180未満に正規化
	書式 ret = Normal180(FLOAT dat)

	LONG ret;	出力
	LONG dat;	入力
----------------------------------------------------------------------------*/
FLOAT Normal180(FLOAT dat)
{
	while (dat >= 180.0) dat -= 360.0;
	while (dat < -180.0) dat += 360.0;
	return dat;
}
/*----------------------------------------------------------------------------
	フィルタ
	書式 void Filter(FLOAT* filt, FLOAT dat, FLOAT fact);

	FLOAT	filt;	入力(1サンプル前)→出力
	FLOAT	dat;	入力(今回)
	FLOAT fact;		フィルタ定数
----------------------------------------------------------------------------*/
void Filter(FLOAT* filt, FLOAT dat, FLOAT fact)
{
	if (AfterPowerOnTime <= 1)	// 通電直後は初期化
	{
		*filt = dat;
		return;
	}
	*filt = (1.0 - fact) * *filt + fact * dat;
}
/*----------------------------------------------------------------------------
	Nバイトシリアル出力
	書式 void SprintN(UCHAR* buf, SHORT len);

	UCHAR* buf;	文字列バッファ
	SHORT len;	データ長
----------------------------------------------------------------------------*/
void SprintN(UCHAR* buf, SHORT len)
{
	while (len--)
	{
		Serial2.write(*(buf++));	// GPS送信先はソフトシリアル
	}
}
/*----------------------------------------------------------------------------
	EEP読み書き処理
----------------------------------------------------------------------------*/
// EEPROM構造体
struct EEPobj
{
	LONG vc;
	LONG lon;
	LONG lat;
	FLOAT cs;
	FLOAT yofst;
	UCHAR sum;
};
/*----------------------------------------------------------------------------
	EEP書き込み処理
----------------------------------------------------------------------------*/
void EepWrite()
{
	EEPobj strct;
	SHORT i;
	UCHAR sum = 0, *p;

	strct.vc = VspConst;
	strct.lon = InsLon;
	strct.lat = InsLat;
	strct.cs = InsCourse;
	strct.yofst = YawOffset;

	p = (UCHAR *)&strct;
	for (i = 0; i < sizeof(strct) - 1; i++)
	{
		sum += *p;
	}
	strct.sum = ~sum;
	EEPROM.put(0, strct);
}
// EEP初期データ書き込み
void EepInitialWrite()
{

	InsLon = 1397686040;	// 東京駅
	InsLat = 356796388;
	InsCourse = 0.0;

	VspConst = VSPCONST;
	YawOffset = 0.0;
	EepWrite();
}
/*----------------------------------------------------------------------------
	EEP読出し処理
----------------------------------------------------------------------------*/
BOOL EepRead()
{

	EEPobj strct;
	SHORT i;
	UCHAR sum = 0, *p;

	EEPROM.get(0, strct);
	p = (UCHAR *)&strct;
	for (i = 0; i < sizeof(strct) - 1; i++)
	{
		sum += *p;
	}
	sum = ~sum;
	if (sum != strct.sum)	// チェックサムエラー
	{
		Serial.println(F("EEPROMサムエラーのため初期化"));
		EepInitialWrite();	// EEP初期データ書き込み
	}
	// 学習値を設定
	VspConst = strct.vc;
	InsLon = strct.lon;
	InsLat = strct.lat;
	InsCourse = strct.cs;
//	YawOffset = strct.yofst;	// 温特補正したので書き戻さない方が良いかも？
	// モニタ出力の初期値が0にならないように
	GpsLon = InsLon;
	GpsLat = InsLat;
	if (InsLon == 0)	// F/S:妥当な値が設定されなかった場合
	{
		InsLon = GpsLon = 1397686040;	// 東京駅
		InsLat = GpsLat = 356796388;
	}

#if !_HILS	// HILS用ビルドでないなら
	#if _HEADER	// タイトル表示するなら
	// 表示
	Sprint(F("VspC:"));
	Sprintln(VspConst);
	Sprint(F("Pos:"));
	Sprintc(InsLon);
	Sprintc(InsLat);
	Sprintln(InsCourse);
	Sprint(F("Yofst:"));
	Sprintfln(YawOffset, 3);
	#endif //_HEADER
#endif //_HILS

	return TRUE;	// OK
}
/*----------------------------------------------------------------------------
	モニタ用に変数をシリアル出力
----------------------------------------------------------------------------*/
void MonitorOut()
{
	#if _HEADER	// タイトル表示するなら
	if (AfterPowerOnTime == 1)
	{
		Sprint(F("time,Vreal,GpsVreal,AyRaw,VspC,"));
		Sprint(F("YawRaw,"));
		Sprint(F("G経度,G緯度,I経度,I緯度,"));
		Sprint(F("GpsYaw,Yaw,Yofst,RollRaw,"));
		Sprintln(F("G方位,I方位,bug,bug1,TempRaw,SNR,Stat"));
	}
	#endif //_HEADER
	// 時間[s]
	Sprintfc(TxDt, 2);
	// 車速[km/h]
	Sprintfc(Vreal, 2);
	// GPS車速[km/h]
	Sprintfc(GpsVreal, 2);
	// 前後Gセンサ生値[G]x32768/2
	Sprintc(AccelYRaw);
	if (AfterPowerOnTime == 1)
	{
		// 車速変換定数
		Sprintc(VspConst);
	}
	else
	{
		// VSPパルス周期[μs]
		Sprintc(VspTime);
	}
	// ヨーレートセンサ生値[deg/s]x32768/250
	Sprintc(YawRaw);
	// 経度[deg]x1e7
	Sprintc(GpsLon);
	// 緯度[deg]x1e7
	Sprintc(GpsLat);
	// 慣性航法経度[deg]x1e7
	Sprintc(InsLon);
	// 慣性航法緯度[deg]x1e7
	Sprintc(InsLat);
	// GPSヨーレート[deg/s]
	Sprintfc(GpsYawRate, 4);
	// ヨーレート[deg/s]
	Sprintfc(YawRate, 4);
	// ヨーレートオフセット[deg/s]
	Sprintfc(YawOffset, 4);
	// 衛星数
	Sprintc(GpsSats);
	// GPS方位[deg]
	Sprintfc(GpsCourse, 3);
	// 慣性航法方位[deg]
	Sprintfc(InsCourse, 3);
	// デバッグ情報
	Sprintfc(bug, 4);
	Sprintfc(bug1, 4);
	// 温度センサ生値([deg]-36.53)*340
	Sprintc(TempRaw);
	// SNR
	Sprintfc(SatLevel, 1);
	// Status
	Sprintln(fReverseRun * 16 +
			fCurve * 8 + fGpsDoubt * 4 + fGpsFix * 2 + fStop);
}
/*----------------------------------------------------------------------------
	デバッグ用変数をシリアルに出力
----------------------------------------------------------------------------*/
void printBugs()
{
	Sprint(F("bug="));
	Sprintf(bug,3);
	Sprint(F(", bug1="));
	Sprintf(bug1,3);
	Sprint(F(", bug2="));
	Sprintf(bug2,3);
	Sprint(F(", bug3="));
	Sprintf(bug3,3);
	Sprint(F(", bug4="));
	Sprintf(bug4,3);
	Sprint(F(", bug5="));
	Sprintf(bug5,3);
	Sprint(F(", bug6="));
	Sprintf(bug6,3);
	Sprint(F(", bug7="));
	Sprintf(bug7,3);
	Sprint(F(", bug8="));
	Sprintf(bug8,3);
	Sprint(F(", bug9="));
	Sprintfln(bug9,3);
}
// 温度、ヨー、前後Gセンサ生値
void printTempYawG()
{
	static CHAR t;
	
	if (++t >= 10)
	{
		Sprintc(TempRaw);
		Sprintc(YawRaw);
		Sprintln(AccelYRaw);
		t = 0;
	}
}
// 温度補償したヨー、前後G
void printYawG()
{
	static CHAR t;
	
	if (++t >= 10)
	{
		Sprintfc(Temperature / 10.0, 3);// 温度の1/10
		Sprintfc(YawRate0 * 10.0, 3);	// ヨーレートの10倍
		Sprintfln(AccelY * 100.0, 3);	// 前後Gの100倍
		t = 0;
	}
}
#if _HILS	// HILS用ビルドなら
/*----------------------------------------------------------------------------
	HILSデータ受信
----------------------------------------------------------------------------*/
void HILS_RxData()
{
	CHAR c, *p;
	static FLOAT timz;
	static ULONG loopt;
	static BOOL fstx;
	static FLOAT vz;
	static SHORT cnt;
	static LONG lonz, latz;
	static FLOAT dtimeini;
	FLOAT x;

	loopt = millis();
	while (1)
	{
	  if (Serial.available() > 0)	// 文字を受信していれば
	  {
		c = Serial.read();	// 1文字受信
		if (BufPtr < BUFFMAX - 2)	// バッファに余裕があるなら
		{
			Buff[BufPtr++] = c;// 格納
			if (c == '\n')		// 行末か？
			{
				Buff[BufPtr] = '\0';	// 文字列をターミネート
				if (strncmp(Buff, "STX", 3) == 0)	//ネゴシエーション開始
				{
					fstx = TRUE;
					RxCount = 0;
					AfterPowerOnTime = 0;
					StopTime = 0;
					GpsFixCount = 0;
					cnt = 0;
					lonz = latz = 0;
					fTimeOut = TRUE;
					fReverseRun = FALSE;
					fStop = TRUE;
					Sprintln("OK");
					BufPtr = 0;	// ポインタを先頭に
				}
				else if (fstx)		// 初期値受信
				{
					fstx = FALSE;
	 				p = strtok(Buff, ",");
					YawOffset = atof(p);
	 				p = strtok(NULL, ",");
					InsLon = atol(p);
	 				p = strtok(NULL, ",");
					InsLat = atol(p);
	 				p = strtok(NULL, ",");
					InsCourse = atof(p);
					BufPtr = 0;	// ポインタを先頭に
				}
				else
				{
					p = strtok(Buff, ",");
					Dtime = atof(p);
					if (RxCount == 0)
					{
						dtimeini = Dtime - 0.1;
						timz = 0;
					}
					Dtime -= dtimeini;
					Dt = Dtime - timz;
					p = strtok(NULL, ",");
					x = atof(p);
					fReverse = BOOLSET(x < 0.0);
					VspTime = (LONG)abs(x);
					VspItvl = (VspTime == 0) ? VSPMINITVL : 0;
					if (RxCount == 0 && Vreal <= 0.0)
					{
						StopTime = 999;
					}
					p = strtok(NULL, ",");
					GpsVreal = atof(p);
					p = strtok(NULL, ",");
					AccelYRaw = atoi(p);
					p = strtok(NULL, ",");
					YawRaw = atoi(p);
					p = strtok(NULL, ",");
					RollRaw = atoi(p);
					p = strtok(NULL, ",");
					TempRaw = atoi(p);
					p = strtok(NULL, ",");
					if (p != NULL)
					{
						GpsLon = atol(p);
					}
					p = strtok(NULL, ",");
					if (p != NULL)
					{
						GpsLat = atol(p);
					}
					p = strtok(NULL, ",");
					GpsYawRate = atof(p);
					p = strtok(NULL, ",");
					GpsCourse = Normal360(atof(p));
					p = strtok(NULL, ",");
					SatLevel = atof(p);
					p = strtok(NULL, ",");
					fGpsFix = BOOLSET(atoi(p) & 0x02);
					fGpsFixGGA = fGpsFixRMC = fGpsFix;

					timz = Dtime;
					BufPtr = 0;	// ポインタを先頭に
					RxCount++;

					// GPS変化したか時間が経過した？
					if (lonz != GpsLon || latz != GpsLat || vz != GpsVreal ||
						cnt == 0)
					{
						// GPS全受信FIX
						AddOnTime(fGpsFix, &GpsFixCount);
						fGpsRecv = TRUE;
						cnt = 10 + 1;

						// GPSが確定していたらフィルタリングしておく
						if (GpsFixCount >= 2 && !fGpsDoubt)
						{
							Filter(&SatLevelFilt, SatLevel, GPSFILT);
						}
					}
					lonz = GpsLon;
					latz = GpsLat;
					vz = GpsVreal;
					// (初回後)11→10, 10→9,,,1→0,(次回後)11→10,,,
					DecNonZero(&cnt);
					break;
				}
			}
		}
		else
		{
			BufPtr = 0;	// ポインタを先頭に
		}
	  }
	  else if (millis() - loopt > 2000)	// タイムアウト？
	  {
		loopt = millis();
		AfterPowerOnTime = 0;
		cnt = 0;
		lonz = latz = 0;
		BufPtr = 0;	// ポインタを先頭に
	  }
	}
}
/*----------------------------------------------------------------------------
	HILS用に変数をシリアルに出力
----------------------------------------------------------------------------*/
void HILSOut()
{
	CHAR s[16];

	// 慣性航法経度[deg]x1e7
	Sprintc(InsLon);
	// 慣性航法緯度[deg]x1e7
	Sprintc(InsLat);
	// ヨーレート[deg/s]
	Sprintfc(YawRate, 4);
	// ヨーレートオフセット[deg/s]
	Sprintfc(YawOffset, 4);
	// 慣性航法方位0～360[deg]
	Sprintfc(InsCourse, 3);
	// 車速変換定数
	Sprintc(VspConst);
	// GPS状態
	Sprintc(fReverseRun * 16 +
				fCurve * 8 + fGpsDoubt * 4 + fGpsFix * 2 + fStop);
	// デバッグ情報
	Sprintfc(bug, 4);
	Sprintfc(bug1, 4);
	Sprintfc(bug2, 4);
	Sprintfc(bug3, 4);
	Sprintfc(bug4, 4);
	Sprintfln(bug5, 4);
}
#endif //_HILS
/*----------------------------------------------------------------------------
	16進文字を10進に変換
	書式 ret = Hex2Dec(CHAR a);

	SHORT ret;	出力数値
	CHAR a;		入力文字
----------------------------------------------------------------------------*/
SHORT Hex2Dec(CHAR a)
{
	if (a >= 'A' && a <= 'F')
	{
		return a - 'A' + 10;
	}
	else if (a >= 'a' && a <= 'f')
	{
		return a - 'a' + 10;
	}
	else
	{
		return a - '0';
	}
}
/*----------------------------------------------------------------------------
	','カンマで区切られた文字列の手前部分を返す
	書式 ret = strtokComma(CHAR* s);

	CHAR* ret;	抽出文字列(存在しなければNULLを返す)
	CHAR* s;	入力文字列(2回目以降はNULLを引数にする)
----------------------------------------------------------------------------*/
#define STRBUFMAX 16
CHAR* strtokComma(CHAR* s)
{
	static CHAR* p0;
	static CHAR buf[STRBUFMAX];
	CHAR* p;
	SHORT len;

	if (s)
	{
		p0 = s;	// 初期の文字の先頭
	}
	if (*p0 == '\0')	// 最後まで行っていたら
	{
		return NULL;	// NULLを返す
	}
	p = strchr(p0, ',');	// ','の位置
	if (p)	// 発見した？
	{
		len = p - p0;		// ','手前までの文字数
		if (len < STRBUFMAX)// バッファに入るなら
		{
			strncpy(buf, p0, len);	// 文字をコピー
			*(buf + len) = '\0';	// ターミネート
		}
		else
		{
			strcpy(buf, "#over#");	// エラー
		}
		p0 = p + 1;	// ','の次の位置を次回先頭に
	}
	else	// 発見できなかった→最後の文字列だった
	{
		len = strlen(p0);
		p0 += len;		// 文字列の最後の'\0'をポイント
		return p0 - len;// 文字列をそのまま返す
	}
	return buf;	// 抽出した文字列を返す
}
/*** end of "ExtGPS.ino" ***/
