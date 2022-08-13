Option Explicit
Public DataBuf() As Variant

'Const YAWGAIN As Single = 1.000041934
Const YAWGAIN As Single = 1.001

Dim AfterPowerOnTime As Long
Dim bug As Double, bug1 As Double, bug2 As Double, bug3 As Double
Dim bug4 As Double, bug5 As Double
Dim bugf As Single
Dim YawOffset As Single, RollOffset As Single , PitchOffset As Single
Dim InsCourse As Single, InsCourse0 As Single
Dim Vreal As Single, GpsVreal As Single, Vreal0 As Single, VrealLog As Single
Dim VrealFilt As Single
Dim YawRaw As Integer, YawRate As Single
Dim Dt As Single
Dim GpsYawRate As Single, GpsCourse As Single, GpsCourseCalc As Single
Dim fGpsFix As Boolean
Dim fGpsConfirm As Boolean, fGpsYawConfirm As Boolean
Dim GpsLon As Long, GpsLat As Long, InsLon As Long, InsLat As Long
Dim GpsLonZ As Long, GpsLatZ As Long
Dim GpsFixCount As Long
Dim fGpsDoubt As Boolean, fGpsDoubt2 As Boolean
Dim fReverse As Boolean
Dim fReverseRun As Boolean
Dim ArealFilt As Single, ArealFiltZ As Single
Dim Jark As Single
Dim GpsHdop As Single
Dim fStop As Boolean
Dim fGpsRecv As Boolean
Dim SimLostTime As Single
Dim SimDoubtTime As Single
Dim AccelYRaw As Single
Dim Roll As Single, Pitch As Single
Dim fcurve As Boolean
Dim VspConst As Long, VspTime As Long
Dim SlopeG As Single
Dim RunTime As Long
Dim StopTime As Long
Dim StatDoubt As Integer
Dim SatLevelSum As Single		'// GPS SN比合計
Dim SatLevel As Single			'// GPS SN比平均
Dim SatLevelFilt As Single		'// GPS SN比平均フィルタ値
Dim SatCount As Integer			'// GPS SN比平均データ数
Dim fBrakeDecel As Boolean
Dim fBrakeRecover As Boolean
Dim fPark As Boolean
Dim Temperature As Single
Dim YawRate0 As Single
Dim AccelY As Single
Dim AccelYFilt As Single

Const GFILT As Double = 0.08
Const L1e7 As Long = 10000000
Const F1e7 As Single = 10000000#
Const G1 As Single = 9.80665
Const GPSFILT As Single = 0.17
Const YAWV As Single = 10#			'// GPS方位が得られる車速[km/h]
Const CURVEYAW As Single = 2#		'// カーブ判定ヨーレート [deg/s]
Const POWERONDELAYC As Long = 0		'// 通電直後の安定待ち時間[s]x10
Const POWERONDELAYYAW As Long = 600	'// 通電直後の安定待ち時間[s]x10
Const VSPLEARN_LV As Single = 20	'// VSP学習最低車速[km/h]
Const WHEELBASE As Single = 2530
Const TREAD As Single = 1470
Const CALTEMP As Single = 24.75	'// キャリブレーション時の温度
Const SLOPE_YAW As Single = -1.3527291E-02
Const SLOPE_G As Single = 1.4434167E-04

Sub GraphSim()
	Sheets("Log").Select
	Sim
	Sheets("Graph1").Select
end Sub

'-----------------------------------------------------------------------------
'文字列中の文字の出現個数を返す
Function StrCount(Source As String, Target As String) As Long
	Dim n As Long, cnt As Long
	do
		n = InStr(n + 1, Source, Target)
		if n = 0 Then
			Exit Do
		else
			cnt = cnt + 1
		end if
	Loop
	StrCount = cnt
end Function
'-----------------------------------------------------------------------------
'隙間を計算式で埋める
Private Sub Fill2end()
	Dim y1 As Long, y2 As Long

	y1 = Range("B2").end(xlDown).Row	'データの最初の行数
	y2 = Range("W2").end(xlDown).Row	'計算式の最初の行数
	if y1 > y2 Then 'データの方が多い場合は隙間を計算式で埋めるW=23
		Range(Cells(y2, 23), Cells(y2, 30)).Copy _
			Destination:= _
			Range(Cells(y2 + 1, 23), Cells(y1, 30))
	elseif y1 < y2 Then '計算式の方が多い場合はクリアする
		Range(Cells(y1 + 1, 23), Cells(y2, 30)).Clear
	end if
	y2 = Range("AL2").end(xlDown).Row	'計算式の最初の行数
	if y1 > y2 Then 'データの方が多い場合は隙間を計算式で埋めるW=23
		Range(Cells(y2, 38), Cells(y2, 45)).Copy _
			Destination:= _
			Range(Cells(y2 + 1, 38), Cells(y1, 45))
	elseif y1 < y2 Then '計算式の方が多い場合はクリアする
		Range(Cells(y1 + 1, 38), Cells(y2, 45)).Clear
	end if
end Sub
'-----------------------------------------------------------------------------

'-----------------------------------------------------------------------------
'ログファイルの読み込み
Sub LoadFile()
	Dim fileToOpen As Variant
	Dim fso As Object, tstream As Object
	Dim sTmp As String
	Dim sLine() As String
	Dim ColNum As Long
	Dim DatNum As Long
	Dim sDat() As String
	Dim i As Long
	Dim j As Long
	Dim item As Variant
	Dim time0 As Double

	'EXCELファイルのある場所へ移動
	ChDrive ThisWorkbook.Path
	ChDir ThisWorkbook.Path

	'ファイルオープンダイアログ
	fileToOpen = Application.GetOpenFilename("ログファイル,*.txt")
	'キャンセルされたら何もしない
	if (fileToOpen = FALSE) Then
		Exit Sub
	end if

	'一度ファイルの中身をすべて変数に移す
	Set fso = CreateObject("Scripting.FileSystemObject")
	Set tstream = fso.OpenTextFile(fileToOpen)
	sTmp = tstream.ReadAll				'すべてを読み込む
	ReDim DataBuf(30000, 31)			'最大行と桁分を確保してクリア

	sLine = Split(sTmp, vbCrLf) 		'一行ごとに分解
	ColNum = StrCount(sLine(0), ",") + 1 '最大カラム数を求める
	DatNum = UBound(sLine)				'データ数を求める
	if(DatNum > 30000) Then DatNum = 30000
	for i = 0 To DatNum - 1 			'すべての行
		sDat = Split(sLine(i), ",") 	'一行をカンマ区切りにして配列に移す
		j = 0
		for Each item In sDat
			DataBuf(i, j) = item 		'配列に格納
			if (i > 0 And j = 0) Then
				DataBuf(i, j) = DataBuf(i, j) - time0	'時間を0からに
			end if
			j = j + 1
		Next item
		if (i = 0) Then
			time0 = DataBuf(0, 0)
			DataBuf(0, 0) = 0.0
		end if
	Next
	tstream.Close			'テキストストリームのClose
	Set tstream = Nothing	'オブジェクトクリア
	Set fso = Nothing

	'シートに書き込む
	Range("B4:AY65536").Clear
	Range("B2:V30002") = DataBuf

	'隙間を計算式で埋める
	Fill2end
	'シミュレーション計算
	Sim
	'軸スケールをグラフ1から2にコピー
'	CopyAxis1to2
end Sub

