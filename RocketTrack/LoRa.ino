
/*---------------------------------------------------*\
|                                                     |
| LoRa radio code, for downlink, uplink and repeating |
|                                                     |
| Messages can be timed using a GPS reference, to     |
| comply with the TDMA timing requirements.           |
|                                                     |
| Connections:                                        |
|                                                     |
|               Arduino  X - RFM98W DIO5              |
|               Arduino  X - RFM98W DIO0              |
|                                                     |
|               Arduino  X  - RFM98W NSS              |
|               Arduino 11 - RFM98W MOSI              |
|               Arduino 12 - RFM98W MISO              |
|               Arduino 13 - RFM98W CLK               |
|                                                     |
\*---------------------------------------------------*/

//#include <SPI.h>
#include <string.h>

#include "LoRa.h"
#include "Packetisation.h"

typedef enum {lmIdle, lmListening, lmSending} tLoRaMode;

bool LoRaTransmit=0;

uint32_t TXStartTimeMillis;





// LORA settings
#define LORA_FREQUENCY	434.650
#define LORA_OFFSET		0         // Frequency to add in kHz to make Tx frequency accurate

#define LORA_ID			0
#define LORA_MODE		0

//------------------------------------------------------------------------------------------------------

//------------------------------------------------------------------------------------------------------

// HARDWARE DEFINITION

#define LORA_NSS			18		// Comment out to disable LoRa code
#define LORA_RESET			14		// Comment out if not connected
#define LORA_DIO0			26






double lora_frequency=LORA_FREQUENCY;
double lora_offset=LORA_OFFSET;
uint8_t lora_mode=0;

bool lora_constant_transmit=false;

tLoRaMode LoRaMode;
byte currentMode=0x81;
int ImplicitOrExplicit;

uint8_t TXPacket[MAX_TX_PACKET_LENGTH];
uint8_t TxPacketLength;

uint16_t TXPacketCounter=0;

uint32_t LastLoRaTX=0;

int SetupLoRa(void)
{
	setupRFM98(LORA_FREQUENCY,LORA_MODE);
	
	pinMode(USER_BUTTON,INPUT);
	
	delay(100);
	
	if(!digitalRead(USER_BUTTON))
	{
		Serial.println("Using permanent GPS transmit mode ...");
		lora_constant_transmit=true;
	}
	
	return(0);
}

void setupRFM98(double Frequency,int Mode)
{
	int PayloadLength;
	
	// initialize the pins
#ifdef LORA_RESET
	Serial.println("Resetting LoRa Module ...");
	pinMode(LORA_RESET,OUTPUT);
	digitalWrite(LORA_RESET,LOW);
	delay(10);
	digitalWrite(LORA_RESET,HIGH);
//	delay(1000);          // Module needs this before it's ready on these boards (slow power up ?)
	delay(100);
	Serial.println("Reset LoRa Module");
#endif
	
	pinMode(LORA_NSS,OUTPUT);
	pinMode(LORA_DIO0,INPUT);

	SPI.begin(SCK,MISO,MOSI);
	
	// LoRa mode 
	setLoRaMode();

	// Frequency
	setFrequency(Frequency+lora_offset/1000.0);
	
	setLoraOperatingMode(Mode);
	
	writeRegister(REG_PAYLOAD_LENGTH,PayloadLength);
	
	// Change the DIO mapping to 01 so we can listen for TxDone on the interrupt
	writeRegister(REG_DIO_MAPPING_1,0x40);
	writeRegister(REG_DIO_MAPPING_2,0x00);
	
	// Go to standby mode
	setMode(RF98_MODE_STANDBY);
	
	Serial.println("Setup Complete");
}

void setLoraOperatingMode(int Mode)
{
	int ErrorCoding;
	int Bandwidth;
	int SpreadingFactor;
	int LowDataRateOptimize;
	
	if(lora_mode==0)
	{
		// this transmits 16 bytes of data in 5135ms
		ImplicitOrExplicit=EXPLICIT_MODE;	ErrorCoding=ERROR_CODING_4_8;	Bandwidth=BANDWIDTH_20K8;	SpreadingFactor=SPREADING_11;	LowDataRateOptimize=0x08;
	}
	else
	{
		// this transmits 16 bytes of data in 139ms
		ImplicitOrExplicit=EXPLICIT_MODE;	ErrorCoding=ERROR_CODING_4_8;	Bandwidth=BANDWIDTH_62K5;	SpreadingFactor=SPREADING_7;	LowDataRateOptimize=0;
	}
	
	writeRegister(REG_MODEM_CONFIG,ImplicitOrExplicit|ErrorCoding|Bandwidth);
	writeRegister(REG_MODEM_CONFIG2,SpreadingFactor|CRC_ON);
	writeRegister(REG_MODEM_CONFIG3,0x04|LowDataRateOptimize);	// 0x04: AGC sets LNA gain
	
	// writeRegister(REG_DETECT_OPT, (SpreadingFactor == SPREADING_6) ? 0x05 : 0x03);					// 0x05 For SF6; 0x03 otherwise
	writeRegister(REG_DETECT_OPT,(readRegister(REG_DETECT_OPT)&0xF8)|((SpreadingFactor==SPREADING_6)?0x05:0x03));  // 0x05 For SF6; 0x03 otherwise
	
	writeRegister(REG_DETECTION_THRESHOLD,(SpreadingFactor==SPREADING_6)?0x0C:0x0A);		// 0x0C for SF6, 0x0A otherwise  
}

