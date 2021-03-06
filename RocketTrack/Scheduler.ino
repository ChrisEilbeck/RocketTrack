
#define USER_BUTTON			38

uint32_t next_transmit=0;

int SetupScheduler(void)
{
	pinMode(USER_BUTTON,INPUT);
	if(digitalRead(USER_BUTTON)==0)
		lora_constant_transmit=true;
	
	return(0);
}

bool last_user_button=true;
int button_timer=0;

void PollScheduler(void)
{
	bool user_button=digitalRead(USER_BUTTON);
	
	if(!user_button&&last_user_button)
	{
		Serial.println("Button pressed");
		button_timer=millis();
	}
	
	if(user_button&&!last_user_button)
	{
		Serial.println("Button released");
		
		if(millis()>(button_timer+5000))
		{
			lora_mode=!lora_mode;
			SetLoRaMode(lora_mode);
			Serial.print("Toggling lora_mode to ");
			Serial.println(lora_mode);
		}
		else if(millis()>(button_timer+1000))
		{
			lora_constant_transmit=!lora_constant_transmit;
			Serial.print("Setting constant transmit to ");
			Serial.println(lora_constant_transmit);
		}
	}
	
	last_user_button=user_button;
	
	if(millis()>=next_transmit)
	{
		if(		(LoRaTransmitSemaphore==0)
			&&	(lora_constant_transmit)	)
		{
			PackPacket(TxPacket,&TxPacketLength);
			EncryptPacket(TxPacket);
//			TxPacketLength=16;
			LoRaTransmitSemaphore=1;
			
			if(lora_mode==0)
			{
				next_transmit=millis()+10000;
				LedPattern=0xf0f0f0f0;
				LedRepeatCount=1;
				LedBitCount=0;										
			}
			else
			{
				next_transmit=millis()+1000;
				LedPattern=0xaaaaaaaa;
				LedRepeatCount=1;
				LedBitCount=0;					
			}
			
			Serial.printf("millis() = %d\r\n",millis());
		}
	}
}