'/*---------------------------------------------------------------------------
'   シミュレーション計算
'---------------------------------------------------------------------------*/
Sub Sim()
	Const RUNRV As Single = 5#			'// リバース走行判定解除車速[km/h]
	Const STOPTIMEC As Long = 10		'// 停止判定時間[s]x10
	Const VSPLEARN_HV As Single = 180	'// VSP学習最高車速[km/h]
	Const VSPLEARN_A As Single = 0.05	'// VSP学習最高加速度[G]
	Const GOFSTFILT As Single = 0.0001	'// 加速度センサオフセットフィルタ定数
	Const GOFSTC  = 0.05				'// 加速度センサオフセット算出G[G]
	Const PERRGAIN As Single = 80.0		'// 位置リセット閾値車速ゲイン
	Const PERROFST As Single = 1000.0	'// 位置リセット閾値オフセット
	Const CERR As single = 45.0			'// 方位リセット閾値[deg]
	Const ERRTIME As Long = 10		'// INSをリセットするまでの時間閾値[回]
	Const INSFILT As Single = 0.2		'// INS GPS位置一致フィルタ値
	Const INSFILTCRS As Single = 0.05	'// INS GPS方位一致フィルタ値
	Const FILT_OFCANROLL As Single = 0.005'// 停止時オフセット除去フィルタ定数
	Const SLOPEDEADG As Single = 0.04	'// 勾配加速度不感帯[G]
	Const SATLEVELMAX As Single = 50
	Const GPSLEVELFILT As Single = 0.01

	Dim y As Long
	Dim ans As Variant
	Dim tim As Single
	Dim x As Single
	static rtime As Long
	static yawratefilt As Single
	Dim yawratez As Single, yawratefiltz As Single
	Dim yawratezz As Single
	static cnt As Integer
	Dim errfilt As Single
	Dim tofst As Single
	Dim gcount As Long
	Dim poserrcount As Long, crserrcount As Long
	Dim laterr As Single, lonerr As Single, crserr As Single
	Dim poserr As Single
	Dim Gdist As Single, Idist As single
	Dim MaxDecel As Single
	Dim vofst As Single
	Dim TempRaw As Integer
	Dim cofst As Single
	Dim sgfilt As Single
	Dim latcal As Single, loncal As Single, crscal As single

	Gdist = 0
	Idist = 0
	AfterPowerOnTime = 0
	fReverseRun = FALSE
	fCurve = FALSE
	Roll = 0
	Pitch = 0
	ArealFilt = 0
	ArealFiltZ = 0
	GpsFixCount = 0
	poserrcount = 0
	crserrcount = 0
	yawratezz = 0
	yawratez = 0
	bug=0
	bug1=0
	bug2=0
	bug3=0
	bugf=0
	fBrakeDecel = FALSE
	fBrakeRecover = FALSE
	laterr = 0
	lonerr = 0
	crserr = 0


	SimLostTime = Cells(3, 1)	'sim 衛星をロストする時間
	SimDoubtTime = Cells(5, 1)	'sim 衛星が疑わしくなる時間

	YawOffset = Cells(2, Range("N1").Column)	'オフセット初期値 y(r)
	VspConst = Cells(2, Range("F1").Column)
	if (VspConst < 1300000) Then _
		VspConst = Cells(2, Range("R1").Column) * 10000
	if (VspConst < 1400000 Or VspConst > 1500000) Then _
		VspConst = Cells(2, Range("R1").Column) * 10000
	if (VspConst < 1400000 Or VspConst > 1500000) Then _
		VspConst = Cells(2, Range("AL1").Column)
	if (VspConst < 1400000 Or VspConst > 1500000) Then _
		VspConst = 1470000

	InsCourse = Cells(2, Range("Q1").Column)	'I初期方位=ログに従う
'	InsCourse = Cells(2, Range("P1").Column)	'I初期方位=G初期方位
	InsLon = Cells(2, Range("J1").Column)	'INS初期経度=ログに従う
	InsLat = Cells(2, Range("K1").Column)	'INS初期緯度=ログに従う
