#ifndef	_ULIB_H
#define	_ULIB_H

#define	TRUE	1
#define	FALSE	0
#define	ERR	(-1)
#define	OK		0
#define	YES		1
#define	NO		0

#define BOOL unsigned char
#define CHAR char
#define UCHAR unsigned char
#define SHORT int
#define USHORT unsigned int
#define LONG long
#define ULONG unsigned long
#define FLOAT float
#define DOUBLE double

/* macros */
#ifdef max
#undef max
#endif
#ifdef min
#undef min
#endif
#ifdef ABS
#undef ABS
#endif
#ifdef BOOLSET
#undef BOOLSET
#endif
#define max(x,y) (((x) > (y)) ? (x) : (y))
#define min(x,y) (((x) < (y)) ? (x) : (y))
#define ABS(x) (((x) >= 0) ? (x) : -(x))
#define BOOLSET(x) (BOOL)(((x) != 0) ? TRUE : FALSE)

#ifndef NULL
#define NULL ((void*)0)
#endif

/* prototypes */
SHORT stcu_d(CHAR* buf, USHORT data);
SHORT stci_d(CHAR* buf, SHORT data);
SHORT stcu_h(CHAR* buf, USHORT data);
UCHAR RangeUCHAR(SHORT);
CHAR RangeCHAR(SHORT);
SHORT RangeSHORT(LONG);
SHORT DuDt(SHORT, SHORT*, BOOL*);
SHORT Limiter(SHORT, SHORT, SHORT);
SHORT RateLimiter(SHORT, SHORT, SHORT, SHORT*, BOOL*);
SHORT ShortFilter(SHORT, LONG, LONG, LONG*);
void InitShortFilter(SHORT, LONG, LONG, LONG*);
void AddOnTime(BOOL, SHORT*);
void AddOnTimeUCHAR(BOOL flag, UCHAR* ontime);
void AddOnOffTime(BOOL, SHORT*, SHORT*);
void AddOnOffTimeUCHAR(BOOL flag, UCHAR* ontime, UCHAR* offtime);
SHORT DecNonZero(SHORT* data);
UCHAR DecNonZeroUCHAR(UCHAR* data);
SHORT Average(SHORT data, SHORT* n, LONG* sum);
void InitAverage(SHORT* n, LONG* sum);
SHORT BiFilt(SHORT time, SHORT in, SHORT *inz, SHORT *outz);
void InitBiFilt(SHORT in, SHORT *inz, SHORT *outz);
SHORT TableData(SHORT x, SHORT n, SHORT* p);
SHORT Lpf008(SHORT v, LONG* Z008);
void InitLpf008(SHORT v, LONG* Z008);
SHORT Lpf006(SHORT v, LONG* Z006);
void InitLpf006(SHORT v, LONG* Z006);

#endif /* _ULIB_H */
