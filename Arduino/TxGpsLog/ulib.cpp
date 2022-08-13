/*****************************************************************************
*
*	ulib.c -- 汎用関数
*
*	rev1.1	31 Jan. 2009	initial revision by	Toshi
*	rev2.0	08 Apr.2017		common.cをコピー
*
*****************************************************************************/

#include "ulib.h"

/*----------------------------------------------------------------------------
	数値を10進文字列に変換する(符号なし)
	書式 ret = stcu_d(CHAR* buf, USHORT data);

	SHORT ret;		頭までの文字数(0～4)
	CHAR* buf;		文字列格納バッファ(6文字分)
	USHORT data;	入力値
----------------------------------------------------------------------------*/
SHORT stcu_d(CHAR* buf, USHORT data)
{
	CHAR i = 4, j;

	buf[5] = '\0';
	do {
		buf[i--] = '0' + data % 10;
		data /= 10;
	} while (data != 0);
	j = i + 1;
	while (i >= 0)
	{
		buf[i--] = ' ';
	}

	return j;
}
/*----------------------------------------------------------------------------
	数値を10進文字列に変換する(符号あり)
	書式 ret = stci_d(CHAR* buf, SHORT data);

	SHORT ret;		頭までの文字数(0～5)
	CHAR* buf;		文字列格納バッファ(7文字分)
	SHORT data;		入力値
----------------------------------------------------------------------------*/
SHORT stci_d(CHAR* buf, SHORT data)
{
	SHORT j;
	BOOL fminus = FALSE;

	buf[0] = ' ';
	if (data < 0)
	{
		fminus = TRUE;
		data = -data;
	}
	j = stcu_d(buf+1, data) + 1;
	if (fminus)
	{
		j--;
		buf[j] = '-';
	}

	return j;
}
/*----------------------------------------------------------------------------
	数値を16進文字列に変換する
	書式 ret = stcu_h(CHAR* buf, USHORT data);

	SHORT ret;		頭までの文字数(0)
	CHAR* buf;		文字列格納バッファ(5文字分)
	USHORT data;	入力値
----------------------------------------------------------------------------*/
SHORT stcu_h(CHAR* buf, USHORT data)
{
	CHAR i = 3, j;

	buf[4] = '\0';
	do {
		buf[i] = '0' + data % 16;
		if (buf[i] > '9')
		{
			buf[i] += 7;
		}
		i--;
		data /= 16;
	} while (data != 0);
	j = i + 1;
	while (i >= 0)
	{
		buf[i--] = '0';
	}

	return 0;
}

/*----------------------------------------------------------------------------
	一つ小さな型に変換する(LONG -> SHORT, SHORT -> CHAR)
	書式 ret = RangeXXX(SHORT x);

	XXX	 ret;		出力値
	SHORT x;		入力値
----------------------------------------------------------------------------*/
UCHAR RangeUCHAR(SHORT x)
{
	/* 上限下限をUCHARの範囲に制限 */
	return (UCHAR)Limiter(x, 255, 0); 
}

CHAR RangeCHAR(SHORT x)
{
	/* 上限下限をCHARの範囲に制限 */
	return (CHAR)Limiter(x, 127, -128); 
}

SHORT RangeSHORT(LONG x)
{
	x = min (32767L, x);
	x = max (-32768L, x);
	return (SHORT)x;			/* 上限下限をSHORTの範囲に制限 */
}

/*----------------------------------------------------------------------------
	微分(100msサンプリング)
	書式 ret = DuDt(SHORT x, SHORT* z1, BOOL* f2nd);

	SHORT ret;		出力値
	SHORT x;		入力値
	SHORT* z1;		過去の値を保持するためのバッファへのポインタ
	BOOL* f2nd;		初期化済みを示すフラグへのポインタ(初期値=0)
----------------------------------------------------------------------------*/
SHORT DuDt(SHORT x, SHORT* z1, BOOL* f2nd)
{
	SHORT y;

	if (*f2nd == FALSE)		/* 初めてか？       */
	{
		*f2nd = TRUE;		/* 初期化済みを記録 */
		y = 0;				/* 初回は0を返す    */
	}
	else
	{
		/* 結果をSHORTの範囲に収める */
		y = Limiter(x - *z1, 3276, -3276) * 10;
	}
	*z1 = x;			/* 現在値をレジスタに保持 */

	return y;
}