void setFrequency(double Frequency)
{
	unsigned long FrequencyValue;
	
	Serial.print("Frequency is ");
	Serial.println(Frequency);
	
	Frequency=Frequency*7110656/434;
	FrequencyValue=(unsigned long)(Frequency);
	
#if (DEBUG>1)
	Serial.printf("FrequencyValue is %d\r\n",FrequencyValue);
#endif
	
	// Set frequency
	writeRegister(0x06,(FrequencyValue>>16)&0xFF);
	writeRegister(0x07,(FrequencyValue>>8)&0xFF);
	writeRegister(0x08,FrequencyValue&0xFF);
}

void setLoRaMode()
{
	Serial.println("Setting LoRa Mode");
	
	setMode(RF98_MODE_SLEEP);
	writeRegister(REG_OPMODE,0x80);
}

void setMode(byte newMode)
{
	if(newMode==currentMode)
		return;  
	
#if (DEBUG>1)
	Serial.printf("Set LoRa Mode %d\n",newMode);
#endif
	
	switch(newMode) 
	{
		case RF98_MODE_TX:				writeRegister(REG_LNA,LNA_OFF_GAIN);		// TURN LNA OFF FOR TRANSMIT
										writeRegister(REG_PA_CONFIG,PA_MAX_UK);
										writeRegister(REG_OPMODE,newMode);
										TXStartTimeMillis=millis();
//										ControlLED(AXP20X_LED_BLINK_4HZ);
										currentMode=newMode; 
										break;
		
		case RF98_MODE_RX_CONTINUOUS:	writeRegister(REG_PA_CONFIG,PA_OFF_BOOST);	// TURN PA OFF FOR RECEIVE??
										writeRegister(REG_LNA,LNA_MAX_GAIN);  		// MAX GAIN FOR RECEIVE
										writeRegister(REG_OPMODE,newMode);
//										ControlLED(AXP20X_LED_BLINK_1HZ);
										currentMode=newMode;	 
										break;
		
		case RF98_MODE_SLEEP:			writeRegister(REG_OPMODE,newMode);
//										ControlLED(AXP20X_LED_OFF);
										currentMode=newMode; 
										break;
		
		case RF98_MODE_STANDBY:			writeRegister(REG_OPMODE,newMode);
//										ControlLED(AXP20X_LED_OFF);
										currentMode=newMode; 
										break;
		
		default: 						// do nothing
										return;
	} 
	
	if(newMode!=RF98_MODE_SLEEP)
		delay(10);
}

byte readRegister(byte addr)
{
	LoRa_select();
	SPI.transfer(addr&0x7F);
	byte regval=SPI.transfer(0);
	LoRa_unselect();
	
#if (DEBUG>2)
	Serial.printf("RD Reg %02X=%02X\n",addr,regval);
#endif
	
	return regval;
}

void writeRegister(byte addr,byte value)
{
	LoRa_select();
	SPI.transfer(addr|0x80); // OR address with 10000000 to indicate write enable;
	SPI.transfer(value);
	LoRa_unselect();

#if (DEBUG>2)
	Serial.printf("WR Reg %02X=%02X\n",addr,value);
#endif
}

void LoRa_select() 
{
	digitalWrite(LORA_NSS,LOW);
}

void LoRa_unselect() 
{
	digitalWrite(LORA_NSS,HIGH);
}

/*
int LoRaIsFree(void)
{
	if(		(LoRaMode!=lmSending)
		||	digitalRead(LORA_DIO0)	)
	{
		// Either not sending, or was but now it's sent.  Clear the flag if we need to
		if(LoRaMode==lmSending)
		{
			// Clear that IRQ flag
			writeRegister(REG_IRQ_FLAGS,0x08); 
			LoRaMode=lmIdle;
			
			ControlLED(AXP20X_LED_OFF);
			
			uint32_t TXBurstTime=millis()-TXStartTimeMillis;
			Serial.print("Tx burst time=");	Serial.print(TXBurstTime);	Serial.println(" ms");
		}
	}
	
	return 0;
}
*/

