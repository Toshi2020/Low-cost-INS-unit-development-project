/*****************************************************************************
*
*	TxGpsLog.ino -- GPSログファイル送信
*
*	rev1.0	2022/08/08	initial revision by	Toshi
*
*****************************************************************************/
#include <string.h>
#include <SPI.h>
#include <SD.h>
#include "ulib.h"	// ユーザーライブラリー

#define SD_CS 10

#define START_SW 9	// スタートSW
#define LOGTYPE A3	// 0でLogフォーマット

#define MAXBUF 128
CHAR buf[MAXBUF];

#define L1e7 10000000L
#define F1e7 10000000.0

BOOL fCourseCalc;
CHAR sTime[16], sDate[16], sLat[16], sLon[16], sAlt[16], sSpeed[16], sCrs[16];
CHAR sSat[16], sHdop[16];
CHAR sLatz[16], sLonz[16];
ULONG Lat, Lon, Latz, Lonz;
FLOAT Course, Speed;
CHAR sCourse[32];

// プロトタイプ宣言
FLOAT CourseTo(LONG lat1, LONG lon1, LONG lat2, LONG lon2);

void setup()
{
	pinMode(START_SW, INPUT_PULLUP);
	pinMode(LOGTYPE, INPUT_PULLUP);

	Serial.begin(115200);

	if (!SD.begin(SD_CS))
	{
		Serial.println("Card failed, or not present");
		return;
	}
}

void loop()
{
	if (digitalRead(LOGTYPE))
	{
		SendNMEA();
	}
	else
	{
		SendLog();
	}
}
// Logファイル送信
void SendLog()
{
	File dataFile;
	static ULONG itvl;
	BOOL fstart = FALSE;

	if (dataFile = SD.open("Log.txt"))	// ファイルオープン
	{
		while (fgets(buf, MAXBUF, dataFile) != NULL)
		{
			Serial.print(buf);
			if (buf[strlen(buf)-1] == 0x0a)
			{
				while (millis() - itvl < 100) ;
				itvl = millis();

				if (!digitalRead(START_SW))	// 開始SW押された？
				{
					fstart = TRUE;
				}
				if (!fstart)				// SWが押される前は
				{
					dataFile.seek(0);		// 先頭に戻る
				}
			}
		}
		dataFile.close();					// ファイルクローズ
		while (digitalRead(START_SW)) ;		// SWが押されるまで出力停止
		fstart = FALSE;
		delay(1000);
	}
	else
	{
		Serial.println("File not found.");
		while(1);
	}
}
// NMEAファイル送信
void SendNMEA() 
{
	File dataFile;
	static ULONG itvl;
	CHAR s[90], s1[90];
	UCHAR sum;
	SHORT i;

	if (dataFile = SD.open("NMEA.txt"))		// ファイルオープン
	{
		while (fgets(buf, MAXBUF, dataFile) != NULL)
		{
			if (strncmp(buf + 3, "RMC", 3) == 0)
			{
				strtok(buf, ",");	// $GPRMC
				strtok(NULL, ",");	// time
				strtok(NULL, ",");	// A
				strtok(NULL, ",");	// lat
				strtok(NULL, ",");	// N
				strtok(NULL, ",");	// lon
				strtok(NULL, ",");	// E
				strcpy(sSpeed, strtok(NULL, ","));	// speed
				strcpy(sCrs, strtok(NULL, ","));	// cource
				strcpy(sDate, strtok(NULL, ","));	// date
				Speed = atof(sSpeed) * 1.852;
				// コースを計算で求めるか
				// (昔取得したGPSロガーでは方位が記録されていない)
				fCourseCalc = BOOLSET(atof(sCrs) == 0.0);
				if (fCourseCalc)
				{
					strcpy(sCrs, sCourse);
				}

				sprintf(s, "$GPRMC,%s,A,%s,N,%s,E,%s,%s,%s,,,",
					sTime,
					sLat,
					sLon,
					sSpeed,
					fCourseCalc ? sCourse : sCrs,
					sDate
					);
				sum = 0;

				for (i = 1; i <= strlen(s) - 1; i++)
				{
					sum ^= s[i];
				}
				sprintf(s1, "%s*%02X", s, sum);
				while (millis() - itvl < 1000) ;
				itvl = millis();
			}
			if (strncmp(buf + 3, "GGA", 3) == 0)
			{
				strtok(buf, ",");	// $GPGGA
				strcpy(sTime, strtok(NULL, ","));	// time
				strcpy(sLat, strtok(NULL, ","));	// lat
				strtok(NULL, ",");	// N
				strcpy(sLon, strtok(NULL, ","));	// lon
				strtok(NULL, ",");	// E
				strtok(NULL, ",");	// Quality(0/1/2)
				strcpy(sSat, strtok(NULL, ","));	// Satellites
				strcpy(sHdop, strtok(NULL, ","));	// HDOP
				strcpy(sAlt, strtok(NULL, ","));	// Altitude
				Lat = parse_degrees(sLat);
				Lon = parse_degrees(sLon);
				Course = CourseTo(Latz, Lonz, Lat, Lon);
				if (Speed >= 10.0)	// 走行しているなら
					dtostrf(Course, 6, 2, sCourse);	// 方位を用意
				strcpy(sLatz, sLat);
				strcpy(sLonz, sLon);
				Latz = Lat;
				Lonz = Lon;
				sprintf(s,"$GNGGA,%s,%s,N,%s,E,1,%s,%s,%s,M,33.3,M,,0000",
					sTime,
					sLat,
					sLon,
					sSat,
					sHdop,
					sAlt
					);
				sum = 0;
				for (i = 1; i <= strlen(s) - 1; i++)
				{
					sum ^= s[i];
				}
				sprintf(s1, "%s*%02X", s, sum);
			}
			Serial.println(s1);
		}
		dataFile.close();
	}
	else
	{
		Serial.println("File not found.");
		while(1);
	}
}
// ファイルから1行読む
CHAR* fgets(CHAR* buf, SHORT n, File &fp)
{
	CHAR c;
	SHORT i;

	for (i = 0; i < (n - 1); )
	{
		if (!fp.available()) break;
		c = fp.read();
		buf[i++] = c;
		if (c == '\n') break;
	}
	buf[i] = '\0';
	if (i == 0) return(NULL);
	return buf;
}
 bool gpsisdigit(char c) { return c >= '0' && c <= '9'; }