'	InsLon = Cells(2, Range("H1").Column)	'INS初期経度=GPS初期経度
'	InsLat = Cells(2, Range("I1").Column)	'INS初期経度=GPS初期緯度
	crscal = GetPI(0, TRUE)
	GpsCourse = Cells(2, Range("P1").Column)	'G方位
	GpsLatZ = InsLat
	GpsLonZ = InsLon

	Vreal = Cells(2, Range("C1").Column)	'[km/h]
	RunTime = 0
	StopTime = 0
	if (Vreal > 10#) Then RunTime = 100
	if (Vreal = 0#) Then StopTime = 100
	SatLevelFilt = 30
	SatLevel = 30
	VrealFilt = Vreal

	Application.ScreenUpdating = FALSE	'画面の更新を停止
	Application.Calculation = xlCalculationManual	'自動計算停止

	Dt=0.1
	y = 2
	do while (Cells(y, Range("B1").Column) <> "")
		Call AddOnTime(TRUE, AfterPowerOnTime)	'// 通電後の時間[s]x10
		tim = Cells(y, Range("B1").Column)
		VrealLog = Cells(y, Range("C1").Column)	'[km/h]
		GpsVreal = Cells(y, Range("D1").Column) '[km/h]
		GpsYawRate = Cells(y, Range("L1").Column)	'GpsYawRate [deg/s]
		GpsCourse = Cells(y, Range("P1").Column)	'G方位
		SatLevel = Cells(y, Range("U1").Column)
		if (SatLevel > 50) then SatLevel = 30
		if (tim < 1) then
			YawOffset = Cells(y, Range("N1").Column)
		end if

'		fReverse = VrealLog < 0#
		fReverse = Cells(y, Range("V1").Column) And 16	'// ログに従う
		Call AddOnTime(fReverse, rtime) 	'// リバース信号オン時間
		if (rtime >= 1) Then					'// リバースギアを入れたら
			fReverseRun = TRUE				'// リバース状態確定
		elseif (rtime = 0 And Vreal >= RUNRV) Then	'// 前進したら
			fReverseRun = FALSE 			'// リバース状態解除
		end if

		fGpsFix = Cells(y, Range("V1").Column) And 2
		if (SimLostTime > 0 And tim >= SimLostTime) Then
			fGpsFix = FALSE
		end if

		if (y > 2) Then
			Dt = Cells(y, 2) - Cells(y - 1, 2)		'[s]
		end if
		VspTime = Cells(y, Range("F1").Column)

		'// YawGセンサ生データ取得[deg/s]x250/32768
		'// 左右方向をX、前後方向をYとする。右が正、前が正
		YawRaw = Cells(y, Range("G1").Column)		'[deg/s]x32768/250
		TempRaw = Cells(y, Range("T1").Column) 
		AccelYRaw = Cells(y, Range("E1").Column)	'[G]x32768/2
		Temperature = TempRaw / 340.0 + 36.53
'bug=Temperature
		tofst = (Temperature - CALTEMP) * SLOPE_G '// Gセンサオフセット
'bug=tofst
		AccelY = AccelYRaw * 2# / 32768#	'センサーの前後加速度[G]
		AccelY = AccelY - tofst

		'// 車速算出処理
		Vreal = CalcVsp(ArealFilt, Dt, SlopeG, AccelY, YawRate, fCurve, _
							fReverse, FALSE)
		Call Filter(VrealFilt, Vreal, GPSFILT)
		if (AfterPowerOnTime <= 1) Then '// 起動時は
			Vreal0 = VrealLog
		end if
		fPark = (tim < 1 And Vreal = 0)

		'道路勾配算出
		'// オフセット除去して車速から求めた加速度と位相合わせ
		Call Filter(AccelYFilt, AccelY, GPSFILT)
		'// 差分が勾配による加速度
		Call Filter(sgfilt, AccelYFilt - ArealFilt, GPSFILT)
		SlopeG = sgfilt
		if (Abs(sgfilt) < SLOPEDEADG) Then SlopeG = 0
'bug=ArealFilt*100
'bug1=accelyfilt*100
'bug=SlopeG*100
'bug3=(AccelYFilt - ArealFilt)*100

		Call AddOnTime(Vreal >= YAWV And Not fReverse, RunTime)
		Call AddOnTime(Vreal = 0# And Not fReverse, StopTime)
'		fStop = StopTime >= STOPTIMEC	'// 停止判定
		fStop = Cells(y, Range("V1").Column) And 1	'// ログに従う

		if (Cells(y, Range("H1").Column) <> "") Then
			GpsLon = Cells(y, Range("H1").Column)	'GPS経度[deg]x1e7
			GpsLat = Cells(y, Range("I1").Column)	'GPS緯度[deg]x1e7
'		else
'			GpsLon = Cells(y, Range("J1").Column)	'INS経度[deg]x1e7
'			GpsLat = Cells(y, Range("K1").Column)	'INS緯度[deg]x1e7
		end if

		'// GPSデータ受信処理
		Call RxGPS
'bug=SatLevel
'bug1=SatLevelFilt
		'// GPSが疑わしい判断
		fGpsDoubt = JudgeGpsDoubtLevel(Vreal, GpsVreal, fGpsFix, _
						SatLevel, SatLevelFilt) Or fGpsDoubt2
		if (SimDoubtTime > 0 And tim >= SimDoubtTime) Then
			fGpsDoubt = TRUE
		end if
		'// GPS精度確定
		fGpsConfirm = GpsFixCount >= 2 And Not fGpsDoubt And Not fReverseRun

		'// GPSヨーレート確定
		'  (GPS2回以上確定でバック走行でなくて車速が規定以上)
		fGpsYawConfirm = fGpsConfirm And Vreal >= YAWV

'bug=SatLevelFilt
'bug1=SatLevel

		'// オフセット除去＆取付角度補正して真のヨーレートを得る[deg/s]
		YawRate = GetTrueYawRate(YawOffset, YawRaw, Temperature, GpsYawRate, _
					Vreal, ArealFilt, fCurve, fPark, _
					fStop, _
					fGpsYawConfirm And Not fCurve And fGpsRecv)

		'// カーブ判定
		fCurve = JudgeCurve(YawRate, GpsYawRate, GpsFixCount)

		'// ヨーレートを累積してINS方位0～360[deg]を得る
		if (Not fPark) then
			InsCourse = Normal360(InsCourse + (yawratez + YawRate) / 2.0 * Dt)
		end if
		'// 今回値をメモリ
		yawratez = YawRate

'If (AfterPowerOnTime = 2200) Then InsCourse = InsCourse + 20

		'// INS自車位置推定
		Call CalcInsCoordinate(InsLat, InsLon, InsCourse, Vreal, Dt)

'bug=InsCourse
'bug1=GpsCourse
'bug3=fcurve * -160
		'// GPSから受信した直後
		if (fGpsRecv) Then
			fGpsRecv = FALSE	'// GPS受信フラグクリア
'bug=GpsVreal
'bug1=Vreal
'bug2=GpsVreal - Vreal
			'// 車速から疑わしい判断を追加
			fGpsDoubt2 = JudgeGpsDoubtVreal(Vreal, GpsVreal, GpsFixCount)
			fGpsDoubt = fGpsDoubt Or fGpsDoubt2
			'// 疑わしければ
			if (fGpsDoubt2) then
				fGpsConfirm = false
				fGpsYawConfirm = false
			end if
			if (fGpsConfirm) Then
				'// 受信レベルのフィルタリング
				Call Filter(SatLevelFilt, SatLevel, GPSLEVELFILT)

				'// INSとGPSの差分
				laterr = GpsLat - InsLat
				lonerr = GpsLon - InsLon
				'// INS座標をGPS座標に一致させるための補正量
				latcal = laterr * INSFILT
				loncal = lonerr * INSFILT
				'// INS座標をGPS座標に徐々に一致させる
				InsLat = InsLat + latcal
				InsLon = InsLon + loncal
			else
				laterr = 0
				lonerr = 0
				latcal= 0
				loncal = 0
			end if
			if (fGpsYawConfirm And Not fCurve) then
				crserr = Normal180( _
					Normal180(GpsCourse) - Normal180(InsCourse))
				'// INS方位をGPS方位に一致させるための補正量
				crscal = crserr * INSFILTCRS
				'// INS方位をGPS方位に徐々に一致させる
				InsCourse = Normal360(InsCourse + crscal)
			else
				crserr = 0
				crscal = 0
			end if
			'// GPSデータが疑わしい判断直後に過去の補正量を戻す
			'Call UndoCollect(InsCourse, crscal, fGpsDoubt)
'bug=crscal
'bug=GpsVreal - VrealFilt
'bug1=ArealFilt * 10
'bug2=Normal180(Normal180(GpsCourse) - Normal180(InsCourse))
'bug2=VrealFilt / 10
'bug3=YawRate / 10
			'// INSとGPSが大きく乖離した回数
			poserr = Abs(Vreal) * PERRGAIN + PERROFST'// 位置ずれ判定距離
			Call AddOnTime(fGpsConfirm And _
					(Abs(laterr) >= poserr Or Abs(lonerr) >= poserr), _
					poserrcount)
			Call AddOnTime(fGpsYawConfirm And Abs(crserr) >= CERR, _
					crserrcount)

			'// INSとGPSが大きく乖離した時の初期化
			if (poserrcount >= ERRTIME Or crserrcount >= ERRTIME) Then
				InsLat = GpsLat
				InsLon = GpsLon
				InsCourse = GpsCourse
			end if
			if (SimDoubtTime > 0 And tim < SimDoubtTime) Then
				InsLon = GpsLon
				InsLat = GpsLat
			end if

			'// VSP定数学習処理
					'// GPS取得かつ疑わしくなく
					'// バック走行ではなくかつ
					'// 車速が範囲内かつ
					'// 加速度が範囲内かつ
					'// ヨーレートが範囲内
			VspConst = LearnVsp(VspConst, VrealFilt, GpsVreal, _
					fGpsConfirm And _
					Not fReverseRun And _
					Vreal >= VSPLEARN_LV And _
					Vreal <= VSPLEARN_HV And _
					Abs(ArealFilt) <= VSPLEARN_A And _
					Abs(GpsYawRate) <= CURVEYAW)

			gcount = 0

			GpsCourseCalc = Course(GpsLatZ, GpsLonZ, GpsLat, GpsLon)
			'// 今回値をメモリ
			GpsLatZ = GpsLat
			GpsLonZ = GpsLon
		end if



'bug2=GpsCourse
'bug=crserr
'bug1=Calib(3)
'bug1=Calib(2)
'bug2=fCurve * -15

		gcount = gcount + 1
		if (gcount = 5) Then
		end if

		Cells(y, Range("AE1").Column) = InsLon
		Cells(y, Range("AF1").Column) = InsLat
		Cells(y, Range("AG1").Column) = YawRate
		Cells(y, Range("AH1").Column) = YawOffset
		Cells(y, Range("AI1").Column) = InsCourse
		Cells(y, Range("AJ1").Column) = VspConst
		Cells(y, Range("AK1").Column) = -(fReverseRun * 16 + _
					fcurve * 8 + fGpsDoubt * 4 + _
					fGpsFix * 2 + fStop) + StatDoubt
		Cells(y, Range("AT1").Column) = bug
		Cells(y, Range("AU1").Column) = bug1
		Cells(y, Range("AV1").Column) = bug2
		Cells(y, Range("AW1").Column) = bug3
		Cells(y, Range("AX1").Column) = bug4
		Cells(y, Range("AY1").Column) = bug5

		y = y + 1
	Loop
	Cells(2, Range("F1").Column) = VspConst
'	Cells(2, Range("N1").Column) = YawOffset

	'最終停止方位
	Cells(18, Range("A1").Column) = Cells(y-1, Range("Q1").Column)
	Cells(20, Range("A1").Column) = InsCourse

	'軸スケールをグラフ2から1にコピー
'	CopyAxis2to1
	'軸スケールをグラフ1から2にコピー
'   CopyAxis1to2

	Application.ScreenUpdating = TRUE	'画面の更新を再開
	Application.Calculation = xlCalculationAutomatic '自動計算再開
end Sub
'/*---------------------------------------------------------------------------
'   GPSデータ受信処理
'---------------------------------------------------------------------------*/
static Function RxGPS()
	static glonz As Long, glatz As Long, gvz As Single
	static cnt As Long

	'GPS同期処理
	'// GPS変化したか時間が経過した？
	if (glonz <> GpsLon Or glatz <> GpsLat Or gvz <> GpsVreal Or _
				cnt = 0) Then
		cnt = 11
		Call AddOnTime(fGpsFix, GpsFixCount)
		fGpsRecv = TRUE '// GPS受信完了
	end if

	glonz = GpsLon
	glatz = GpsLat
	gvz = GpsVreal
	Call DecNonZero(cnt)
end Function
'/*---------------------------------------------------------------------------
'   GPSデータが疑わしい判断直後に過去の補正量を戻す
'---------------------------------------------------------------------------*/
static Function UndoCollect0(lat As long, lon As long, crs As Single, _
				latcal As Single, loncal As Single, crscal As Single, _
				fdoubt As Boolean)

	Const CALMAX As Integer = 4
	static cal(3, CALMAX) As Single
	static ptop As Integer
	static fdoubtz As Boolean
	Dim sum(3) As Single
	Dim i As Integer

'bug2=latcal
'bug3=loncal
'bug4=crscal
'bug2=0
	if (AfterPowerOnTime <= 1) Then '// 起動時は
		'// バッファクリア
		cal(0, i) = 0
		cal(1, i) = 0
		cal(2, i) = 0
	end if

	'// GPSが疑わしくない時に
	if (Not fdoubt) then
		'// 補正量をリングバッファにメモリしておく
		cal(0, ptop) = latcal
		cal(1, ptop) = loncal
		cal(2, ptop) = crscal
		ptop = (ptop + 1) And (CALMAX - 1)
	end if
	'// 疑わしい判断初回
	if (fdoubt And Not fdoubtz) Then
		'// 過去しばらくの期間の補正量の合計
		for i = 0 To 2
			sum(i) = 0
		next i
		for i = 0 To CALMAX - 1
			sum(0) = sum(0) + cal(0, i)
			sum(1) = sum(1) + cal(1, i)
			sum(2) = sum(2) + cal(2, i)
			'// バッファクリア
			cal(0, i) = 0
			cal(1, i) = 0
			cal(2, i) = 0
		next i
'bug2=sum(1)
'bug2=sum(0)
'bug3=sum(1)
'bug4=sum(2)
		'// 過去に補正した量を戻す
		lat = lat - sum(0)
		lon = lon - sum(1)
		crs = Normal360(crs - sum(2))
	end if
	fdoubtz = fdoubt
end Function
static Function UndoCollect(crs As Single, crscal As Single, _
				fdoubt As Boolean)

	Const CALMAX As Integer = 4
	static cal(CALMAX) As Single
	static ptop As Integer
	static fdoubtz As Boolean
	Dim sum As Single
	Dim i As Integer

	if (AfterPowerOnTime <= 1) Then '// 起動時は
		'// バッファクリア
		cal(i) = 0
	end if

	'// GPSが疑わしくない時に
	if (Not fdoubt) then
		'// 補正量をリングバッファにメモリしておく
		cal(ptop) = crscal
		ptop = (ptop + 1) And (CALMAX - 1)
	end if
	'// 疑わしい判断初回
	if (fdoubt And Not fdoubtz) Then
		'// 過去しばらくの期間の補正量の合計
		sum = 0
		for i = 0 To CALMAX - 1
			sum = sum + cal(i)
			'// バッファクリア
			cal(i) = 0
		next i
		'// 過去に補正した量を戻す
		crs = Normal360(crs - sum)
	end if
	fdoubtz = fdoubt
end Function
'/*---------------------------------------------------------------------------
'   GPSデータが疑わしい判断(SatLevel)
'---------------------------------------------------------------------------*/
static Function JudgeGpsDoubtLevel(v As Single, vgps As Single, _
				fgpsfix As Boolean, _
				satlevel As Single, satlevelfilt As Single) As Boolean

	Const DOUBTON As Single = 0.8	'// GPSが疑わしいと判断するレベル
	Const DOUBTOFF As Single = 0.85	'// GPSが疑わしくないと判断するレベル
	Const DOUBTOFFTIME As Long = 70	'// GPSが疑わしくないと判断する時間[s]x10
	Const DOUBTV As Single = 5.0	'// 疑わしい状態回復の車速偏差[km/h]
	Const DOUBTTIMEC As Long = 200	'// 車速が正しい時の回復時間[s]x10

	static fdoubt As Boolean
	static recovertime As Long, judgetime As Long, nodoubttime As Long
	Dim fleveldown As Boolean

	'// 衛星受信レベルの平均値が通常よりも低下
	fleveldown = fgpsfix And satlevel < satlevelfilt * DOUBTON
	if (fgpsfix) Then	'// GPS確定中なら
		'// 衛星受信レベルの平均値が通常よりも低下した初回？
		if (Not fdoubt And fleveldown) Then
			fdoubt = TRUE	'// GPSが疑わしいと判断
		'// 衛星受信レベルの平均値が回復して時間が経過した？
		elseif (recovertime >= judgetime) Then
			fdoubt = FALSE	'// GPSが疑わしくないと判断
			judgetime = 0
		end if
	else
		fdoubt = FALSE	'// GPSがFIXしていないならフラグを下しておく
	end if
	'// GPS精度悪化または未受信が継続した時間
	if ((fleveldown Or Not fgpsfix) And judgetime < DOUBTOFFTIME) then
		Call AddOnTime(TRUE, judgetime)	'// 判定解除時間を設定しておく
	end if

	'// 衛星受信レベルの平均値が回復してからの経過時間
	Call AddOnTime(fdoubt And satlevel > satlevelfilt * DOUBTOFF, _
					recovertime)

	'// GPSが疑わしい状態から回復できないときのF/S
	'// GPSが疑わしい状態で車速があってほぼ正しい時間
	Call AddOnTime(fdoubt And v >= YAWV And abs(v - vgps) <= DOUBTV, _
					nodoubttime)
	'// GPS車速がほぼ正しい状態が長引いた？
	if (fdoubt And nodoubttime >= DOUBTTIMEC) Then
		fdoubt = FALSE			'// GPSが疑わしくないとする
		satlevelfilt = satlevel	'// 次回判定用の値として現在値を使用する
		judgetime = 0
	end if
	JudgeGpsDoubtLevel = fdoubt
end Function
'/*---------------------------------------------------------------------------
'   GPSデータが疑わしい判断(Vreal)
'---------------------------------------------------------------------------*/
static Function JudgeGpsDoubtVreal0(v As Single, vgps As Single, _
				gpscount As Long) As Boolean

	Const VDOUBTX As Single = 2#	'// 疑わしい状態の車速の閾値倍数
	Const DOUBTV As Single = 5# 	'// 疑わしい状態の車速偏差ミニマム
	Const VDOUBTTIMEC As Long = 3	'// 車速回復待ち時間[s]
	Const VFILTC As Single = 0.1	'// 車速偏差フィルタ定数
	Dim level As Single, err As Single
	static errfilt As Single
	static fdoubt As Boolean
	static oktime As Long

	if (AfterPowerOnTime <= 1) Then '// 起動時は
		errfilt = 5
		fdoubt = FALSE
		oktime = 0
	end if

	if (gpscount >= 2) Then	'// GPSが確定しているなら
'		if (v >= YAWV) Then '// 車速が得られる程度に走行しているなら
		if (1) Then '   // 車速が得られる程度に走行しているなら
			level = errfilt * VDOUBTX		'// 判定車速偏差閾値
			if (level < DOUBTV) Then level = DOUBTV '// 低くし過ぎない
			err = Abs(v - vgps)
'bug2=level
'bug3=err
			if (err >= level) Then		'// 車速が悪化した？
				fdoubt = TRUE			'// 車速による疑わしい判定開始
				oktime = 0
			end if
			Call AddOnTime(err < level, oktime) '// 車速が回復した時間
			if (oktime >= VDOUBTTIMEC) Then 	'// 回復が規定回数に達した？
				fdoubt = FALSE			'// 疑わしい判定取り下げ
			end if

			if (Not fdoubt) Then
				'// 精度のいいときの車速偏差をフィルタ
				Call Filter(errfilt, err, VFILTC)
			end if
		end if
	else
		fdoubt = FALSE
		oktime = 0
	end if
'bug2=fdoubt* -8
	JudgeGpsDoubtVreal0 = fdoubt
end Function
static Function JudgeGpsDoubtVreal1(v As Single, vgps As Single, _
				gpscount As Long) As Boolean

	Const VDOUBTX As Single = 0.2	'// 疑わしい状態の車速の閾値倍数
	Const DOUBTV As Single = 5# 	'// 疑わしい状態の車速偏差ミニマム
	Const VDOUBTTIMEC As Long = 3	'// 車速回復待ち時間[s]
	Dim level As Single, err As Single
	static fdoubt As Boolean
	static oktime As Long

	if (AfterPowerOnTime <= 1) Then '// 起動時は
		fdoubt = FALSE
		oktime = 0
	end if

	if (gpscount >= 2) Then	'// GPSが確定しているなら
'		if (v >= YAWV) Then '   // 車速が得られる程度に走行しているなら
		if (1) Then '   // 車速が得られる程度に走行しているなら
			level = v * VDOUBTX		'// 判定車速偏差閾値
			if (level < DOUBTV) Then level = DOUBTV '// 低くし過ぎない
			err = Abs(v - vgps)
'bug=v
'bug1=vgps
'bug2=err
'bug3=level
			if (err >= level) Then		'// 車速が悪化した？
				fdoubt = TRUE			'// 車速による疑わしい判定開始
				oktime = 0
			end if
			Call AddOnTime(err < level, oktime) '// 車速が回復した時間
			if (oktime >= VDOUBTTIMEC) Then 	'// 回復が規定回数に達した？
				fdoubt = FALSE			'// 疑わしい判定取り下げ
			end if
		end if
	else
		fdoubt = FALSE
		oktime = 0
	end if
'bug2=fdoubt* -8
	JudgeGpsDoubtVreal1 = fdoubt
end Function
static Function JudgeGpsDoubtVreal(v As Single, vgps As Single, _
				gpscount As Long) As Boolean

	Const DOUBTV As Single = 5# 	'// 疑わしい状態の車速偏差
	Const VDOUBTTIMEC As Long = 3	'// 車速回復待ち時間[s]
	Dim err As Single
	static fdoubt As Boolean
	static oktime As Long

	if (AfterPowerOnTime <= 1) Then '// 起動時は
		fdoubt = FALSE
		oktime = 0
	end if

	if (gpscount >= 2) Then	'// GPSが確定しているなら
			err = Abs(v - vgps)
'bug=v
'bug1=vgps
'bug2=err
'bug3=DOUBTV
			if (err >= DOUBTV) Then		'// 車速が悪化した？
				fdoubt = TRUE			'// 車速による疑わしい判定開始
				oktime = 0
			end if
			Call AddOnTime(err < DOUBTV, oktime) '// 車速が回復した時間
			if (oktime >= VDOUBTTIMEC) Then 	'// 回復が規定回数に達した？
				fdoubt = FALSE			'// 疑わしい判定取り下げ
			end if
	else
		fdoubt = FALSE
		oktime = 0
	end if
'bug2=fdoubt* -8
	JudgeGpsDoubtVreal = fdoubt
end Function
'/*---------------------------------------------------------------------------
'   カーブ判定
'---------------------------------------------------------------------------*/
static Function JudgeCurve(yaw As Single, gpsyaw As Single, _
									gpscount As Long) As Boolean
	Const CRESTIMEC As Long = 300	'// カーブ期間リセット時間[s]x10
	Const CURVETIMEC As Long = 20	'// ヨーレート安定までの判定時間[s]x10
	Const CRETRYTIMEC As Long = 100 '// 疑わしい期間リセット時間[s]x10

	static fcurve As Boolean
	static curvedelay As Long, gytime As Long, ctime As Long
	static retrytime As Long

	if (AfterPowerOnTime <= 1) Then '// 起動時は
		curvedelay = 0
		gytime = 0
		ctime = 0
		retrytime = 0
		fcurve = FALSE
	end if

	'// GPSヨーレートが出ていない時間
	Call AddOnTime(Abs(gpsyaw) <= CURVEYAW And gpscount >= 2, gytime)
	Call DecNonZero(curvedelay) 	'// カーブ走行タイマデクリメント
	Call DecNonZero(retrytime)		'// リトライタイマデクリメント
	if (Abs(yaw) >= CURVEYAW) Then	'// ヨーレートが出ている期間は
		curvedelay = CURVETIMEC 	'// タイマ再セット
	end if
	fcurve = curvedelay > 0 '// カーブ走行中判断(仮)
	Call AddOnTime(fcurve, ctime)	'// カーブと判定されている時間
'bug=gytime
'bug=gpscount
'bug=Abs(gpsyaw)
'bug1=ctime
'bug2=curvedelay

	'// カーブと判定されている時間が長いが正しそうな時間も長い
	'// (いつまでも終わらなくなることを防止するためのF/S処理)
	if (ctime >= CRESTIMEC And gytime >= CRESTIMEC) Then
		retrytime = CRETRYTIMEC '// いったん疑わしい期間を取りやめ
	end if
	if (retrytime > 0) Then '// ディレイタイマが0でない期間は
		fcurve = FALSE		'// カーブ判断を取り下げる
	end if
	JudgeCurve = fcurve
end Function
'/*---------------------------------------------------------------------------
'   PI制御でGPS方位に対するINS方位の補正量を得る
'---------------------------------------------------------------------------*/
Function GetPI(err As Single, fenable As Boolean) As Single
	Const PGAIN As Single = 0.05
	Const IGAIN As Single = 0.05
	static ii As Single

	if (AfterPowerOnTime <= 1 Or Not fenable) Then '// 通電直後は初期化
		ii = 0
	end if
	ii = ii + err * IGAIN		'// I項
	GetPI = err * PGAIN + ii	'// P項をプラス
end Function
'/*---------------------------------------------------------------------------
'   慣性航法による緯度＆経度座標を計算
'---------------------------------------------------------------------------*/
Function CalcInsCoordinate(lat As Long, lon As Long, _
				cs As Single, v As Single, dt As Single)
	Dim a As Single
	Dim L As Single
	Dim psi As Single
	Dim len_ As Single
	Dim thta As Single
	Dim dy As Single
	Dim dx As Single
	Dim angllat As Single
	Dim angllon As Single
	Dim x As Single
	static vz As Single, csz As Single

	if (AfterPowerOnTime <= 1) Then '// 起動時は
		vz = v
		csz = cs
	end if
	

	'加速度[m/s^2]
	if (vz > 0 And v > 0) Then
		a = (v - vz) / 3.6 * dt
	else
		a = 0
	end if
	'進んだ距離 = 弧の長さL = v・t + 1/2・a・t^2[m]
	L = (v + vz) / 2 / 3.6 * dt + a * dt * dt / 2.0
	'前回からの角度変化ψ[rad]
	psi = Radians(Normal180(Normal180(cs) - Normal180(csz)))
	if (psi <> 0) Then
		'弦の長さl = 2・sin(ψ/2)・L / ψ [m]
		len_ = 2 * Sin(psi / 2.0) * L / psi
	else
		len_ = L
	end if
	'前回のワールド方位からψ/2ずれた方向に進んだことにする
	thta = Radians(csz) + psi / 2.0
	'北に進んだ距離
	dy = len_ * Cos(thta)
	'緯度に変換
	angllat = Degrees((Atn(dy / 6378150#)))
	'東に進んだ距離
	dx = len_ * Sin(thta)
	'緯度による補正量
	x = Abs(Cos(Radians(lat / F1e7)))
	x = WorksheetFunction.Max(x, 0.1)
	'経度に変換
	angllon = Degrees(Atn(dx / (6378150# * x)))
	'新たな座標
	lat = lat + angllat * F1e7
	lon = lon + angllon * F1e7
	'// 現在値をメモリ
	vz = v
	csz = cs

	CalcInsCoordinate = Array(lat, lon)
end Function

'/*---------------------------------------------------------------------------
'   2つの座標間の距離を返す
'---------------------------------------------------------------------------*/
Function Distance(lat1 As Long, lon1 As Long, _
					lat2 As Long, lon2 As Long)
	Dim x As Single, y As Single

	'// 横方向の移動距離
	x = Sin(Radians((lon2 - lon1) / F1e7)) * 6372795 * _
				Abs(Cos(Radians(lat2 / F1e7)))
	'// 縦方向の移動距離
	y = Sin(Radians((lat2 - lat1) / F1e7)) * 6372795
	Distance = Sqr(x * x + y * y)
end Function
'/*---------------------------------------------------------------------------
'   2つの座標間の方位を返す
'---------------------------------------------------------------------------*/
Function Course(lat1 As Long, lon1 As Long, _
					lat2 As Long, lon2 As Long)
	Dim x As Single, y As Single, z As Single
	static ans As Single

	'// 横方向の移動距離/地球半径(右が正)
	x = Sin(Radians((lon2 - lon1) / F1e7)) * Abs(Cos(Radians(lat2 / F1e7)))
	'// 縦方向の移動距離/地球半径(上が正)
	y = Sin(Radians((lat2 - lat1) / F1e7))

	if (x <> 0) Then
		z = Abs(Degrees(Atn(y / x)))
		if (x >= 0 And y >= 0) Then
			ans = 90 - z
		elseif (x >= 0 And y < 0) Then
			ans = 90 + z
		elseif (x < 0 And y < 0) Then
			ans = 270 - z
		else
			ans = 270 + z
		end if
	end if
	Course = ans
end Function
'/*---------------------------------------------------------------------------
'   フィルター
'---------------------------------------------------------------------------*/
Function Filter(filt As Single, dat As Single, fact As Single)
	if (AfterPowerOnTime <= 1) Then 	'// 通電直後は初期化
		filt = dat
	else
		filt = (1# - fact) * filt + fact * dat
	end if
end Function
'/*---------------------------------------------------------------------------
'   0～360に正規化
'---------------------------------------------------------------------------*/
Function Normal360(dat As Single)
	Dim x As Single
	x = dat
	do while (x >= 360)
		x = x - 360
	Loop
	do while (x < 0)
		x = x + 360
	Loop
	Normal360 = x
end Function
'/*---------------------------------------------------------------------------
'   ±180に正規化
'---------------------------------------------------------------------------*/
Function Normal180(dat As Single)
	Dim x As Single
	x = dat
	do while (x >= 180)
		x = x - 360
	Loop
	do while (x < -180)
		x = x + 360
	Loop
	Normal180 = x
end Function
'/*---------------------------------------------------------------------------
'   180deg反転
'---------------------------------------------------------------------------*/
Function Add180(deg As Single)
	Dim x As Single
	x = deg
	x = x + 180#			'// 180deg反転
	if (x > 360#) Then
		x = x - 360#
	end if
	Add180 = x
end Function
'/*---------------------------------------------------------------------------
'   符号を返す
'---------------------------------------------------------------------------*/
Function Sign(x As Single)
	if (x > 0) Then
		Sign = 1
	elseif (x < 0) Then
		Sign = -1
	else
		Sign = 0
	end if
end Function

'/*---------------------------------------------------------------------------
'   フラグがTRUEの回数を累積
'---------------------------------------------------------------------------*/
Function AddOnTime(f As Boolean, x As Long)
	if (f) Then
		x = x + 1
	else
		x = 0
	end if
	AddOnTime = x
end Function
'/*---------------------------------------------------------------------------
'   フラグがTRUEとそうでないの回数を累積
'---------------------------------------------------------------------------*/
Function AddOnOffTime(f As Boolean, x As Long, y As Long)
	if (f) Then
		x = x + 1
		y = 0
	else
		x = 0
		y = y + 1
	end if
	AddOnOffTime = x
end Function
'/*---------------------------------------------------------------------------
'   値が0でないなら減算
'---------------------------------------------------------------------------*/
Function DecNonZero(x As Long)
	if (x > 0) Then
		x = x - 1
	end if
	DecNonZero = x
end Function
'/*---------------------------------------------------------------------------
'   π
'---------------------------------------------------------------------------*/
Function Pai()
	Pai = Atn(1) * 4
end Function
'/*---------------------------------------------------------------------------
'   deg→rad変換
'---------------------------------------------------------------------------*/
Function Radians(x As Single)
	Radians = x * Atn(1) / 45
end Function
'/*---------------------------------------------------------------------------
'   rad→deg変換
'---------------------------------------------------------------------------*/
Function Degrees(x As Single)
	Degrees = x * 45 / Atn(1)
end Function
'/*---------------------------------------------------------------------------
'   bit
'---------------------------------------------------------------------------*/
Function Bit(x As Integer, pat As Integer, y1 As Single, y0 As Single)
	if (x And pat) Then
		Bit = y1
	else
		Bit = y0
	end if
end Function
'/*---------------------------------------------------------------------------
'   軸スケールをグラフ1から2にコピー
'---------------------------------------------------------------------------*/
Public Sub CopyAxis1to2()
	Dim x0 As Double, x1 As Double
	Dim y0 As Double, y1 As Double
	Dim chtObj1 As ChartObject, chtObj2 As ChartObject
	Dim s As String

	for Each chtObj2 In ActiveSheet.ChartObjects
		if (chtObj2.Chart.HasTitle) Then
			s = chtObj2.Chart.ChartTitle.Text
			if (s = "GPS(ログ)") Then
				for Each chtObj1 In ActiveSheet.ChartObjects
					if (chtObj1.Chart.HasTitle) Then
						s = chtObj1.Chart.ChartTitle.Text
						if (s = "INS(ログ)") Then
							chtObj2.Activate		  'グラフ2選択
							ActiveChart.Axes(xlValue).Select
							With ActiveChart.Axes(xlValue)
								y0 = .MinimumScale	'縦軸の目盛取得
								y1 = .MaximumScale
							end With
							ActiveChart.Axes(xlCategory).Select
							With ActiveChart.Axes(xlCategory)
								x0 = .MinimumScale	'横軸の目盛取得
								x1 = .MaximumScale
							end With
							chtObj1.Activate	  'グラフ1選択
							ActiveChart.Axes(xlValue).Select
							With ActiveChart.Axes(xlValue)
								.MinimumScale = y0	'縦軸の目盛設定
								.MaximumScale = y1
							end With
							ActiveChart.Axes(xlCategory).Select
							With ActiveChart.Axes(xlCategory)
								.MinimumScale = x0	'横軸の目盛設定
								.MaximumScale = x1
							end With
						end if
						if (s = "INS(Sim)") Then
							chtObj2.Activate		  'グラフ2選択
							ActiveChart.Axes(xlValue).Select
							With ActiveChart.Axes(xlValue)
								y0 = .MinimumScale	'縦軸の目盛取得
								y1 = .MaximumScale
							end With
							ActiveChart.Axes(xlCategory).Select
							With ActiveChart.Axes(xlCategory)
								x0 = .MinimumScale	'横軸の目盛取得
								x1 = .MaximumScale
							end With
							chtObj1.Activate	  'グラフ1選択
							ActiveChart.Axes(xlValue).Select
							With ActiveChart.Axes(xlValue)
								.MinimumScale = y0	'縦軸の目盛設定
								.MaximumScale = y1
							end With
							ActiveChart.Axes(xlCategory).Select
							With ActiveChart.Axes(xlCategory)
								.MinimumScale = x0	'横軸の目盛設定
								.MaximumScale = x1
							end With
						end if
					end if
				Next
			end if
		end if
	Next
	ActiveSheet.Cells(2, 2).Activate	  'カーソルを初期位置に
end Sub
'/*---------------------------------------------------------------------------
'   軸スケールをグラフ2から1にコピー
'---------------------------------------------------------------------------*/
Private Sub CopyAxis2to1()
	Dim x0 As Double, x1 As Double
	Dim y0 As Double, y1 As Double
	Dim chtObj1 As ChartObject, chtObj2 As ChartObject
	Dim s As String

	for Each chtObj2 In ActiveSheet.ChartObjects
		if (chtObj2.Chart.HasTitle) Then
			s = chtObj2.Chart.ChartTitle.Text
			if (s = "I緯度S") Then
				for Each chtObj1 In ActiveSheet.ChartObjects
					if chtObj1.Chart.HasTitle Then
						s = chtObj1.Chart.ChartTitle.Text
						if (s = "G緯度") Then
							chtObj2.Activate		  'グラフ2選択
							ActiveChart.Axes(xlValue).Select
							With ActiveChart.Axes(xlValue)
								y0 = .MinimumScale	'縦軸の目盛取得
								y1 = .MaximumScale
							end With
							ActiveChart.Axes(xlCategory).Select
							With ActiveChart.Axes(xlCategory)
								x0 = .MinimumScale	'横軸の目盛取得
								x1 = .MaximumScale
							end With
							chtObj1.Activate	  'グラフ1選択
							ActiveChart.Axes(xlValue).Select
							With ActiveChart.Axes(xlValue)
								.MinimumScale = y0	'縦軸の目盛設定
								.MaximumScale = y1
							end With
							ActiveChart.Axes(xlCategory).Select
							With ActiveChart.Axes(xlCategory)
								.MinimumScale = x0	'横軸の目盛設定
								.MaximumScale = x1
							end With
						end if
					end if
				Next
			end if
		end if
	Next
	ActiveSheet.Cells(2, 2).Activate	  'カーソルを初期位置に
end Sub


'/*---------------------------------------------------------------------------
'   VSP定数学習処理
'---------------------------------------------------------------------------*/
Function LearnVsp(VspConst As Long, v As Single, vgps As Single, _
					flearn As Boolean)
	static dvfilt As Single
	Const V1 As Single = 3.0
	Const VCMAX As Single = 100.0
	Dim vc As Single

	if (AfterPowerOnTime <= 1) Then '// 起動時は
		dvfilt = 0#
	end if
'bug3=flearn*-15
'bug=vgps
'bug1=v
'if (GpsVreal > 0 and flearn) then
'	bug2=(Vreal - GpsVreal) / GpsVreal * 1000
'	bug2=(Vreal - GpsVreal)*10
'else
'	bug2=0
'end if
'bug=v
'bug1=vgps
'bug2=dvfilt
	if (flearn) Then
		Call Filter(dvfilt, v - vgps, 0.1) '// 差分をフィルタリング
		vc = Abs(dvfilt) * VCMAX / V1
		if (vc > VCMAX) Then
			vc = VCMAX
		end if
		if (dvfilt > 0#) Then	'// メータ車速が大きいなら
			VspConst = VspConst - vc '// 定数を減らす
		elseif (dvfilt < 0) Then	'// さもなければ
			VspConst = VspConst + vc '// 定数を増やす
		end if
	end if
'bug2=dvfilt*10
'bug4=VspConst/100000
	LearnVsp = VspConst
end Function

'/*---------------------------------------------------------------------------
'   車速算出処理
'---------------------------------------------------------------------------*/
Function CalcVsp(afilt As Single, dt As Single, slp As Single, _
				accely As Single, yaw As Single, fcrv As Boolean, _
				fReverse As Boolean, fpark As Boolean)

	Const VSPMINV As Single = 0.1		'// VSP最低車速 0.1[km/h]
	Const VSPMINITVL As Long = 20		'// VSP停止判定インターバル 2[s]x10
	Const VSPSTARTG As Single = 0.015	'// 発進判定加速度偏差[G]
	Const VSPDELAYC As Single = 30	'// G変化による発進判定オフディレイ[s]x10
	Const V0 As single = 5.0

	Dim ac As Single, dv As Single
	static v As Single, vz As Single
	static acz As Single
	static gtime As Long
	Dim ofst As Double, sp As Single

	if (AfterPowerOnTime <= 1) Then '// 起動時は
		v = 0
		vz = 0
		acz = 0
		gtime = 0
	end if
'bug2=vc
	if (VspTime > 0) Then		'// パルスがあるなら
		'// 車速計算[km/h]
		v = VspConst / VspTime
	else
		v = 0#
	end if

	Call DecNonZero(gtime)	'// ディレイタイマ減算
	'// 停止中にGの変化が閾値を超えた？
	if (v = 0# And Not fpark And Abs(accely - acz) >= VSPSTARTG) Then
		gtime = VSPDELAYC	'// 停止中のG変動タイマスタート
	end if
	'// 最後のパルスから規定未満または停止中にG変動があった？
	if (gtime > 0) Then
		if v < VSPMINV Then v = VSPMINV '// 最低車速を設定
	end if
	acz = accely	'// 今回のGセンサー前後加速度をメモリ

	if (fReverse) Then	'// リバースなら
		v = v * -1# 	'// 負
	end if
	if (v < 5# And fpark) Then	'// 車速が低くてパーキングブレーキオンなら
		v = 0#	'// 停止
	end if

	v = CollectVspTire(v, afilt)	'// タイヤの動半径補正

	dv = v - vz '// 車速変化分
	if (v >= 2# And vz >= 2# And dt > 0#) Then
		ac = dv / 3.6 / dt / G1 '// 車速微分前後加速度[G]
	else
		ac = accely
	end if
	'// 車速データと位相を合わせたメーター加速度
	Call Filter(afilt, ac, GPSFILT)
	vz = v

	'// 勾配による車速補正
	sp = Abs(slp)
	if (sp > 1#) Then sp = 1#
	v = v * Cos(WorksheetFunction.Asin(sp))
'bug=v
	CalcVsp = v
end Function
'/*---------------------------------------------------------------------------
'   車速の非線形補正
'---------------------------------------------------------------------------*/
Function CollectVsp0(v As Single)
	Const MAXCALV As Single = 20.0	'// 補正する最大車速[km/h]
	Const CALV As Single = 2		'// 補正する車速[km/h]
	Dim v0 As Single

	v0 = v
	if (v0 > CALV And v0 <= MAXCALV) then
		v0 = v0 - CALV
	end if
	if (v0 < -CALV) then
		v0 = v0 + CALV
	end if

	CollectVsp0 = v0
end Function
Function CollectVsp1(v As Single, ac As Single)
	Const MAXCALV As Single = 120.0	'// 補正する最大車速[km/h]
	Const CALV As Single = 3		'// 補正する車速[km/h]
	Const G2V As Single = 20		'// 加速度補正車速係数[G]→[km/h]
	Const GDEADZONE As Single = 0.1'// 加速度補正不感帯[km/h]
	Dim v0 As Single, vc As Single

	'// 車速がプラス側にずれる分
	if (Abs(v) > MAXCALV) then
		vc = 0.0
	else
		vc = CALV - Abs(v) * CALV / MAXCALV
	end if

	v0 = v
	if (v0 > vc) then
		v0 = v0 - vc
	end if
	if (v0 < -vc) then
		v0 = v0 + vc
	end if

	vc = ac * G2V
	if (Abs(vc) < GDEADZONE) then
		vc = 0
	end if
	if (vc > 3) then vc = 3
	if (vc < -3) then vc = -3
	v0 = v0 - vc
	if (v = 0) then
		v0 = 0
	elseif (v > 0) then
		if (v0 < 0) then
			v0 = 0
		end if
	else
		if (v0 > 0) then
			v0 = 0
		end if
	end if
	CollectVsp1 = v0
end Function
'/*---------------------------------------------------------------------------
'	タイヤの動半径補正
'---------------------------------------------------------------------------*/
Function CollectVspTire(v As Single, ac As Single)
	Const MAXCALV As Single = 120.0	'// 補正する最大車速[km/h]
	Const VGAIN0 As Single = 0.995	'// 車速0でのゲイン
	Const G2GAIN As Single = 0.0	'// 加速度補正係数[G]→[倍]
	Const GDEADZONE As Single = 0.05'// 加速度補正不感帯[G]
	Dim v0 As Single, vgain As Single, ggain As Single

	v0 = v
	'// タイヤの動半径補正
	if (Abs(v) > MAXCALV) then
		vgain = 1.0
	else
		vgain = VGAIN0 + Abs(v) * (1.0 - VGAIN0) / MAXCALV
	end if

'bug3=vgain
	v0 = v0 * vgain

	'// 加減速によるスリップ率補正
	ggain = 1.0 - ac * G2GAIN
	if (Abs(ac) < GDEADZONE) then
		ggain = 1.0
	end if
	if (v0 > 0) then	'// 前進時のみ補正する
		v0 = v0 * ggain
	end if
'bug3=ggain

	CollectVspTire = v0
end Function
'/*---------------------------------------------------------------------------
'   ヨーレートセンサの補償とオフセット除去を行い真のヨーレートを得る[deg/s]
'---------------------------------------------------------------------------*/
static Function GetTrueYawRate(yofst As Single, yawr As Integer, _
				temp As Single, _
				gpsyaw As Single, v As Single, accel As Single, _
				fcrv As Boolean, fpk As Boolean, _
				fstop As Boolean, frun As Boolean) As Single

	Const FILT_OFCANSTOP As Single = 0.01'// 停止時オフセット除去フィルタ定数
	Const FILT_OFCAN As Single = 0.005	'// オフセット除去フィルタ定数
	Const ACCEL2ANGLE As Single = 35#	'// 加速度角度変換係数

	Dim x As Single, rpgain As Single
	static yaw As Single, yawdelay As Single
	Dim rol As Single, pich As Single
	Dim tofst As Single

	if (AfterPowerOnTime <= 1) Then '// 起動時は
		yawdelay = yaw
	end if

	'// 温度によって生じるオフセット量[deg/s]
	tofst = (temp - CALTEMP) * SLOPE_YAW
'bug1=tofst
	'// 温度補償したヨーレート[deg/s]
	yaw = yawr * 250# / 32768#
	yaw = yaw - tofst

	'// スケールゲイン補正
	yaw = yaw * YAWGAIN

	'// カーブ中ならロール角とピッチ角[rad]を推定
	if (fcrv) then
		rol = v / 3.6 * Radians(yaw) / G1 * ACCEL2ANGLE
		if (rol >= 30) Then
			rol = 30
		elseif (rol <= -30) Then
			rol = -30
		end if
		rol = Radians(rol)
		pich = accel * ACCEL2ANGLE
		if (pich >= 30) Then
			pich = 30
		elseif (pich <= -30) Then
			pich = -30
		end if
		pich = Radians(pich)
	else
		rol = 0
		pich = 0
	end if
	'// ジャイロZ軸の鉛直線からの傾き[rad]
	x = Atn(Sqr(Tan(rol) ^ 2 + Tan(pich) ^ 2))
	x = Cos(x)	'// Z軸が傾くことによるセンサ出力の減少分
	if (x <> 0#) Then
		rpgain = 1 / x	'// 傾きを補正するためのゲイン
	else
		rpgain = 1#
	end if

	'// ロール＆ピッチ補正
	yaw = yaw * YAWGAIN * rpgain
'bug=yaw
	'// GPSヨーレートと位相を合わせたヨーレート
	Call Filter(yawdelay, yaw, GPSFILT)

	'/*** 停止時ヨーレートゼロ点補正 ***/
	'// 停止中のセンサ出力はそのままオフセットといえる
	if (fstop) Then
		if (fpk) Then
			'// ヨーレートオフセットアップデート[deg/s]
			Call Filter(yofst, yaw, FILT_OFCANSTOP)
		else
			'// ヨーレートオフセットアップデート[deg/s]
			Call Filter(yofst, yaw, FILT_OFCAN)
		end if
'bug=0.01
	end if

	'/*** 走行時ヨーレートゼロ点補正 ***/
	if (frun And Abs(gpsyaw) <= CURVEYAW) Then
'bug=-0.01
		'// ヨーレートオフセットアップデート[deg/s]
		Call Filter(yofst, yawdelay - gpsyaw, FILT_OFCAN)
	end if
'bug1=yofst

	'// オフセットを補正したヨーレート
	yaw = yaw - yofst

'bug=rol
'bug1=pich
'bug2=rpgain

	'// ヨーレートを返す
	GetTrueYawRate = yaw
end Function
