
#include "Adafruit_GPS.h"

#include "GPS.h"
#include "Packetisation.h"

// to be set from the config file

char gps_type[32];
int initial_baud;
int final_baud;
int fix_rate;

#define DEBUG 1

#define GPS_PASSTHROUGH 0
#define BINARY_GPS		1

#define MAX_CHANNELS 50

// Globals
byte RequiredFlightMode=0;
byte GlonassMode=0;
byte RequiredPowerMode=-1;
byte LastCommand1=0;
byte LastCommand2=0;
byte HaveHadALock=0;

bool gps_live_mode=0;

// these are all unpacked from UBX messages

// from NAV-STATUS
uint32_t iTOW=0;
uint8_t gpsFix=0;
uint8_t flags=0;
uint8_t fixStat=0;
uint8_t flags2=0;
uint32_t ttff=0;
uint32_t msss=0;

// from NAV-SVINFO
uint8_t beaconnumCh=0;
uint8_t globalFlags=0;
uint16_t reserved2=0;

uint8_t chn[MAX_CHANNELS];
uint8_t svid[MAX_CHANNELS];
uint8_t svflags[MAX_CHANNELS];
uint8_t quality[MAX_CHANNELS];
uint8_t cno[MAX_CHANNELS];
int8_t elev[MAX_CHANNELS];
int16_t azim[MAX_CHANNELS];
int32_t prRes[MAX_CHANNELS];

uint8_t beaconnumSats=0;

// from NAV-POSLLH
//int32_t gps_lon=0;
//int32_t gps_lat=0;
//int32_t gps_height=0;
//int32_t gps_hMSL=0;
//int32_t max_beaconhMSL=0;
//uint32_t gps_hAcc=0;
//uint32_t gps_vAcc=0;

//uint8_t gps_hAccValue=0;

// from NAV-TIMEUTC

uint16_t beaconyear;
uint8_t beaconmonth;
uint8_t beaconday;
uint8_t beaconhour;
uint8_t beaconmin;
uint8_t beaconsec;

Adafruit_GPS gps(&Serial1);

int SetupGPS(void)
{
#if (DEBUG>0)
	Serial.println("Open GPS port");
#endif
#ifdef ARDUINO_TBEAM_USE_RADIO_SX1276
	Serial.println("\tSetting up GPS port for the T-Beam 3 button with a Neo8 module");
#endif
#ifdef ARDUINO_XIAO_ESP32S3
	Serial.println("\tSetting up GPS port for the Xiao Esp32s3 with an MTK3333 module");
#endif

	Serial1.begin(9600,SERIAL_8N1,UART_TXD,UART_RXD);
	gps.begin(9600);
	
	return(0);
}

void PollGPS(void)
{
	static uint8_t buffer[1024];
	static uint16_t bufferptr=0;
	uint8_t rxbyte=0x00;
	static uint8_t lastbyte=0x00;
	
#if GPS_PASSTHROUGH
	if(Serial1.available())	{	rxbyte=Serial1.read();	Serial.write(rxbyte);	}
	if(Serial.available())	{	rxbyte=Serial.read();	Serial1.write(rxbyte);	}
#else
	while(Serial1.available())
	{
		rxbyte=Serial1.read();
		
		if(gps_live_mode)
			Serial.write(rxbyte);
		
		if((lastbyte==0xb5)&&(rxbyte==0x62))
		{
			ProcessUBX(buffer,bufferptr);
			
			// this is the start of a ubx message so we have a full one stored, process it
			buffer[0]=lastbyte;
			buffer[1]=rxbyte;
			bufferptr=2;
		}
		else
		{
			// this is the middle of a ubx message
			
			if(bufferptr<sizeof(buffer))
			{
				buffer[bufferptr++]=rxbyte;
			}
		}
		
		lastbyte=rxbyte;
	}
#endif
}

void ProcessUBX(uint8_t *buffer,uint16_t bufferptr)
{
	if(bufferptr<=6)
	{
		// this is an invalid ubx message, we need at least the ident, two bytes 
		// for the message type and two bytes of checksum
		return;
	}
	
//	if(!CheckChecksum(buffer,bufferptr))
//	{
//		return;	// failed the checksum test
//	}
	
	if((buffer[2]==0x01)&&(buffer[3]==0x02))	UnpackNAVPOSLLH(buffer);
	if((buffer[2]==0x01)&&(buffer[3]==0x03))	UnpackNAVSTATUS(buffer);
	if((buffer[2]==0x01)&&(buffer[3]==0x30))	UnpackNAVSVINFO(buffer);
	if((buffer[2]==0x01)&&(buffer[3]==0x21))	UnpackNAVTIMEUTC(buffer);
}

