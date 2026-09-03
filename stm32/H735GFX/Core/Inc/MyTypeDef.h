
// MyTyptdef.h
#pragma once

#include <stdio.h>


typedef	unsigned char	U8, *PU8;
typedef	signed char		I8, *PI8;
typedef	unsigned short	U16, *PU16;
typedef	short			I16, *PI16;
typedef	unsigned long	U32, *PU32;
typedef	long			I32, *PI32;

typedef long long 			I64;
typedef unsigned long long  U64;

#ifndef WIN32
typedef long			LONG;
typedef unsigned int	UINT;
typedef int				INT;
typedef	unsigned char	UCHAR;
typedef	char			CHAR;
typedef	unsigned short	USHORT;
typedef	short			SHORT;

typedef unsigned short	UWORD;
typedef	short			WORD;

#ifdef __cplusplus 
typedef	bool	    BOOL;
#else
typedef	I32	    BOOL;
#endif 

typedef	void 		VOID, *PVOID;
typedef unsigned int	HANDLE32, * PHANDLE32;
typedef unsigned long long HANDLE64;

#endif

typedef signed   char   INT8, *PINT8;
typedef unsigned char	UINT8, *PUINT8;
typedef signed   short	INT16, *PINT16;
typedef unsigned short	UINT16, *PUINT16;
typedef signed   int	INT32, *PINT32;
typedef unsigned int    UINT32, *PUINT32;
typedef signed   long long INT64, *PINT64;
typedef unsigned long long UINT64, *PUINT64;
typedef unsigned int	BOOL32, *PBOOL32;

typedef signed   char   INT8S, * PINT8S;
typedef unsigned char	INT8U, * PINT8U;
typedef signed   short	INT16S, * PINT16S;
typedef unsigned short	INT16U, * PINT16U;
typedef signed   int	INT32S, * PINT32S;
typedef unsigned int    INT32U, * PINT32U;
typedef signed   long long INT64S, * PINT64S;
typedef unsigned long long INT64U, * PINT64U;

typedef double			DOUBLE;
typedef float			FLOAT;





#define		BITS_1	0X00000001
#define		BITS_2	0X00000003
#define		BITS_3	0X00000007
#define		BITS_4	0X0000000F
#define		BITS_5	0X0000001F
#define		BITS_6	0X0000003F
#define		BITS_7	0X0000007F
#define		BITS_8	0X000000FF
#define		BITS_9	0X000001FF
#define		BITS_10	0X000003FF
#define		BITS_11	0X000007FF
#define		BITS_12	0X00000FFF
#define		BITS_13	0X00001FFF
#define		BITS_14	0X00003FFF
#define		BITS_15	0X00007FFF
#define		BITS_16	0X0000FFFF
#define		BITS_17 0X0001FFFF
#define		BITS_18	0X0003FFFF
#define		BITS_19	0X0007FFFF
#define		BITS_20	0X000FFFFF
#define		BITS_21	0X001FFFFF
#define		BITS_22	0X003FFFFF
#define		BITS_23	0X007FFFFF
#define		BITS_24	0X00FFFFFF
#define		BITS_25	0X01FFFFFF
#define		BITS_26	0X03FFFFFF
#define		BITS_27	0X07FFFFFF
#define		BITS_28	0X0FFFFFFF
#define		BITS_29	0X1FFFFFFF
#define		BITS_30	0X3FFFFFFF
#define		BITS_31	0X7FFFFFFF
#define		BITS_32	0XFFFFFFFF

/* Define NULL pointer value */
#ifndef NULL
#ifdef __cplusplus
#define NULL    0
#else
#define NULL    ((void *)0)
#endif
#endif

#define     SETBIT(   REG , BIT  )     ( REG ) |= ( 1 << (BIT) )
#define     CLEARBIT( REG , BIT  )     ( REG ) &= ~( 1 << (BIT) )
#define     TESTBIT( REG , BIT )       ( ( (REG)>>(BIT)) & 1 )

#define     COPYBITS( reg , pos , d, len )   ( reg |=  ( reg & (~(BITS_##len<<(pos)))) | (((d)&BITS_##len) << (pos)) )


#define		NOP()		__no_operation()

#define		monitor		__monitor
#define		C_task		__C_task
#define		no_init		__no_init


#define		DisableInterrupt()	__disable_interrupt()
#define		EnableInterrupt()	__enable_interrupt()



void UartPrintf6(char *pcFmt,...);

#ifndef	NDEBUG
//	#define	TRACE(fmt,...)		UartPrintf6(fmt, ##__VA_ARGS__)
#else
	#define	TRACE(fmt,...)
#endif

#define FATAL_PRINT(x,...)		Printf(x, ##__VA_ARGS__)

#define DEBUGBLOCK(...) do{ __VA_ARGS__ }while(0);
/*
  DEBUGBLOCK( 
     char *str = "test";
     printf("%s",str);
   )

*/


#define		FALSE	0
#define		TRUE	1



#define		FREQ_MHz(x)		(x * 1000000.0)
#define 	FREQ_KHz(x) 	(x * 1000.0)
#define 	uSEC(x) 		((double)x / 1000000.0)
#define		SEC2uS(x)		(U32)(x * 1000000)			// 0.000001 Sec ->  000001 uS 
#define 	PERCENT(x) 		((double)x /100.0 )

#define		MIN2SEC( m )		(m*60)

#define		TIME_DELAY(x)	HAL_Delay(x)		// sleep(x)

union uFtoByte{
	float f;
	U8 byte[4];
};


typedef int (*Func)(int, int);

#define   LCD   (*((volatile unsigned long*)0x70000000))

#ifdef __cplusplus 
extern "C" { 
#endif 

#ifdef  __cplusplus 
} 
#endif 