/*----------------------------------------------------------------------------
	リミッタ(最大と最小を規制する)
	書式 ret = Limiter(SHORT x, SHORT xmax, SHORT xmin);

	SHORT ret;		出力値
	SHORT x;		入力値
	SHORT xmax;		最大値
	SHORT xmin;		最小値
----------------------------------------------------------------------------*/
SHORT Limiter(SHORT x, SHORT xmax, SHORT xmin)
{
	if (x > xmax)
	{
		x = xmax;
	}
	if (x < xmin)
	{
		x = xmin;
	}
	return x;
}

/*----------------------------------------------------------------------------
	レートリミッタ(１サンプリングあたりの変化幅を規制する)
	書式 ret = RateLimiter(SHORT x, SHORT xp, SHORT xm, SHORT* z1, BOOL* f2nd);

	SHORT ret;		出力値
	SHORT x;		入力値
	SHORT xp;		１サンプルあたりの+方向への最大変化幅(正の値)
	SHORT xm;		１サンプルあたりの-方向への最大変化幅(負の値)
	SHORT* z1;		過去の値を保持するためのバッファへのポインタ
	BOOL* f2nd;		初期化済みを示すフラグへのポインタ(初期値=0)
----------------------------------------------------------------------------*/
SHORT RateLimiter(SHORT x, SHORT xp, SHORT xm, SHORT* z1, BOOL* f2nd)
{
	if (*f2nd == FALSE)	/* 初めてか？ */
	{
		*f2nd = TRUE;	/* 初期化済みを記録 */
	}
	else
	{
		if (x > (*z1 + xp))
		{
			x = *z1 + xp;
		}
		if (x < (*z1 + xm))
		{
			x = *z1 + xm;
		}
	}
	*z1 = x;			/* 現在値をレジスタに保持 */

	return x;
}

/*----------------------------------------------------------------------------
	バターワース１次ローパスフィルタ(100msサンプリング)
	書式 ret = ShortFilter(SHORT x, LONG a, LONG b, LONG* z);

	SHORT ret;		出力
	SHORT x;		入力
	LONG a;			フィルタ定数-a[1]x10000
	LONG b;			フィルタ定数b[1]x10000 ただしa+2*b=10000の関係を満たすこと
	LONG* z;		フィルタレジスタ

●a, bの求め方
カットオフ0.02Hzならばmatlabで
>> [b,a]=butter(1, 0.02/5)と打ち込むと結果が以下のように表示される。
(ちなみに1は次数,/5はナイキスト周波数)
  b =
    0.0062    0.0062
  a =
    1.0000   -0.9875
ここでa+2*b=10000の関係を満たす一番近い整数値としてaに9876をbに62を設定する。
----------------------------------------------------------------------------*/
SHORT ShortFilter(SHORT x, LONG a, LONG b, LONG* z)
{
	LONG y, absz;
	SHORT ret;

	/* *z * a の計算でlongの範囲を超えないために */
	absz = ABS(*z);

	if (absz > 107374182L)
	{
		y = (LONG)x + (*z / 10000L * a);
	}
	else if (absz > 10737418L)
	{
		y = (LONG)x + (*z / 1000L * a / 10L);
	}
	else if (absz > 1073741L)
	{
		y = (LONG)x + (*z / 100L * a / 100L);
	}
	else if (absz > 107374L)
	{
		y = (LONG)x + (*z / 10L * a / 1000L);
	}
	else
	{
		y = (LONG)x + (*z * a / 10000L);
	}
	ret = RangeSHORT(b * (y + *z) / 10000L);

	*z = y;

	return ret;
}

/*----------------------------------------------------------------------------
	フィルタレジスタのイニシャライズ
	（出力＝入力となるようにレジスタを初期化する）
	書式 void InitShortFilter(SHORT x, LONG a, LONG b, LONG* z);
----------------------------------------------------------------------------*/
void InitShortFilter(SHORT x, LONG a, LONG b, LONG* z)
{
	*z = (LONG)x * 10000L / (2L * b);
}