void UnpackNAVPOSLLH(uint8_t *buffer)
{
#if (DEBUG>1)
	Serial.println("\t\tNAV-POSLLH");
#endif
	
	iTOW=*((uint32_t *)(buffer+6));
	lastfix.longitude=*((int32_t *)(buffer+10));
	lastfix.latitude=*((int32_t *)(buffer+14));
//	lastfix.height=*((int32_t *)(buffer+18));
	lastfix.height=*((int32_t *)(buffer+22));	// hMSL
	uint32_t hacc=*((uint32_t *)(buffer+26));
	uint32_t vacc=*((uint32_t *)(buffer+30));
	
	if((hacc/500)>255)	lastfix.accuracy=(float)255;
	else				lastfix.accuracy=(float)(hacc/500);

#if 0	
	if(gpsFix==3)
		if(max_lastfix.height<lastfix.height)
			max_lastfix.height=lastfix.height;
#endif	
#if 0
	Serial.printf("\t\thAcc = %ld mm\n",lastfix.hAcc);
#endif
#if (DEBUG>2)
	Serial.printf("\t\tLat = %.6f, Lon = %.6f, ",lastfix.lat/1e7,lastfix.lon/1e7);
	Serial.printf("height = %.1f\r\n",lastfix.hMSL/1e3);
#endif
	
	baro_trigger=true;
}

void UnpackNAVSTATUS(uint8_t *buffer)
{
#if (DEBUG>1)
	Serial.println("NAV-STATUS");
#endif
	
	iTOW=*((uint32_t *)(buffer+6));
	gpsFix=*(buffer+10);
	flags=*(buffer+11);
	fixStat=*(buffer+12);
	flags2=*(buffer+13);
	ttff=*((uint32_t *)(buffer+14));
	msss=*((uint32_t *)(buffer+18));
	
#if (DEBUG>2)
	if(gpsFix==0x00)		Serial.println("No Fix");
	else if(gpsFix==0x02)	Serial.println("2D Fix");
	else if(gpsFix==0x03)	Serial.println("3D Fix");
#endif
}

void UnpackNAVTIMEUTC(uint8_t *buffer)
{
#if (DEBUG>1)
	Serial.println("NAV-TIMEUTC");
#endif
	
	iTOW=*((uint32_t *)(buffer+6));
	
	beaconyear=*((uint16_t *)(buffer+18));
	beaconmonth=*(buffer+20);
	beaconday=*(buffer+21);
	beaconhour=*(buffer+22);
	beaconmin=*(buffer+23);
	beaconsec=*(buffer+24);
	
#if (DEBUG>2)
	char buffer[32];
	sprintf(buffer,"%04d/%02d/%02d %02d:%02d:%02d\r\n",beaconyear,beaconmonth,beaconday,beaconhour,beaconmin,beaconsec);
	display.print(buffer);	
#endif
}

void UnpackNAVSVINFO(uint8_t *buffer)
{
#if (DEBUG>1)
	Serial.println("\tNAV-SVINFO");
#endif
	
	iTOW=*((uint32_t *)(buffer+6));
	beaconnumCh=*(buffer+10);
	globalFlags=*(buffer+11);
	reserved2=*((uint16_t *)(buffer+12));
	
	uint8_t cnt;
	beaconnumSats=0;
	for(cnt=0;cnt<beaconnumCh;cnt++)
	{
		chn[cnt]=*(buffer+14+12*cnt);
		svid[cnt]=*(buffer+15+12*cnt);
		svflags[cnt]=*(buffer+16+12*cnt);
		quality[cnt]=*(buffer+17+12*cnt);
		cno[cnt]=*(buffer+18+12*cnt);
		elev[cnt]=*((int8_t *)(buffer+19+12*cnt));
		azim[cnt]=*((int16_t *)(buffer+20+12*cnt));
		prRes[cnt]=*((int32_t *)(buffer+22+12*cnt));
		
		if(cno[cnt]>0)	beaconnumSats++;
	}
	
#if (DEBUG>2)
	Serial.printf("\tnumCh = %d\n",numCh);
#endif
}

int GPSCommandHandler(uint8_t *cmd,uint16_t cmdptr)
{
	// ignore a single key stroke
	if(cmdptr<=2)	return(0);

#if (DEBUG>0)
	Serial.println((char *)cmd);
#endif
	
	int retval=1;
	uint8_t cnt;
	
	switch(cmd[1]|0x20)
	{
		case 'p':	// position fix
					Serial.printf("Latitude = %.6f, Longitude = %.6f, ",lastfix.latitude/1e7,lastfix.longitude/1e7);
					Serial.printf("height = %.1f\r\n",lastfix.height/1e3);
					break;
		
		case 'f':	// fix status
					if(gpsFix==0x00)		Serial.println("No Fix");
					else if(gpsFix==0x02)	Serial.println("2D Fix");
					else if(gpsFix==0x03)	Serial.println("3D Fix");
					break;
		
		case 's':	// satellite info
					Serial.println("Chan\tPRN\tElev\tAzim\tC/No");
					for(cnt=0;cnt<beaconnumCh;cnt++)
					{
						Serial.print(cnt);	Serial.print("\t"); Serial.print(svid[cnt]);	Serial.print("\t");	Serial.print(elev[cnt]);	Serial.print("\t");	Serial.print(azim[cnt]);	Serial.print("\t");	Serial.println(cno[cnt]);
					}
					
					break;
		
		case 'l':	// live mode toggle
					gps_live_mode=!gps_live_mode;
					break;
		
		case '?':	Serial.print("GPS Test Harness\r\n================\r\n\n");
					Serial.print("p\t-\tCheck positon\r\n");
					Serial.print("f\t-\tCheck fix status\r\n");
					Serial.print("s\t-\tCheck satellite status\r\n");
					Serial.print("l\t-\tLive GPS data on/off\r\n");
					Serial.print("?\t-\tShow this menu\r\n");
					break;
		
		default:	retval=0;
					break;
	}
	
	return(retval);
}