// 方位文字列を[deg]x1e7に変換
unsigned long parse_degrees(CHAR* _term)
{
  char *p;
  unsigned long left_of_decimal;
  unsigned long hundred10000ths_of_minute;
  unsigned long mult = 100000;

  left_of_decimal = gpsatol(_term);
  hundred10000ths_of_minute = (left_of_decimal % 100UL) * 1000000UL;
  
  for (p=_term; gpsisdigit(*p); ++p);
  if (*p == '.')
  {
	while (gpsisdigit(*++p))
	{
	  hundred10000ths_of_minute += mult * (*p - '0');
	  mult /= 10;
	}
  }
  return (left_of_decimal / 100) * L1e7 +
  				(hundred10000ths_of_minute + 3) / 6;
}
// 小数点から上の文字を数値に変換
long gpsatol(char *str)
{
  long ret = 0;
  while (gpsisdigit(*str))
	ret = 10 * ret + *str++ - '0';
  return ret;
}
/*----------------------------------------------------------------------------
	2つの座標間の方位を返す

	書式 ret = CourseTo(LONG lat1, LONG lon1, LONG lat2, LONG lon2);

	FLOAT ret;	方位0～360[deg]
	LONG lat1;	緯度1[deg]x1e7
	LONG lon1;	経度1[deg]x1e7
	LONG lat2;	緯度2[deg]x1e7
	LONG lon2;	経度2[deg]x1e7
----------------------------------------------------------------------------*/
FLOAT CourseTo(LONG lat1, LONG lon1, LONG lat2, LONG lon2)
{
	FLOAT x, y, z;
	static FLOAT ans;

	// 横方向の移動距離/地球半径(右が正)
	x = sin(radians((FLOAT)(lon2 - lon1) / F1e7)) * 
			abs(cos(radians((FLOAT)lat2 / F1e7)));
	// 縦方向の移動距離/地球半径(上が正)
	y = sin(radians((FLOAT)(lat2 - lat1) / F1e7));

	if (x != 0.0)
	{
		z = abs(degrees(atan(y / x)));
		if (x >= 0.0 && y >= .00)
		{
			ans = 90.0 - z;
		}
		else if (x >= 0.0 && y < 0.0)
		{
			ans = 90.0 + z;
		}
		else if (x < 0.0 && y < 0.0)
		{
			ans = 270.0 - z;
		}
		else
		{
			ans = 270.0 + z;
		}
	}
	return ans;
}
/*** end of "TxGpsLog.ino" ***/