void SendLoRaPacket(unsigned char *buffer,int Length)
{
	int i;
	
	LastLoRaTX=millis();
	
//	setupRFM98(LORA_FREQUENCY,LORA_MODE);
	
	Serial.print("Sending "); Serial.print(Length);	Serial.println(" bytes");
	
	setMode(RF98_MODE_STANDBY);
	
	// 01 00 00 00 maps DIO0 to TxDone
	writeRegister(REG_DIO_MAPPING_1,0x40);
	
	// Update the address ptr to the current tx base address
	writeRegister(REG_FIFO_TX_BASE_AD,0x00);
	writeRegister(REG_FIFO_ADDR_PTR,0x00); 
	
	if(ImplicitOrExplicit==EXPLICIT_MODE)
	{
		writeRegister(REG_PAYLOAD_LENGTH,Length);
	}
	
	LoRa_select();
	
	SPI.transfer(REG_FIFO|0x80);

	for(i=0;i<Length;i++)
	{
#if (DEBUG>2)
		Serial.printf("WR Reg %02X=%02X\n",REG_FIFO|0x80,buffer[i]);
#endif
		SPI.transfer(buffer[i]);
	}
	
	LoRa_unselect();
	
	// go into transmit mode
	setMode(RF98_MODE_TX);
	
	LoRaMode=lmSending;
}

int LORACommandHandler(uint8_t *cmd,uint16_t cmdptr)
{
#if (DEBUG>0)
	Serial.println((char *)cmd);
#endif
	
	int retval=1;
	
	switch(cmd[1]|0x20)
	{
		case 't':	Serial.println("Transmitting LoRa packet");
					memcpy(TXPacket,"$$Hello world!\r\n",16);
					EncryptPacket(TXPacket);
					TxPacketLength=16;
					LoRaTransmit=1;
					break;
		
		case 'g':	Serial.println("Transmitting GPS LoRa packet");
					PackPacket();
					EncryptPacket(TXPacket);
					LoRaTransmit=1;
					break;
		
		case 'l':	Serial.println("Long range mode");
					lora_mode=0;
					break;
		
		case 'h':	Serial.println("High rate mode");
					lora_mode=1;
					break;
		
		case 'm':	if(lora_mode==0)	Serial.print("Long range mode\r\n");
					else				Serial.print("High rate mode\r\n");
					break;
					
		case 'c':	lora_constant_transmit=!lora_constant_transmit;
					Serial.printf("Setting constant transmit mode to %d\r\n",lora_constant_transmit);
					break;
		
		case '+':	lora_offset+=1.0;
					Serial.printf("LoRa offset = %.1f\n",lora_offset);
					break;
		
		case '-':	lora_offset-=1.0;
					Serial.printf("LoRa offset = %.1f\n",lora_offset);
					break;
		
		case '?':	Serial.print("LoRa Test Harness\r\n================\r\n\n");
					Serial.print("t\t-\tTransmit a test packet\r\n");
					Serial.print("g\t-\tTransmit a GPS packet\r\n");
					Serial.print("h\t-\tSet high rate mode\r\n");
					Serial.print("l\t-\tSet long range mode\r\n");
					Serial.print("m\t-\tCheck operating mode\r\n");
					Serial.print("c\t-\tConstant Transmit on/off\r\n");
					Serial.print("+\t-\tIncrement LoRa offset\r\n");
					Serial.print("-\t-\tDecrement LoRa offset\r\n");
					Serial.print("?\t-\tShow this menu\r\n");
					break;
					
		default:	// ignore
					break;
	}
	
	return(retval);
}

void PollLoRa(void)
{
	// wait for DIO0 to indicate end of transmission
	
	if(digitalRead(LORA_DIO0))
	{
		// Clear that IRQ flag
		writeRegister(REG_IRQ_FLAGS,0x08); 
		LoRaMode=lmIdle;
		
		ControlLED(AXP20X_LED_OFF);
		
		uint32_t TXBurstTime=millis()-TXStartTimeMillis;
		Serial.printf("Tx burst time = %d ms\r\n",TXBurstTime);
	}
	
	if(LoRaTransmit)
	{
//		ControlLED(AXP20X_LED_BLINK_4HZ);
		ControlLED(AXP20X_LED_LOW_LEVEL);
		
		if(TxPacketLength>0)
		{
			setupRFM98(lora_frequency,lora_mode);
			SendLoRaPacket(TXPacket,TxPacketLength);
		}
		
		LoRaTransmit=0;
	}
}