/*** 桁落ちが問題となるためスペシャル版を使用する  ***/
/* フィルタ 1次 0.08Hz */
const SHORT D_008Hz = 13;		/* １次、0.08Hz 0.0257847*512 */
const SHORT E_008Hz = 486;		/* 0.9508431*512 */
SHORT Lpf008(SHORT v, LONG* Z008)
{
	LONG x, xx;

	x = (LONG)v + ((LONG)E_008Hz * *Z008 + 256L) / 512L;
	xx = x + *Z008;
	*Z008 = x;
	return (SHORT)(((LONG)D_008Hz * xx + 256L) / 512L);
}

/* 初期化 */
void InitLpf008(SHORT v, LONG* Z008)
{
	*Z008 = (LONG)v * 512L / (2L * (LONG)D_008Hz);
}

const SHORT D_006Hz = 9;		/* １次、0.06Hz 0.0185*512 */
const SHORT E_006Hz = 494;		/* 0.9630*512 */
SHORT Lpf006(SHORT v, LONG* Z006)
{
	LONG x, xx;

	x = (LONG)v + ((LONG)E_006Hz * *Z006 + 256L) / 512L;
	xx = x + *Z006;
	*Z006 = x;
	return (SHORT)(((LONG)D_006Hz * xx + 256L) / 512L);
}

/* 初期化 */
void InitLpf006(SHORT v, LONG* Z006)
{
	*Z006 = (LONG)v * 512L / (2L * (LONG)D_006Hz);
}

/*----------------------------------------------------------------------------
	フラグのオン時間の累積
	書式 void AddOnTime(BOOL flag, SHORT* ontime)

	BOOL flag;		フラグ
	SHORT* ontime;	オン時間
----------------------------------------------------------------------------*/
#define	TIMEMAX 30000
#define	TIMEMAXC 255
void AddOnTime(BOOL flag, SHORT* ontime)
{
	if (flag)							/* オンしてるなら */
	{
		if (*ontime < TIMEMAX)
		{
			(*ontime)++;				/*オン時間＋＋ */
		}
	}
	else
	{
		*ontime = 0;
	}
}
/* UCHAR版 */
void AddOnTimeUCHAR(BOOL flag, UCHAR* ontime)
{
	if (flag)							/* オンしてるなら */
	{
		if (*ontime < TIMEMAXC)
		{
			(*ontime)++;				/* オン時間＋＋ */
		}
	}
	else
	{
		*ontime = 0;
	}
}

/*----------------------------------------------------------------------------
	フラグのオン／オフ時間の累積 
	書式 void AddOnOffTime(BOOL flag, SHORT* ontime, SHORT* offtime)

	BOOL flag;		フラグ
	SHORT* ontime;	オン時間
	SHORT* offtime;	オフ時間
----------------------------------------------------------------------------*/
void AddOnOffTime(BOOL flag, SHORT* ontime, SHORT* offtime)
{
	if (flag)							/* オンしてるなら */
	{
		*offtime = 0;					/* オフ時間を０に */
		if (*ontime < TIMEMAX)			/* 規定に達するまでタイマ＋＋ */
		{
			(*ontime)++;
		}
	}
	else								/* オフなら */
	{
		*ontime = 0;					/* オン時間を０に */

		if (*offtime < TIMEMAX)			/* 規定に達するまでタイマ＋＋ */
		{
			(*offtime)++;
		}
	}
}
/* UCHAR版 */
void AddOnOffTimeUCHAR(BOOL flag, UCHAR* ontime, UCHAR* offtime)
{
	if (flag)							/* オンしてるなら */
	{
		*offtime = 0;					/* オフ時間を０に */
		if (*ontime < TIMEMAXC)			/* 規定に達するまでタイマ＋＋ */
		{
			(*ontime)++;
		}
	}
	else								/* オフなら */
	{
		*ontime = 0;					/* オン時間を０に */

		if (*offtime < TIMEMAXC)		/* 規定に達するまでタイマ＋＋ */
		{
			(*offtime)++;
		}
	}
}
/*----------------------------------------------------------------------------
	ゼロでなければデクリメント
	書式 SHORT DecNonZero(SHORT* data)

	SHORT ret;		出力
	SHORT* data;	入力
----------------------------------------------------------------------------*/
SHORT DecNonZero(SHORT* data)
{
	if (*data != 0)
		(*data)--;
	return *data;
}
/* UCHAR版 */
UCHAR DecNonZeroUCHAR(UCHAR* data)
{
	if (*data != 0)
		(*data)--;
	return *data;
}

/*----------------------------------------------------------------------------
	平均を得る
	書式 ret = Average(SHORT data, SHORT* n, LONG* sum);

	SHORT ret;		出力データ
	SHORT data;		入力データ
	SHORT* n;		データ個数レジスタ
	LONG* sum;		データ合計レジスタ
----------------------------------------------------------------------------*/
SHORT Average(SHORT data, SHORT* n, LONG* sum)
{
	SHORT ret;

	(*sum) += (LONG)data;	/* 累積 */
	(*n)++;					/* データ個数インクリメント */
	ret = RangeSHORT(*sum / (LONG)*n);	/* 平均値算出 */
	if (*n >= 10000)		/* 累積が連続した？ */
	{
		*sum = (LONG)ret;	/* 今までの平均から */
		*n = 1;				/* 再スタート */
	}
	return ret;
}
void InitAverage(SHORT* n, LONG* sum)
{
	*n = 0;		/* 個数レジスタクリア */
	*sum = 0L;	/* 合計レジスタクリア */
}

/*----------------------------------------------------------------------------
	双一次変換を用いた一次遅れ離散フィルタ
	書式 ret = BiFilt(SHORT time, SHORT in,  SHORT* inz, SHORT* outz)
	
	SHORT ret;		出力
	SHORT time;		時定数[ms]
	SHORT in;		入力
	SHORT* inz;		過去の値を保持するためのバッファ１(入力過去値)
	SHORT* outz;	過去の値を保持するためのバッファ２(出力過去値)
----------------------------------------------------------------------------*/
SHORT BiFilt(SHORT time, SHORT in, SHORT *inz, SHORT *outz)
{
	LONG tau;
	SHORT ret;

	/* tau = 2 x サンプリング周波数(10Hz) x 時定数[ms] / 10 */
	tau = 2L * (LONG)time;

	ret = RangeSHORT(((LONG)in*100L + (LONG)*inz*100L
						+ (tau - 100L)*(LONG)*outz)
									/ (tau + 100L));
	*inz = in;
	*outz = ret;
	
	return ret;
}

/*----------------------------------------------------------------------------
	フィルタレジスタのイニシャライズ
	（出力＝入力となるようにレジスタを初期化する）
	書式 void InitBiFilt(SHORT in, SHORT *inz, SHORT *outz)
----------------------------------------------------------------------------*/
void InitBiFilt(SHORT in, SHORT *inz, SHORT *outz)
{
	*inz = in;
	*outz = in;
}

/*----------------------------------------------------------------------------
	テーブル検索(直線補間)
	書式 ret = TableData(SHORT x, SHORT n, SHORT* p);

	SHORT ret;		結果
	SHORT x;		入力
	SHORT n;		配列要素の数
	SHORT* p;		x,yテーブル(xは昇順にソート済みであること)
----------------------------------------------------------------------------*/
SHORT TableData(SHORT x, SHORT n, SHORT* p)
{
	SHORT i, x0, x1, y0, y1, y;
	LONG xb;

	n--;	/* インデックスMAX値 */
	if (x <= *p)			/* 最初のx値以下なら */
		return *(p + 1);	/* 最初のy値を返す */
	if (x >= *(p + 2 * n))	/* 最後のx値以上なら */
		return *(p + 2 * n + 1);	/* 最後のy値を返す */
	
	for (i = 1; i < n; i++)
	{
		if (*(p + i * 2) > x)	/* 区間を探す */
			break;
	}
	x0 = *(p + i * 2 - 2);
	y0 = *(p + i * 2 - 1);
	x1 = *(p + i * 2);
	y1 = *(p + i * 2 + 1);
	xb = (LONG)(x1 - x0);
	xb = max(1, xb);
	y = y0 + RangeSHORT(((LONG)(y1 - y0) * (LONG)(x - x0) + xb / 2) / xb);

	return y;
}

/*** end of common.c ***/
